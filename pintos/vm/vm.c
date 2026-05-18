/* vm.c: 가상 메모리 객체를 위한 일반 인터페이스. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "lib/kernel/hash.h"
#include <string.h>
#include "include/userprog/process.h"
#include "vm/lazy_load.h"


/* SONNY'S CODE */
// KENL_BASS를 사용하기 위한 import
#include "../include/threads/vaddr.h"
#include "../threads/mmu.h"
/* SONNY'S CODE */

/**
 * @brief lock init을 위한 synch.h, list init을 위한 list.h include
 * @author 임가인
 * @date 2026-05-16
 */
#include "./threads/synch.h"
#include "lib/kernel/list.h"

/**
 * @brief frame table에서 frame제거를 위한 vm_free_frame 함수 선언
 * 
 * @param frame 
 * @author ummfieg
 * @date 2026-05-17
 */
static void vm_free_frame (struct frame *frame);


#define STACK_MAX (1 << 20) // stack 최대값 1MB

/* 각 하위 시스템의 초기화 코드를 호출하여 가상 메모리 하위 시스템을 초기화한다. */
static uint64_t page_hash_func (const struct hash_elem *e, void *aux);
static bool page_less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux);
static void spt_destroy_func(struct hash_elem *e, void *aux UNUSED);
static bool is_stack_growth(void *addr, void *rsp);
bool vm_claim_or_grow_page(void *addr, void *rsp);

/**
 * @brief frame들을 관리하는 전역 frame_table
 * 
 * @author 임가인
 * @date 2026-05-16
 */
static struct list frame_table;

/**
 * @brief frame_table을 동시에 건드리는 상황을 막기 위한 lock
 * 
 * @author 임가인
 * @date 2026-05-16
 */
static struct lock frame_lock;


/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* 프로젝트 4용 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* 위 줄들은 수정하지 마세요. */
	/* TODO: 여기에 코드를 작성하세요. */

	/* frame_table, frame_lock 초기화 */
	list_init(&frame_table);
	lock_init(&frame_lock);
}

/* 페이지의 타입을 가져온다. 이 함수는 페이지가 초기화된 뒤의 타입을 알고 싶을 때 유용하다.
 * 이 함수는 현재 완전히 구현되어 있다. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* 도우미 함수 */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, de it dio not creatrectly and make it through this function or
 * `vm_alloc_page`. */
/* vm_initializer : struct page *, void *를 인자로 받고 bool을 반환하는 함수 타입 */
/* TODO load_segment 함수를 보고 aux 처리에 대한 로직 추가 필요*/
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable, vm_initializer *init, void *aux) {

	/* 이 함수에 VM_UNINIT 타입을 직접 넘기면 안됨 */
	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;
	struct page *new_page;

	/* 이미 등록되어있는 페이지인지 확인 */
	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {

		/* page를 생성하고, VM type에 맞는 initializer를 가져온 다음,
		   uninit_new를 호출해서 "uninit" page 구조체를 만든다.
		   uninit_new를 호출한 뒤 필요한 필드를 수정해야 한다. (writable)
		 */
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */

		/* initializer 변수는 bool 반환 struct page *, enum vm_type, void *를 받는 함수를 가리킴 */
		bool (*initializer)(struct page *, enum vm_type, void *);

		/* fault 때 사용할 page_initializer를 분기 */
		switch (VM_TYPE(type)) {
		case VM_ANON:
			initializer = anon_initializer;
			break;
		
		case VM_FILE:
			initializer = file_backed_initializer;
			break;

		default:
			return false;
		}

		new_page = malloc(sizeof *new_page);
		if(new_page == NULL) {
			return false;
		}

		/* page를 현재 VM_UNINIT 상태로 세팅하고,
		   fault 때 사용할 정보를 저장한다. */
		uninit_new (new_page, upage, init, type, aux, initializer);
		new_page->writable = writable;
		/**
		 * @brief 현재 thread를 page owner로 초기화 
		 * 
		 * @author 임가인
		 * @date 2026-05-17
		 */
		new_page->owner = thread_current();
		/* TODO: Insert the page into the spt. */

		if(!spt_insert_page (spt, new_page)) {
			free(new_page);
			return false;
		} return true;

	} 
	return false;
}

