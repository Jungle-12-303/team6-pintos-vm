/* vm.c: 가상 메모리 객체를 위한 일반 인터페이스. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "lib/kernel/hash.h"


/* SONNY'S CODE */
// KENL_BASS를 사용하기 위한 import
#include "../include/threads/vaddr.h"
/* SONNY'S CODE */

/* 각 하위 시스템의 초기화 코드를 호출하여 가상 메모리 하위 시스템을 초기화한다. */
static uint64_t page_hash_func (const struct hash_elem *e, void *aux);
static bool page_less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux);


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
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

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
	tmp.va = va;

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
	int succ = false;
	/* TODO: 이 함수를 채운다. */

	/* SONNY'S CODE */
	// 해시 테이블에 page 추가
	if (0x400000 <= page->va <= USER_STACK) { // 유저 영역일 때
		succ = true;
		hash_insert (&spt->hash_table, &page->hash_elem);
	}
	/* SONNY'S CODE */

	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* 축출될 struct frame을 가져온다. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: 축출 정책은 직접 정한다. */

	return victim;
}

/* 페이지 하나를 축출하고 해당 프레임을 반환한다.
 * 오류 시 NULL을 반환한다. */
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: victim을 스왑 아웃하고 축출된 프레임을 반환한다. */

	return NULL;
}

/* palloc()을 호출하고 프레임을 가져온다.
 * 사용 가능한 페이지가 없으면 페이지를 축출하고 그 프레임을 반환한다.
 * 이 함수는 항상 유효한 주소를 반환한다.
 * 즉, 사용자 풀 메모리가 가득 차면 이 함수는 사용 가능한 메모리 공간을 얻기 위해 프레임을 축출한다. */
static struct frame *
vm_get_frame (void) {
	struct frame *frame = NULL;
	/* TODO: 이 함수를 채운다. */

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* 스택을 확장한다. */
static void
vm_stack_growth (void *addr UNUSED) {
	
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
	if (not_present) { // 아직 할당x인 경우

	}
	else if (write | user) { // 할당은 되어있지만 권한 문제인 경우
		
	}

	
	/* SONNY'S CODE */

	return vm_do_claim_page (page);
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

	return vm_do_claim_page (page);
}

/* PAGE를 claim하고 mmu를 설정한다. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	/* 연결을 설정한다. */
	frame->page = page;
	page->frame = frame;

	/* TODO: 페이지의 VA를 프레임의 PA에 매핑하도록 페이지 테이블 엔트리를 삽입한다. */

	return swap_in (page, frame->kva);
}

/* 새로운 supplemental page table을 초기화한다. */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	/* SONNY'S CODE */
	// 해시 초기화
	if(!hash_init(&spt->hash_table, page_hash_func, page_less_func, NULL)) {
		PANIC("failed hash_init -SONNY-");
	}
	/* SONNY'S CODE */
}

/* supplemental page table을 src에서 dst로 복사한다. */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
}

/* supplemental page table이 보유한 자원을 해제한다. */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: 스레드가 보유한 모든 supplemental_page_table을 제거하고,
	 * TODO: 수정된 모든 내용을 저장소에 다시 기록한다. */
}

/* 새로 구현하는 함수 */

/* va를 인덱스로 사용하는 해시 함수*/
static uint64_t page_hash_func (const struct hash_elem *e, void *aux) {
	struct page *p = hash_entry(e, struct page, hash_elem);
	uint64_t hash = hash_bytes(&p->va, sizeof p->va);

	printf ("[page_hash] va=%p hash=0x%llx\n", p->va, hash);

	return hash;
}

/* 해시 테이블에서 va를 기준으로 비교하는 함수*/
static bool page_less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux){
	struct page *p_a = hash_entry(a, struct page, hash_elem);
	struct page *p_b = hash_entry(b, struct page, hash_elem);
	
	return p_a->va < p_b->va;
}