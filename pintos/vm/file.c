/* file.c: 메모리 기반 파일 객체(mmap된 객체)의 구현. */

#include "vm/vm.h"
#include "vm/lazy_load.h"


/**
 * @author iamnuked
 * @date 2026-05-16
 * 헤더 파일 추가
 * page aligned 체크 메크로 함수 추가
 * load_lazy_file 함수 선언 추가
 */
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "threads/mmu.h"
#define is_pg_aligned(va) (pg_ofs(va) == 0)

bool lazy_load_file (struct page *page, void *aux);

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
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	ASSERT(VM_TYPE(type) == VM_FILE);

	/* 핸들러를 설정합니다 */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;

	/* SONNY'S CODE */

	return true;
	/* SONNY'S CODE */
}

/* 파일에서 내용을 읽어 페이지를 스왑 인합니다. */
/**
 * @brief file_page의 file에서 데이터를 읽어서 frame에 올리기
 * 
 * @param page 
 * @param kva 
 * @return true 
 * @return false 
 * @author iamnuked
 * @date 2026-05-18
 */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	ASSERT(page != NULL);
	ASSERT(kva != NULL);
	ASSERT(page->operations->type == VM_FILE)

	struct file_page *file_page UNUSED = &page->file;
	

	off_t read = file_read_at(file_page->file, kva, file_page->page_read_bytes, file_page->ofs);
	if (read != (off_t)file_page->page_read_bytes) {
		return false;
	}
	memset(kva + file_page->page_read_bytes, 0, file_page->page_zero_bytes);
	
	return true;
}

/* 내용을 파일에 다시 써서 페이지를 스왑 아웃합니다. */
/**
 * @brief 내용이 변경된 페이지인 경우 파일 저장 후 스왑 아웃
 * 
 * @param page 
 * @return true 
 * @return false 
 * @author iamnuked
 * @date 2026-05-18
 */
static bool
file_backed_swap_out (struct page *page) {
	ASSERT(page != NULL);
	ASSERT(page->operations->type == VM_FILE);
	off_t bytes;
	struct file_page *file_page = &page->file;

	/* dirty bit 확인 전 NULL 체크 */
	if(page->owner == NULL || page->owner->pml4 == NULL || page->frame == NULL || 
		page->frame->kva == NULL || file_page->file == NULL) {
		return false;
	}
	if (pml4_is_dirty(page->owner->pml4, page->va)) {
		bytes = file_write_at(file_page->file, page->frame->kva, (off_t)file_page->page_read_bytes, file_page->ofs);
	
		/* 써야할 bytes만큼 못 썼으면 false 반환 */
		if (bytes != (off_t) file_page->page_read_bytes) {
			return false;
		}
		pml4_set_dirty(page->owner->pml4, page->va, false);	
	}

	return true;
}

/* 파일 기반 페이지를 제거합니다. PAGE는 호출자가 해제합니다. */
/**
 * @brief 파일 페이지 리소스 해제
 * 페이지 구조체를 명시적으로 해제할 필요는 없습니다. 이는 호출자가 수행해야 합니다.
 * vm_dealloc_page에서 호출
 * vm_dealloc_page는 spt_remove_page, spt_destroy_func 에서 호출
 * 
 * 닫기 하기 전에 dirty인 페이지는 파일에 저장해야됨.
 * -> 정리 작업은 munmap or destroy?
 * -> munmap에서 destroy 호출하는 방식
 * 
 * @param page 
 * @author iamnuked
 * @date 2026-05-19
 */