/* spt에서 VA를 찾아 페이지를 반환한다. 오류 시 NULL을 반환한다. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: 이 함수를 채운다. */
	struct page tmp;
	tmp.va = pg_round_down(va);

	struct hash_elem *find = hash_find(&spt->hash_table, &tmp.hash_elem);

	if(!find) {
		return NULL;
	}

	page = hash_entry(find, struct page, hash_elem);

	return page;
}

/* 검증 후 PAGE를 spt에 삽입한다. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {

	
	if (pg_ofs(page->va) != 0){
		return false;
	}

	if ((uintptr_t)page->va < PGSIZE || !is_user_vaddr(page->va) ) {
		return false;
	}

	return hash_insert(&spt->hash_table, &page->hash_elem) == NULL;
}

/* spt 테이블에서 페이지가 정확히 제거 됐는지 체크하고 page 할당을 해제 해준다.*/
void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	ASSERT (spt != NULL)
	ASSERT (page != NULL)
	ASSERT (hash_delete(&spt->hash_table, &page->hash_elem) != NULL)

	/**
	 * @brief page 해제 전 연결된 frame을 frame_table에서 제거
	 * 
	 * @author ummfieg
	 * @date 2026-05-18
	 */
	if (page->frame != NULL && page->owner != NULL && page->owner->pml4 != NULL) {
		pml4_clear_page (page->owner->pml4, page->va);
	}
	vm_free_frame(page->frame);
	vm_dealloc_page (page);
}

/* 축출될 struct frame을 가져온다. */
/**
 * @brief 내보낼 대상 frame을 탐색하는 함수 (정책은 second-chance 사용)
 * 
 * @author 임가인
 * @date 2026-05-16
 */
static struct frame *
vm_get_victim (void) {
	/* victim으로 고른 frame의 주소를 담아둘 포인터 변수 */
	struct frame *victim = NULL;
	 /* TODO: 축출 정책은 직접 정한다. */
	struct list_elem *e;

	lock_acquire (&frame_lock);

	while (victim == NULL) {
		bool has_candidate = false;

		for (e = list_begin (&frame_table);
			 e != list_end (&frame_table);
			 e = list_next (e)) {
			struct frame *frame = list_entry (e, struct frame, elem);

			if (frame->pinned || frame->page == NULL ||
					frame->page->owner == NULL ||
					frame->page->owner->pml4 == NULL) {
				continue;
			}

			/* pinned가 아니거나, page, owner, pml4가 있는 frame은 후보*/
			has_candidate = true;

			if (pml4_is_accessed (frame->page->owner->pml4,
						frame->page->va)) {
				pml4_set_accessed (frame->page->owner->pml4,
						frame->page->va, false);
				continue;
			}

			victim = frame;
			break;
		}

		if (!has_candidate) {
			break;
		}
	}

	lock_release (&frame_lock);
	return victim;
}

/* 페이지 하나를 축출하고 해당 프레임을 반환한다.
 * 오류 시 NULL을 반환한다. */
 /**
  * @brief victim 정책으로 선정된 frame swap out 후 page 매핑 끊고 비워진 frame 반환
  * 
  * @return struct frame* 
  * @author ummfieg
  * @date 2026-05-17
  */
static struct frame *
vm_evict_frame (void) {
	struct frame *victim = vm_get_victim ();
	/* TODO: victim을 스왑 아웃하고 축출된 프레임을 반환한다. */
	if(victim == NULL) {
		return NULL;
	} 

	struct page *old_page = victim->page;
	if(old_page == NULL || old_page->owner == NULL || old_page->owner->pml4 == NULL) {
		return NULL;
	}

	if(!swap_out(old_page)) {
		return NULL;
	}

	/* old_page의 매핑을 끊고 frame 연결 해제 */
	pml4_clear_page(old_page->owner->pml4, old_page->va);
	old_page->frame = NULL;
	victim->page = NULL;
		
	return victim;
}

