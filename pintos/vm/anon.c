/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include <bitmap.h>
#include "threads/synch.h"
#include "threads/vaddr.h"
#include <string.h>

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);
/* 추가한 정적전역 변수 */
static struct bitmap *swap_table;
static struct lock swap_lock;



/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
/**
 * @brief 익명 페이지 시스템 전체에서 공유할 스왑 자원을 초기화 하는 함수
 * 
 * @author hojun-lee99
 * @date 2026-05-16
 */
void
vm_anon_init (void) {
	// disk swap 영역 획득
	swap_disk = NULL;
	swap_disk = disk_get(1, 1);
	ASSERT(swap_disk != NULL);

	// 한 페이지에 필요한 디스크 섹터 수, swap disk에 저장 가능한 페이지 수
	size_t sector_per_page = PGSIZE / DISK_SECTOR_SIZE;
	size_t swap_slot_count = disk_size(swap_disk) / sector_per_page;

	// swap table 생성 및 swap table lock 초기화
	swap_table = bitmap_create(swap_slot_count);
	ASSERT(swap_table != NULL);
	
	lock_init(&swap_lock);
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	ASSERT(VM_TYPE(type) == VM_ANON);
	/* Set up the handler */
	page->operations = &anon_ops;
	page->anon.swap_slot = SWAP_SLOT_NONE;
	return true;
}

/* Swap in the page by read contents from the swap disk. */
/**
 * @brief swap disk에 있는 페이지 데이터를 물리 메모리에 올리는 작업을 진행합니다.
 * 
 * @param page 
 * @param kva 
 * @return true 
 * @return false 
 * @author hojun-lee99
 * @date 2026-05-17
 */
static bool
anon_swap_in (struct page *page, void *kva) {
	/*
	 * 1. paeg->anon.swap_slot == SWAP_SLOT_NONE이면 swap disk에 데이터가 없는 새 anonymous page
	 *    memset(kva, 0, PGSIZE) 후 true
	 * 2. slot이 있으면 해당 slot의 8개 sector를 disk_read()로 kva에 복원
	 * 3. 복원 후 bitmap에서 slot을 free 처리
	 * 4. page->annon.swap_slot = SWAP_SLOT_NONE
	 * 5. return true
	 */
	ASSERT(page != NULL);
	ASSERT(kva != NULL);
	ASSERT(page->operations->type == VM_ANON);

	if (page->anon.swap_slot == SWAP_SLOT_NONE) {
		memset(kva, 0, PGSIZE);
		return true;
	}

	size_t slot = page->anon.swap_slot;
	size_t sectors_per_page = PGSIZE / DISK_SECTOR_SIZE;
	uint8_t *dst = kva;

	for (size_t i = 0; i < sectors_per_page; i++) {
		disk_read(swap_disk, slot * sectors_per_page + i, dst + (i * DISK_SECTOR_SIZE));
	}

	lock_acquire(&swap_lock);
	bitmap_flip(swap_table, slot);
	lock_release(&swap_lock);

	page->anon.swap_slot = SWAP_SLOT_NONE;
	
	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
}
