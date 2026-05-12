#include "threads/palloc.h"
#include <bitmap.h>
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "threads/init.h"
#include "threads/loader.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

/* 페이지 할당자. 페이지 크기 또는 페이지 배수 크기의 덩어리로 메모리를 나누어 준다.
   더 작은 덩어리를 나누어 주는 할당자는 malloc.h를 참조한다.

   시스템 메모리는 커널 풀과 사용자 풀이라는 두 "풀"로 나뉜다. 
   사용자 풀은 사용자 가상 메모리 페이지용이고, 커널 풀은 그 외 모든 용도에 사용된다.
   여기서의 의도는 사용자 프로세스들이 심하게 스와핑 중이더라도 커널이 자신의 작업에 필요한 메모리를 확보하도록 하는 것이다.

   기본적으로 시스템 RAM의 절반은 커널 풀에, 나머지 절반은 사용자 풀에 할당된다.
   커널 풀에는 지나치게 넉넉한 양이지만, 데모 목적으로는 괜찮다. */

/* 메모리 풀. */
struct pool {
	struct lock lock;               /* 상호 배제. */
	struct bitmap *used_map;        /* 빈 페이지들의 비트맵. */
	uint8_t *base;                  /* 풀의 기준 주소. */
};

/* 두 개의 풀: 하나는 커널 데이터용, 하나는 사용자 페이지용. */
static struct pool kernel_pool, user_pool;

/* 사용자 풀에 넣을 최대 페이지 수. */
size_t user_page_limit = SIZE_MAX;
static void
init_pool (struct pool *p, void **bm_base, uint64_t start, uint64_t end);

static bool page_from_pool (const struct pool *, void *page);

/* multiboot 정보 */
struct multiboot_info {
	uint32_t flags;
	uint32_t mem_low;
	uint32_t mem_high;
	uint32_t __unused[8];
	uint32_t mmap_len;
	uint32_t mmap_base;
};

/* e820 엔트리 */
struct e820_entry {
	uint32_t size;
	uint32_t mem_lo;
	uint32_t mem_hi;
	uint32_t len_lo;
	uint32_t len_hi;
	uint32_t type;
};

/* ext_mem/base_mem의 범위 정보를 나타낸다. */
struct area {
	uint64_t start;
	uint64_t end;
	uint64_t size;
};

#define BASE_MEM_THRESHOLD 0x100000
#define USABLE 1
#define ACPI_RECLAIMABLE 3
#define APPEND_HILO(hi, lo) (((uint64_t) ((hi)) << 32) + (lo))

/* e820 엔트리를 순회하며 basemem과 extmem의 범위를 파싱한다. */
static void
resolve_area_info (struct area *base_mem, struct area *ext_mem) {
	struct multiboot_info *mb_info = ptov (MULTIBOOT_INFO);
	struct e820_entry *entries = ptov (mb_info->mmap_base);
	uint32_t i;

	for (i = 0; i < mb_info->mmap_len / sizeof (struct e820_entry); i++) {
		struct e820_entry *entry = &entries[i];
		if (entry->type == ACPI_RECLAIMABLE || entry->type == USABLE) {
			uint64_t start = APPEND_HILO (entry->mem_hi, entry->mem_lo);
			uint64_t size = APPEND_HILO (entry->len_hi, entry->len_lo);
			uint64_t end = start + size;
			printf("%llx ~ %llx %d\n", start, end, entry->type);

			struct area *area = start < BASE_MEM_THRESHOLD ? base_mem : ext_mem;

			// 이 영역에 속하는 첫 번째 엔트리.
			if (area->size == 0) {
				*area = (struct area) {
					.start = start,
					.end = end,
					.size = size,
				};
			} else {  // 그 외의 경우
				// start를 확장한다.
				if (area->start > start)
					area->start = start;
				// end를 확장한다.
				if (area->end < end)
					area->end = end;
				// size를 확장한다.
				area->size += size;
			}
		}
	}
}

/*
 * 풀을 채운다.
 * 코드 페이지를 포함한 모든 페이지는 이 할당자가 관리한다.
 * 기본적으로 메모리의 절반은 커널에, 절반은 사용자에게 준다.
 * base_mem 부분은 가능한 한 커널 쪽으로 밀어 넣는다.
 */