/* palloc()을 호출하고 프레임을 가져온다.
 * 사용 가능한 페이지가 없으면 페이지를 축출하고 그 프레임을 반환한다.
 * 이 함수는 항상 유효한 주소를 반환한다.
 * 즉, 사용자 풀 메모리가 가득 차면 이 함수는 사용 가능한 메모리 공간을 얻기 위해 프레임을 축출한다. */

static struct frame *
vm_get_frame (void) {
	/* TODO: 이 함수를 채운다. */
	struct frame *frame = malloc(sizeof *frame);
	if (frame == NULL) {
		return NULL;
	}

	frame->kva = palloc_get_page(PAL_USER);

	/* 새 frame을 만들 수 없으면 기존 frame을 eviction으로 확보한다. */
	/**
	 * @brief palloc 실패시 eviction 함수 실행으로 frame 선정
	 * 
	 * @author 임가인
	 * @date 2026-05-17
	 */
	if (frame->kva == NULL) {
		free(frame);
		return vm_evict_frame();
	}

	/**
	 * @brief 생성된 frame field 세팅
	 * 
	 * @author 임가인
	 * @date 2026-05-16
	 */
	frame->page = NULL;
	frame->pinned = false;

	/**
	 * @brief 정상적으로 생성된 frame만(kva) 전역 frame_table에 넣고
	 *  추가 되는 동안 lock을 걸어 frame_table 리스트 삽입 중 동시 수정 방지
	 * 
	 * @author 임가인
	 * @date 2026-05-16
	 */
	lock_acquire(&frame_lock);
	list_push_back(&frame_table, &frame->elem);
	lock_release(&frame_lock);

	return frame;
}

/* 스택을 확장한다. */
static void
vm_stack_growth (void *addr UNUSED) {
/*
fault addr 검증
addr page boundary로 내림
SPT 등록
claim
pml4 매핑
*/
	void* p_addr = pg_round_down(addr);

	/* SONNY'S CODE */
	vm_alloc_page(VM_ANON | VM_MARKER_0, p_addr, 1); /* stack_growth인 경우 anon 타입, 쓰기 가능하도록 해야 함. */
	/* SONNY'S CODE */
}

/* 쓰기 보호된 페이지에서 발생한 fault를 처리한다. */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* 성공 시 true를 반환한다. */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
					 bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {

	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: fault를 검증한다. */
	/* TODO: 여기에 코드를 작성하세요. */
	
	/* SONNY'S CODE */
	// 이미 할당이 되었을 경우
	if (not_present == false) {
		return false;
	}

	// addr이 user 주소인지 확인
	if (!is_user_vaddr(addr)) {
		return false;
	}

	// write fault인데 read-only page면 거절
	page = spt_find_page(spt, addr);
	if (page != NULL && write && !page->writable ) {
		return false;
	}
	/* SONNY'S CODE */

	void *rsp = user ? (void *) f->rsp : thread_current()->user_rsp;
	return vm_claim_or_grow_page(addr, rsp);
	
}

/* 페이지를 해제한다.
 * 이 함수는 수정하지 마세요. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* VA에 할당된 페이지를 claim한다. */
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: 이 함수를 채운다. */
	
	/* SONNY'S CODE */
	struct supplemental_page_table *spt = &(thread_current()->spt);
	page = spt_find_page(spt, va);
	/* SONNY'S CODE */

	if(page == NULL)  {
		return false;
	}

	return vm_do_claim_page (page);
}

