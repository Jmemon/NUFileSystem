
#define _GNU_SOURCE
#include <string.h>

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <assert.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>

#include "pages.h"
#include "bitmap.h"
#include "util.h"

#include "globals.h"


extern const int PAGE_COUNT;
extern const int PAGE_SIZE;
extern const int NUFS_SIZE;

extern int   pages_fd;
extern void* pages_base;

void
pages_init(const char* path)
{
	// Initialize memory
    pages_fd = open(path, O_CREAT | O_RDWR, 0644);
    assert(pages_fd != -1);

    int rv = ftruncate(pages_fd, NUFS_SIZE);
    assert(rv == 0);

	// get start of memory region
    pages_base = mmap(0, NUFS_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, pages_fd, 0);
    assert(pages_base != MAP_FAILED);

	// set region to zero
	memset(pages_base, 0, NUFS_SIZE);

	// check that page gvars have been properly initialized
	int err = globals_pinit_check();
	if (err == -1) {
		printf("pages_init: Page gvars failed to initialize\n");
		return;
	}

    // mark page 0 as taken
    void* pbm = pages_base;
	bitmap_put(pbm, 0, 1);
}


void
pages_free()
{
	int err = globals_pinit_check();
	if (err == -1) {
		printf("pages_free: Page gvars not initialized\n");
		return;
	}

    int rv = munmap(pages_base, NUFS_SIZE);
    assert(rv == 0);
}

void*
pages_get_page(int pnum)
{
	int err = globals_pinit_check();
	if (err == -1) {
		printf("pages_get_page: Page gvars not initialized\n");
		return (void*)(-1);
	}

    return pages_base + PAGE_SIZE * pnum;
}

void*
get_pages_bitmap()
{
	return pages_get_page(0);
}

int
alloc_page()
{
	void* pbm = get_pages_bitmap();

	for (int ii = 1; ii < PAGE_COUNT; ++ii) {
		if (!bitmap_get(pbm, ii)) {
			bitmap_put(pbm, ii, 1);
			printf("+ alloc_page() -> %d\n", ii);
			return ii;
		}
	}

	return -1;
}

void
free_page(int pnum)
{
	printf("+ free_page(%d)\n", pnum);
	void* pbm = get_pages_bitmap();
	bitmap_put(pbm, pnum, 0);
}