static void
populate_pools (struct area *base_mem, struct area *ext_mem) {
	extern char _end;
	void *free_start = pg_round_up (&_end);

	uint64_t total_pages = (base_mem->size + ext_mem->size) / PGSIZE;
	uint64_t user_pages = total_pages / 2 > user_page_limit ?
		user_page_limit : total_pages / 2;
	uint64_t kern_pages = total_pages - user_pages;

	// E820 맵을 파싱하여 각 풀의 메모리 영역을 확보한다.
	enum { KERN_START, KERN, USER_START, USER } state = KERN_START;
	uint64_t rem = kern_pages;
	uint64_t region_start = 0, end = 0, start, size, size_in_pg;

	struct multiboot_info *mb_info = ptov (MULTIBOOT_INFO);
	struct e820_entry *entries = ptov (mb_info->mmap_base);

	uint32_t i;
	for (i = 0; i < mb_info->mmap_len / sizeof (struct e820_entry); i++) {
		struct e820_entry *entry = &entries[i];
		if (entry->type == ACPI_RECLAIMABLE || entry->type == USABLE) {
			start = (uint64_t) ptov (APPEND_HILO (entry->mem_hi, entry->mem_lo));
			size = APPEND_HILO (entry->len_hi, entry->len_lo);
			end = start + size;
			size_in_pg = size / PGSIZE;

			if (state == KERN_START) {
				region_start = start;
				state = KERN;
			}

			switch (state) {
				case KERN:
					if (rem > size_in_pg) {
						rem -= size_in_pg;
						break;
					}
					// 커널 풀을 생성한다.
					init_pool (&kernel_pool,
							&free_start, region_start, start + rem * PGSIZE);
					// 다음 상태로 전환한다.
					if (rem == size_in_pg) {
						rem = user_pages;
						state = USER_START;
					} else {
						region_start = start + rem * PGSIZE;
						rem = user_pages - size_in_pg + rem;
						state = USER;
					}
					break;
				case USER_START:
					region_start = start;
					state = USER;
					break;
				case USER:
					if (rem > size_in_pg) {
						rem -= size_in_pg;
						break;
					}
					ASSERT (rem == size);
					break;
				default:
					NOT_REACHED ();
			}
		}
	}

	// 사용자 풀을 생성한다.
	init_pool(&user_pool, &free_start, region_start, end);

	// e820_entry를 순회하며 사용 가능 영역을 설정한다.
	uint64_t usable_bound = (uint64_t) free_start;
	struct pool *pool;
	void *pool_end;
	size_t page_idx, page_cnt;

	for (i = 0; i < mb_info->mmap_len / sizeof (struct e820_entry); i++) {
		struct e820_entry *entry = &entries[i];
		if (entry->type == ACPI_RECLAIMABLE || entry->type == USABLE) {
			uint64_t start = (uint64_t)
				ptov (APPEND_HILO (entry->mem_hi, entry->mem_lo));
			uint64_t size = APPEND_HILO (entry->len_hi, entry->len_lo);
			uint64_t end = start + size;

			// TODO: 0x1000 ~ 0x200000을 추가한다. 지금은 문제가 되지 않는다.
			// 모든 페이지가 사용 불가능하다.
			if (end < usable_bound)
				continue;

			start = (uint64_t)
				pg_round_up (start >= usable_bound ? start : usable_bound);
split:
			if (page_from_pool (&kernel_pool, (void *) start))
				pool = &kernel_pool;
			else if (page_from_pool (&user_pool, (void *) start))
				pool = &user_pool;
			else
				NOT_REACHED ();

			pool_end = pool->base + bitmap_size (pool->used_map) * PGSIZE;
			page_idx = pg_no (start) - pg_no (pool->base);
			if ((uint64_t) pool_end < end) {
				page_cnt = ((uint64_t) pool_end - start) / PGSIZE;
				bitmap_set_multiple (pool->used_map, page_idx, page_cnt, false);
				start = (uint64_t) pool_end;
				goto split;
			} else {
				page_cnt = ((uint64_t) end - start) / PGSIZE;
				bitmap_set_multiple (pool->used_map, page_idx, page_cnt, false);
			}
		}
	}
}

/* 페이지 할당자를 초기화하고 메모리 크기를 가져온다. */
uint64_t
palloc_init (void) {
  /* 링커가 기록한 커널의 끝.
     kernel.lds.S를 참조한다. */
	extern char _end;
	struct area base_mem = { .size = 0 };
	struct area ext_mem = { .size = 0 };

	resolve_area_info (&base_mem, &ext_mem);
	printf ("Pintos booting with: \n");
	printf ("\tbase_mem: 0x%llx ~ 0x%llx (Usable: %'llu kB)\n",
		  base_mem.start, base_mem.end, base_mem.size / 1024);
	printf ("\text_mem: 0x%llx ~ 0x%llx (Usable: %'llu kB)\n",
		  ext_mem.start, ext_mem.end, ext_mem.size / 1024);
	populate_pools (&base_mem, &ext_mem);
	return ext_mem.end;
}

