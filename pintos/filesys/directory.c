#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"

/* 디렉터리. */
struct dir {
	struct inode *inode;                /* 백업 저장소. */
	off_t pos;                          /* 현재 위치. */
};

/* 단일 디렉터리 항목. */
struct dir_entry {
	disk_sector_t inode_sector;         /* 헤더의 섹터 번호. */
	char name[NAME_MAX + 1];            /* 널 종료된 파일 이름. */
	bool in_use;                        /* 사용 중인가, 비어 있는가? */
};

/* 주어진 SECTOR에 ENTRY_CNT개의 항목을 저장할 공간이 있는
 * 디렉터리를 만든다. 성공하면 true, 실패하면 false를 반환한다. */
bool
dir_create (disk_sector_t sector, size_t entry_cnt) {
	return inode_create (sector, entry_cnt * sizeof (struct dir_entry));
}

/* 주어진 INODE에 대한 디렉터리를 열어 반환하며, 해당 INODE의
 * 소유권을 가져온다. 실패하면 널 포인터를 반환한다. */
struct dir *
dir_open (struct inode *inode) {
	struct dir *dir = calloc (1, sizeof *dir);
	if (inode != NULL && dir != NULL) {
		dir->inode = inode;
		dir->pos = 0;
		return dir;
	} else {
		inode_close (inode);
		free (dir);
		return NULL;
	}
}

/* 루트 디렉터리를 열고 그에 대한 디렉터리를 반환한다.
 * 성공하면 true, 실패하면 false를 반환한다. */
struct dir *
dir_open_root (void) {
	return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* DIR과 같은 inode에 대한 새 디렉터리를 열어 반환한다.
 * 실패하면 널 포인터를 반환한다. */
struct dir *
dir_reopen (struct dir *dir) {
	return dir_open (inode_reopen (dir->inode));
}

/* DIR을 제거하고 관련 리소스를 해제한다. */
void
dir_close (struct dir *dir) {
	if (dir != NULL) {
		inode_close (dir->inode);
		free (dir);
	}
}

/* DIR이 캡슐화한 inode를 반환한다. */
struct inode *
dir_get_inode (struct dir *dir) {
	return dir->inode;
}

/* DIR에서 주어진 NAME을 가진 파일을 찾는다.
 * 성공하면 true를 반환하고, EP가 널이 아니면 *EP를 디렉터리
 * 항목으로 설정하며, OFSP가 널이 아니면 *OFSP를 디렉터리 항목의
 * 바이트 오프셋으로 설정한다.
 * 그렇지 않으면 false를 반환하고 EP와 OFSP를 무시한다. */
static bool
lookup (const struct dir *dir, const char *name,
		struct dir_entry *ep, off_t *ofsp) {
	struct dir_entry e;
	size_t ofs;

	ASSERT (dir != NULL);
	ASSERT (name != NULL);

	for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
			ofs += sizeof e)
		if (e.in_use && !strcmp (name, e.name)) {
			if (ep != NULL)
				*ep = e;
			if (ofsp != NULL)
				*ofsp = ofs;
			return true;
		}
	return false;
}

/* DIR에서 주어진 NAME을 가진 파일을 찾고,
 * 존재하면 true, 그렇지 않으면 false를 반환한다.
 * 성공하면 *INODE를 해당 파일의 inode로 설정하고, 그렇지 않으면
 * 널 포인터로 설정한다. 호출자는 *INODE를 닫아야 한다. */
bool
dir_lookup (const struct dir *dir, const char *name,
		struct inode **inode) {
	struct dir_entry e;

	ASSERT (dir != NULL);
	ASSERT (name != NULL);

	if (lookup (dir, name, &e, NULL))
		*inode = inode_open (e.inode_sector);
	else
		*inode = NULL;

	return *inode != NULL;
}

/* NAME이라는 파일을 DIR에 추가한다. DIR에는 같은 이름의 파일이
 * 이미 없어야 한다. 파일의 inode는 INODE_SECTOR 섹터에 있다.
 * 성공하면 true, 실패하면 false를 반환한다.
 * NAME이 유효하지 않거나(즉, 너무 길거나) 디스크 또는 메모리
 * 오류가 발생하면 실패한다. */
bool
dir_add (struct dir *dir, const char *name, disk_sector_t inode_sector) {
	struct dir_entry e;
	off_t ofs;
	bool success = false;

	ASSERT (dir != NULL);
	ASSERT (name != NULL);

	/* NAME의 유효성을 검사한다. */
	if (*name == '\0' || strlen (name) > NAME_MAX)
		return false;

	/* NAME이 사용 중이 아닌지 검사한다. */
	if (lookup (dir, name, NULL, NULL))
		goto done;

	/* OFS를 빈 슬롯의 오프셋으로 설정한다.
	 * 빈 슬롯이 없으면 현재 파일 끝으로 설정된다.

	 * inode_read_at()은 파일 끝에서만 짧은 읽기를 반환한다.
	 * 그렇지 않다면 메모리 부족 같은 일시적인 원인으로 짧은 읽기가
	 * 발생하지 않았는지 확인해야 한다. */
	for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
			ofs += sizeof e)
		if (!e.in_use)
			break;

	/* 슬롯을 쓴다. */
	e.in_use = true;
	strlcpy (e.name, name, sizeof e.name);
	e.inode_sector = inode_sector;
	success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;

done:
	return success;
}

/* DIR에서 NAME에 해당하는 모든 항목을 제거한다.
 * 성공하면 true, 실패하면 false를 반환하며, 실패는 주어진 NAME을
 * 가진 파일이 없을 때만 발생한다. */
bool
dir_remove (struct dir *dir, const char *name) {
	struct dir_entry e;
	struct inode *inode = NULL;
	bool success = false;
	off_t ofs;

	ASSERT (dir != NULL);
	ASSERT (name != NULL);

	/* 디렉터리 항목을 찾는다. */
	if (!lookup (dir, name, &e, &ofs))
		goto done;

	/* inode를 연다. */
	inode = inode_open (e.inode_sector);
	if (inode == NULL)
		goto done;

	/* 디렉터리 항목을 지운다. */
	e.in_use = false;
	if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e)
		goto done;

	/* inode를 제거한다. */
	inode_remove (inode);
	success = true;

done:
	inode_close (inode);
	return success;
}

/* DIR에서 다음 디렉터리 항목을 읽고 이름을 NAME에 저장한다.
 * 성공하면 true를 반환하고, 디렉터리에 더 이상 항목이 없으면
 * false를 반환한다. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1]) {
	struct dir_entry e;

	while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) {
		dir->pos += sizeof e;
		if (e.in_use) {
			strlcpy (name, e.name, NAME_MAX + 1);
			return true;
		}
	}
	return false;
}
