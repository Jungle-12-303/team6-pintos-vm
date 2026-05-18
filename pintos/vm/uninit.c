/* uninit.c: 초기화되지 않은 페이지의 구현.
 *
 * 모든 페이지는 uninit 페이지로 생성된다.
 * 첫 번째 페이지 fault가 발생하면 핸들러 체인은 uninit_initialize (page->operations.swap_in)를 호출한다.
 * uninit_initialize 함수는 페이지 객체를 초기화하여 특정 페이지 객체 (anon, file, page_cache)로 변환하고,
 * vm_alloc_page_with_initializer 함수에서 전달된 초기화 콜백을 호출한다.
 */

#include "vm/vm.h"
#include "vm/uninit.h"
#include "threads/vaddr.h"
#include <string.h>
#include "threads/malloc.h"

static bool uninit_initialize (struct page *page, void *kva);
static void uninit_destroy (struct page *page);

/* 이 구조체는 수정하지 마세요. */
static const struct page_operations uninit_ops = {
	.swap_in = uninit_initialize,
	.swap_out = NULL,
	.destroy = uninit_destroy,
	.type = VM_UNINIT,
};

/* 이 함수는 수정하지 마세요. */
void
uninit_new (struct page *page, void *va, vm_initializer *init, enum vm_type type, void *aux,
			bool (*initializer)(struct page *, enum vm_type, void *)) {
	ASSERT (page != NULL);

	*page = (struct page) {
		.operations = &uninit_ops,
		.va = va,
		.frame = NULL, /* 현재는 프레임이 없다. */
		.uninit = (struct uninit_page) {
			.init = init,
			.type = type,
			.aux = aux,
			.page_initializer = initializer,
		}
	};
}

/**
 * @brief 첫 번째 fault에서 페이지를 초기화한다.
 * 
 * @param page 
 * @param kva 
 * @return true 
 * @return false 
 * @author hojun-lee99
 * @date 2026-05-16
 */
static bool
uninit_initialize (struct page *page, void *kva) {
	struct uninit_page *uninit = &page->uninit;

	/* page_initialize가 값을 덮어쓸 수 있으므로 먼저 가져온다. */
	vm_initializer *init = uninit->init;
	void *aux = uninit->aux;
	enum vm_type type = uninit->type;

	// page_initializer에 맞춰 페이지 타입 init
	// 각 페이지 page_initializer에서 false 반환되면
	if (!uninit->page_initializer (page, type, kva)) {
		return false;
	}

	// init이 있으면 init 호출해 페이지에 실제 내용을 채우고 반환
	if (init != NULL) {
		return init(page, aux);
	}

	/* init이 없고, ANON 타입이면 메모리를 0으로 초기화 */
	if (VM_TYPE(type) == VM_ANON) {
		memset(kva, 0, PGSIZE);
	}

	return true;
}

/**
 * @brief uninit_page가 보유한 자원을 해제한다.
 * 대부분의 페이지는 다른 페이지 객체로 변환되지만,
 * 실행 중 한 번도 참조되지 않아 프로세스 종료 시점까지 uninit 페이지로 남아 있을 수 있다.
 * PAGE는 호출자가 해제한다.
 * 
 * @param page 
 * @author hojun-lee99
 * @date 2026-05-18
 */
static void uninit_destroy (struct page *page) {
	struct uninit_page *uninit UNUSED = &page->uninit;
	
	// aux가 있을경우 해제
	if (uninit->aux != NULL) {
		free(uninit->aux);
		uninit->aux = NULL;
	}
}