/* PAGE를 claim하고 mmu를 설정한다. */
static bool
vm_do_claim_page (struct page *page) {
	bool succ;
	if(page == NULL) {
		return false;
	}

	struct frame *frame = vm_get_frame ();

	if(frame == NULL) {
		return false;
	}

	/* 연결을 설정한다. */
	frame->page = page;
	page->frame = frame;

	/* TODO: 페이지의 VA를 프레임의 PA에 매핑하도록 페이지 테이블 엔트리를 삽입한다. */
	succ = pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->writable);
	if(!succ){

		/* page table 매핑 실패로 claim하지 못한 frame을 해제 */
		vm_free_frame (frame);
		return false;
	}
	
	if(!swap_in (page, frame->kva)) {
		/* swap_in 실패 시 생성했던 page table 매핑을 삭제하고 frame을 해제 */
		pml4_clear_page(thread_current()->pml4, page->va);
		vm_free_frame (frame);
		return false;
	}

	return true;
}

/* 새로운 supplemental page table을 초기화한다. */
bool
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	/* SONNY'S CODE */
	// 해시 초기화
	return hash_init(&spt->hash_table, page_hash_func, page_less_func, NULL);
	/* SONNY'S CODE */
}

/* supplemental page table을 src에서 dst로 복사한다. */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
	struct hash_iterator i;

	// iterator를 이용해 해시 테이블을 처음부터 순회
	hash_first (&i, &src->hash_table);
	while (hash_next (&i)) {
		struct page *src_page = hash_entry(hash_cur(&i), struct page, hash_elem);
		struct page *dst_page;

		if (spt_find_page (dst, src_page->va) != NULL)
			return false;

		dst_page = malloc (sizeof *dst_page);
		if (dst_page == NULL)
			return false;

		// 복사하려는 페이지 타입이 VM_UNINIT lazy 페이지이면 새로운 페이지 생성
		if (src_page->operations->type == VM_UNINIT) {
			struct lazy_load_info *new_aux = NULL;

			if(src_page->uninit.aux != NULL) {
				struct lazy_load_info *old_aux = src_page->uninit.aux;
				
				new_aux = malloc(sizeof *new_aux);
				if (new_aux == NULL) {
					free(dst_page);
					free(new_aux);
					return false;
				}
				
				*new_aux = *old_aux;
				new_aux->file = file_reopen(old_aux->file);
				if (new_aux->file == NULL) {
					free(new_aux);
					free(dst_page);
					return false;
				}
			}

			uninit_new (dst_page,
				src_page->va,
				src_page->uninit.init,
				src_page->uninit.type,
				new_aux,
				src_page->uninit.page_initializer);
			dst_page->writable = src_page->writable;
			/**
			 * @brief dst_page는 자식 thread의 SPT에 들어가는 pgae라 owner을 현재 thread로 초기화
			 * 
			 * @author 임가인
			 * @date 2026-05-17
			 */
			dst_page->owner = thread_current();
			
			if (!spt_insert_page (dst, dst_page)) {
				free (dst_page);
				return false;
			}
			continue;
		}

		//VM_UNINIT 타입이 아닌 경우
		enum vm_type type = page_get_type (src_page);
		bool (*initializer) (struct page *, enum vm_type, void *);

		switch (VM_TYPE (type)) {
		case VM_ANON:
			initializer = anon_initializer;
			break;
		case VM_FILE:
			initializer = file_backed_initializer;
			break;
		default:
			free (dst_page);
			return false;
		}

		if (src_page->frame == NULL) {
			free (dst_page);
			return false;
		}

		uninit_new (dst_page, src_page->va, NULL, type, NULL, initializer);
		dst_page->writable = src_page->writable;

		/**
		 * @brief dst_page는 자식 thread의 SPT에 들어가는 pgae라 owner을 현재 thread로 초기화
		 * 
		 * @author 임가인
		 * @date 2026-05-17
		 */
		dst_page->owner = thread_current();

		// 목적지 spt에 페이지 추가
		if (!spt_insert_page (dst, dst_page)) {
			free (dst_page);
			return false;
		}

		// 물리 메모리 추가
		src_page->frame->pinned = true;

		if (!vm_do_claim_page (dst_page)) {
			src_page->frame->pinned = false;
			spt_remove_page (dst, dst_page);
			return false;
		}

		dst_page->frame->pinned = true;

		memcpy (dst_page->frame->kva, src_page->frame->kva, PGSIZE);

		src_page->frame->pinned = false;
		dst_page->frame->pinned = false;
	}

	return true;
}

