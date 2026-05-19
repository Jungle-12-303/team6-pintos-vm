#include <stddef.h>
#include "filesys/off_t.h"

struct file;

struct lazy_load_info {
	struct file *file; 		 /* 읽어올 실행파일 */
	off_t ofs; 				 /* page 데이터가 시작되는 파일 offset */
	size_t page_read_bytes;  /* pgae에 파일에서 읽어 넣을 byte 수  */
	size_t page_zero_bytes;  /* page에서 0으로 채울 byte 수 */
	void* start;			 /* do_munmap에서 연속된 페이지 시작 주소 */
	void* end;				 /* do_munmap에서 연속된 페이지 끝 주소(zero 영역 전) */
};