/* PAGE_CNT개의 연속된 빈 페이지 그룹을 얻어 반환한다.
   PAL_USER가 설정되어 있으면 사용자 풀에서 페이지를 얻고, 그렇지 않으면 커널 풀에서 얻는다.
   FLAGS에 PAL_ZERO가 설정되어 있으면 페이지를 0으로 채운다.
   사용 가능한 페이지가 너무 적으면 널 포인터를 반환한다.
   단, FLAGS에 PAL_ASSERT가 설정되어 있으면 커널 패닉을 일으킨다. */
void *
palloc_get_multiple (enum palloc_flags flags, size_t page_cnt) {
	struct pool *pool = flags & PAL_USER ? &user_pool : &kernel_pool;

	lock_acquire (&pool->lock);
	size_t page_idx = bitmap_scan_and_flip (pool->used_map, 0, page_cnt, false);
	lock_release (&pool->lock);
	void *pages;

	if (page_idx != BITMAP_ERROR)
		pages = pool->base + PGSIZE * page_idx;
	else
		pages = NULL;

	if (pages) {
		if (flags & PAL_ZERO)
			memset (pages, 0, PGSIZE * page_cnt);
	} else {
		if (flags & PAL_ASSERT)
			PANIC ("palloc_get: out of pages");
	}

	return pages;
}

/* 빈 페이지 하나를 얻어 그 커널 가상 주소를 반환한다.
   PAL_USER가 설정되어 있으면 사용자 풀에서 페이지를 얻고, 그렇지 않으면 커널 풀에서 얻는다.
   FLAGS에 PAL_ZERO가 설정되어 있으면 페이지를 0으로 채운다.
   사용 가능한 페이지가 없으면 널 포인터를 반환한다.
   단, FLAGS에 PAL_ASSERT가 설정되어 있으면 커널 패닉을 일으킨다. */
void *
palloc_get_page (enum palloc_flags flags) {
	return palloc_get_multiple (flags, 1);
}

/* PAGES에서 시작하는 PAGE_CNT개의 페이지를 해제한다. */
void
palloc_free_multiple (void *pages, size_t page_cnt) {
	struct pool *pool;
	size_t page_idx;

	ASSERT (pg_ofs (pages) == 0);
	if (pages == NULL || page_cnt == 0)
		return;

	if (page_from_pool (&kernel_pool, pages))
		pool = &kernel_pool;
	else if (page_from_pool (&user_pool, pages))
		pool = &user_pool;
	else
		NOT_REACHED ();

	page_idx = pg_no (pages) - pg_no (pool->base);

#ifndef NDEBUG
	memset (pages, 0xcc, PGSIZE * page_cnt);
#endif
	ASSERT (bitmap_all (pool->used_map, page_idx, page_cnt));
	bitmap_set_multiple (pool->used_map, page_idx, page_cnt, false);
}

/* PAGE 위치의 페이지를 해제한다. */
void
palloc_free_page (void *page) {
	palloc_free_multiple (page, 1);
}

/* START에서 시작하고 END에서 끝나는 풀 P를 초기화한다. */
static void
init_pool (struct pool *p, void **bm_base, uint64_t start, uint64_t end) {
    /* 풀의 used_map을 풀의 기준 주소에 둔다.
       비트맵에 필요한 공간을 계산하고 풀 크기에서 뺀다. */
	uint64_t pgcnt = (end - start) / PGSIZE;
	size_t bm_pages = DIV_ROUND_UP (bitmap_buf_size (pgcnt), PGSIZE) * PGSIZE;

	lock_init(&p->lock);
	p->used_map = bitmap_create_in_buf (pgcnt, *bm_base, bm_pages);
	p->base = (void *) start;

	// 모두 사용 불가능한 상태로 표시한다.
	bitmap_set_all(p->used_map, true);

	*bm_base += bm_pages;
}

/* PAGE가 POOL에서 할당되었으면 true를 반환하고,
   그렇지 않으면 false를 반환한다. */
static bool
page_from_pool (const struct pool *pool, void *page) {
	size_t page_no = pg_no (page);
	size_t start_page = pg_no (pool->base);
	size_t end_page = start_page + bitmap_size (pool->used_map);
	return page_no >= start_page && page_no < end_page;
}