/* supplemental page table이 보유한 자원을 해제한다. */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: 스레드가 보유한 모든 supplemental_page_table을 제거하고,
	 * TODO: 수정된 모든 내용을 저장소에 다시 기록한다. */

	hash_destroy(&spt->hash_table, spt_destroy_func);
}

/* 새로 구현하는 함수 */

/* va를 인덱스로 사용하는 해시 함수*/
static uint64_t page_hash_func (const struct hash_elem *e, void *aux) {
	struct page *p = hash_entry(e, struct page, hash_elem);
	uint64_t hash = hash_bytes(&p->va, sizeof p->va);

	// printf ("[page_hash] va=%p hash=0x%llx\n", p->va, hash);

	return hash;
}

/* 해시 테이블에서 va를 기준으로 비교하는 함수*/
static bool page_less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux){
	struct page *p_a = hash_entry(a, struct page, hash_elem);
	struct page *p_b = hash_entry(b, struct page, hash_elem);
	
	return p_a->va < p_b->va;
}


static void spt_destroy_func(struct hash_elem *e, void *aux UNUSED) {
	struct page *page = hash_entry(e, struct page, hash_elem);

	/**
	 * @brief SPT 전체 정리 중 page에 연결된 frame도 함께 제거
	 * 
	 * @author ummfieg
	 * @date 2026-05-18
	 */
	
	if (page->frame != NULL && page->owner != NULL && page->owner->pml4 != NULL) {
		pml4_clear_page (page->owner->pml4, page->va);
	}
	vm_free_frame(page->frame);
	vm_dealloc_page(page);
}

static bool is_stack_growth(void *addr, void *rsp) {
	uint8_t *fault_addr = addr;
	uint8_t *stack_pointer = rsp;

	return stack_pointer != NULL
		&& fault_addr != NULL
		&& is_user_vaddr(fault_addr)
		&& fault_addr < (uint8_t *) USER_STACK
		&& fault_addr >= stack_pointer - 8
		&& (uint8_t *) USER_STACK - (uint8_t *) pg_round_down(fault_addr) <= STACK_MAX;
}

/* stack growth 여부 체크 후 page claim */
/* bool is_stack_growth(void *addr, void *rsp) */
/* vm_stack_growth (void *addr UNUSED) */
bool
vm_claim_or_grow_page(void *addr, void *rsp) {
    struct supplemental_page_table *spt = &thread_current()->spt;
    struct page *page = spt_find_page(spt, addr);
    void *kva = pml4_get_page(thread_current()->pml4, addr);

	/* plm4매핑이 있는지 여부 확인 */
    if (kva != NULL) {
        return true;
    }

	/* spt에 페이지가 있는지 확인 */
    if (page != NULL) {
        return vm_claim_page(addr);
    }

	/* stack growth 여부 확인 후 있으면 claim */
    if (is_stack_growth(addr, rsp)) {
        vm_stack_growth(addr);  
        return vm_claim_page(addr);
    }

    return false;
}

/**
 * @brief page-frame연결 및 frame table에 등록 된 frame을 제거하고 frame을 해제하는 helper함수
 * 
 * @param frame 
 * @author ummfieg
 * @date 2026-05-17
 */
static void
vm_free_frame (struct frame *frame) {
	if (frame == NULL) {
		return;
	}
		
	/* page-frame연결 해제 */
	if (frame->page != NULL) {
		frame->page->frame = NULL;
		frame->page = NULL;
	}

	/* remove하는 과정 동안 lock 실행 */
	lock_acquire (&frame_lock);
	list_remove (&frame->elem);
	lock_release (&frame_lock);

	/* frame 물리주소 및 구조체 free */
	if (frame->kva != NULL) {
		palloc_free_page (frame->kva);
	}

	free (frame);
}