static void
file_backed_destroy (struct page *page) {
	ASSERT(page != NULL);
	ASSERT(page->operations->type == VM_FILE);
	// off_t bytes;
	struct file_page *file_page UNUSED = &page->file;
	// /* 뭘 정리하지 -> 저장 후 파일 닫기? */
	
	// /* dirty bit 확인 전 NULL 체크 */
	// if(page->owner == NULL || page->owner->pml4 == NULL || page->frame == NULL || 
	// 	page->frame->kva == NULL || file_page->file == NULL) {
	// 	thread_current ()->exit_status = -1;
	// 	return thread_exit ();
	// }
	// if (pml4_is_dirty(page->owner->pml4, page->va)) {
	// 	bytes = file_write_at(file_page->file, page->frame->kva, (off_t)file_page->page_read_bytes, file_page->ofs);
	
	// 	/* 써야할 bytes만큼 못 썼으면 false 반환 */
	// 	if (bytes != (off_t) file_page->page_read_bytes) {
	// 		thread_current ()->exit_status = -1;
	// 		return thread_exit ();
	// 	}
	// }
	// spt, pte 해제 작업 필요
	// spt_remove_page (&page->owner->spt, page);

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
	
	struct supplemental_page_table *spt = &(thread_current()->spt);

	/* 인자 검증 */
	
	/* length 가 0인지 확인*/
	if (length == 0) {
		return NULL;
	}

	/* 정상 파일인지 체크 */
	if (file == NULL) {
		return NULL;
	}

	off_t file_size = file_length(file);

	/* 파일 크기가 offset보다 큰지 확인 */
	if (file_size <= offset) {
		return NULL;
	}

	/* addr 정상수 주소 확인 */
	if (addr == NULL || !is_pg_aligned(addr)) {
		return NULL;
	}

	uint8_t *start = addr;
	uint8_t *end = start + length;

	if (!is_user_vaddr(start) || !is_user_vaddr(end - 1)) {
		return NULL;
	}

	/* 스택 영역 침범하는지 확인 */
	if (start < (uint8_t *) USER_STACK && end > (uint8_t *) USER_STACK - STACK_MAX) {
    	return NULL;
	}

	/* 읽는 파일 위치가 Page 시작 위치에 맞도록 맞춰야 함 */
	if (offset % PGSIZE != 0) {
		return NULL;
	}

	

	/* 이미 사용중인 페이지인지 체크 */
	for (uint8_t* curr_addr = start; curr_addr < end; curr_addr = curr_addr + PGSIZE) {
		if (spt_find_page(spt, curr_addr) != NULL) {
			return NULL;
		}
	}

	uint32_t read_bytes = file_size - offset;

	/* 읽은 파일이 할당할 길이보다 길 경우 초과된 부분 잘라내기 */
	if (read_bytes > length) {
		read_bytes = length;
	}

	/* SPT에 페이지 정보 등록 */
	for (uint8_t* curr_addr = start; curr_addr < end; curr_addr = curr_addr + PGSIZE) {
		struct page* page = spt_find_page(&thread_current()->spt, (void*)curr_addr);

		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		struct lazy_load_info *aux = malloc(sizeof *aux);
		if(aux == NULL) {
			return NULL;
		}
		

		aux->file = file_reopen(file);
		if (aux->file == NULL) {
			free(aux);
			return NULL;
		}
		aux->ofs = offset;
		aux->page_read_bytes = page_read_bytes;
		aux->page_zero_bytes = page_zero_bytes;
		aux->start = start;
		aux->end = end;

		if(!vm_alloc_page_with_initializer (VM_FILE, curr_addr, writable, lazy_load_file, aux)) {
			file_close(aux->file);
			free(aux);
			return NULL;
		}
		read_bytes -= page_read_bytes;
		offset += page_read_bytes;
	}

	return addr;
}

/* munmap을 수행합니다 */
/**
 * @brief munmap 함수
 * 
 * @param addr 
 * @author iamnuked
 * @date 2026-05-18
 */
void
do_munmap (void *addr) {
	if (addr == NULL || !is_pg_aligned(addr) || !is_user_vaddr(addr)) {
		thread_current ()->exit_status = -1;
		thread_exit();
	}

	struct page *page = spt_find_page(&(thread_current()->spt), addr);

	if (page == NULL) {
		thread_current ()->exit_status = -1;
		thread_exit();
	}
	void* curr_addr = page->file.start;
	void* end_addr = page->file.end;
	while (curr_addr < end_addr) {
		// file_backed_destroy(page);
		page = spt_find_page(&(thread_current()->spt), curr_addr);
		spt_remove_page(&thread_current()->spt, page);
		curr_addr = curr_addr + PGSIZE;
	}

}


/**
 * @brief do_mmap 에서 vm_alloc_page_with_initializer 4번째 인자 함수
 * 
 * @author iamnuked
 * @date 2026-05-17
 */
bool
lazy_load_file (struct page *page, void *aux) {
	struct lazy_load_info *load_info = aux;
	uint8_t *kva = page->frame->kva;

	off_t read = file_read_at(load_info->file, kva, load_info->page_read_bytes, load_info->ofs);

	if(read != (off_t) load_info->page_read_bytes) {
		file_close(load_info->file);
		free(load_info);
		return false;
	}

	/* 읽지 않은 남은 부분 0으로 채우기 */
	memset(kva + load_info->page_read_bytes, 0, load_info->page_zero_bytes);
	struct file_page *file_page = &page->file;
	
	file_page->file = load_info->file;
	file_page->ofs = load_info->ofs;
	file_page->page_read_bytes = load_info->page_read_bytes;
	file_page->page_zero_bytes = load_info->page_zero_bytes;
	file_page->start = load_info->start;
	file_page->end = load_info->end;

	free(load_info);
	
	return true;
}