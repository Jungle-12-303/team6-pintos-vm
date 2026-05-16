/* file.c: 메모리 기반 파일 객체(mmap된 객체)의 구현. */

#include "vm/vm.h"


/**
 * @author iamnuked
 * @date 2026-05-16
 * 헤더 파일 추가
 * page aligned 체크 메크로 함수 추가
 */
#include "threads/vaddr.h"
#define is_pg_aligned(va) (pg_ofs(va) == 0)
/* SONNY'S CODE */


static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* 이 구조체를 수정하지 마세요 */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* 파일 VM의 초기화 함수 */
void
vm_file_init (void) {
}

/* 파일 기반 페이지를 초기화합니다 */
/**
 * @brief 
 * 
 * @param page 
 * @param type 
 * @param kva 
 * @return true 
 * @return false 
 * @author iamnuked
 * @date 2026-05-16
 */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) { // vm_type 매개변수는 왜 필요하지?
	/* 핸들러를 설정합니다 */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;

	/* SONNY'S CODE */



	/* SONNY'S CODE */
}

/* 파일에서 내용을 읽어 페이지를 스왑 인합니다. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;	


}

/* 내용을 파일에 다시 써서 페이지를 스왑 아웃합니다. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* 파일 기반 페이지를 제거합니다. PAGE는 호출자가 해제합니다. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* mmap을 수행합니다 */
/**
 * @brief 인자 검증
 * 1. addr 정상 주소 확인
 * 2. file 정상인지 확인 (NULL 체크)
 * 3. 파일 길이 확인
 * 
 * @param addr 
 * @param length 
 * @param writable 
 * @param file 
 * @param offset 
 * @return void* 
 * @author iamnuked
 * @date 2026-05-16
 */
void *
do_mmap (void *addr, size_t length, int writable, struct file *file, off_t offset) {
	/* 인자 검증 */
	struct supplemental_page_table *spt = &(thread_current()->spt);

	/* addr 정상 주소 확인 */
	if (addr == NULL ||  !is_pg_aligned(addr) || !is_user_vaddr(addr)) {
		return NULL;
	}

	/* length 가 0인지 확인*/
	if (length == 0) {
		return NULL;
	}

	/* 이미 사용중인 페이지인지 체크 */
	if (spt_find_page(spt, addr) != NULL) {
		return NULL;
	}

	/* 정상 파일인지 체크 */
	if (file == NULL) {
		return NULL;
	}
	
	if (vm_alloc_page_with_initializer (VM_FILE, addr, writable, file_backed_initializer, NULL)) {
		return addr;
	}

	return NULL;
}

/* munmap을 수행합니다 */
void
do_munmap (void *addr) {
}
