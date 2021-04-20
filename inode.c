
#include <stdint.h>
#include <errno.h>
#include <assert.h>

#include "pages.h"
#include "inode.h"
#include "util.h"

#include "globals.h"

extern const int PAGE_COUNT;
extern const int PAGE_SIZE;
extern void* pages_base;

extern int INODE_COUNT;
extern void* inode_bm_base;
extern inode* inode_base;

extern const int default_file_mode;

void
print_inode(inode* node)
{
    if (node) {
		printf("node{refs: %d, mode: %04o, size: %d, ptrs[0]: %d, ptrs[1]: %d, iptr: %d, acc: %ld, mod: %ld}\n", 
				node->refs, node->mode, node->size, node->ptrs[0], 
				node->ptrs[1], node->iptr, node->acc, node->mod);
    }
    else
        printf("node{null}\n");
}

void
init_inode_gvars()
{
	int rv = globals_pinit_check();
	if (rv == -1)
		return;

	INODE_COUNT = (8 * PAGE_SIZE - PAGE_COUNT) / (8 * sizeof(inode));
	inode_base = (inode*)((intptr_t)pages_base + PAGE_COUNT);
}

inode*
get_inode(int inum)
{
	int rv = globals_iinit_check();
	if (rv == -1)
		return (inode*)(intptr_t)rv;

	if (inum >= INODE_COUNT)
		return (inode*)(-1);

    return inode_base + inum;
}


int
alloc_inode()
{
	int rv = globals_iinit_check();
	if (rv == -1)
		return rv;

    for (int ii = 2; ii < INODE_COUNT; ++ii) {
        inode* node = get_inode(ii);
        if (node->refs == 0) {
            memset(node, 0, sizeof(inode));
			node->refs = 1;
            node->mode = default_file_mode;
			node->size = 0;
			node->ptrs[0] = -1;
			node->ptrs[1] = -1;
			node->iptr = -1;
			node->acc = -1;
			node->mod = -1;
            printf("+ alloc_inode() -> %d\n", ii);
            return ii;
        }
    }

    return -1;
}

int
grow_inode(inode* node, int size)
{
	int err = globals_pinit_check();
	if (err == -1) {
		printf("grow_inode: page gvars no initiatlized\n");
		return err;
	}

	if (size < 0) {
		printf("grow_inode: requested new size of %d, must be positive\n", size);
		return -EINVAL;
	}

	if (size == 0) {
		node->ptrs[0] = -1;
		node->ptrs[1] = -1;
		node->iptr = -1;
		node->size = 0;
		return size;
	}

	if (node->size > size) {
		printf("grow_inode: size < node->size; called shrink_inode\n");
		return shrink_inode(node, size);
	}

	int blks_needed = ((size - 1) / PAGE_SIZE) + 1;
	int blks_allocd = node->size == 0 ? 0 : ((node->size - 1) / PAGE_SIZE) + 1;

	int* ipgs = (int*)pages_get_page(node->iptr);
	int pnum = -1;

	while (blks_allocd < blks_needed) {
		pnum = alloc_page();
		if (pnum == -1)
			return -1;

		if (blks_allocd == 0 || blks_allocd == 1)
			node->ptrs[blks_allocd] = pnum;
		else
			ipgs[blks_allocd - 2] = pnum;
		
		blks_allocd += 1;
	}

	node->size = size;
	return size;
}

int
shrink_inode(inode* node, int size)
{
	int err = globals_pinit_check();
	if (err == -1) {
		printf("grow_inode: page gvars no initiatlized\n");
		return err;
	}

	if (size < 0) {
		printf("grow_inode: requested new size of %d, must be positive\n", size);
		return -EINVAL;
	}
	else if (size == 0) {
		node->ptrs[0] = -1;
		node->ptrs[1] = -1;
		node->iptr = -1;
		node->size = 0;
		return size;
	}

	if (node->size < size) {
		printf("shrink_inode: size > node->size; called grow_inode\n");
		return grow_inode(node, size);
	}

	int blks_needed = ((size - 1) / PAGE_SIZE) + 1;
	int blks_allocd = node->size == 0 ? 0 : ((node->size - 1) / PAGE_SIZE) + 1;

	int* ipgs = (int*)pages_get_page(node->iptr);
	int pnum = -1;

	while (blks_allocd > blks_needed) {

		if (blks_allocd == 0 || blks_allocd == 1) {
			free_page(node->ptrs[blks_allocd]);
			node->ptrs[blks_allocd] = pnum;
		}
		else {
			pnum = ipgs[blks_allocd - 2];
			free_page(pnum);
		}

		blks_allocd -= 1;
	}

	if (blks_allocd < 2) {
		free_page(node->iptr);
		node->iptr = -1;
	}

	node->size = size;
	return size;
}

void
free_inode(int inum)
{
    printf("+ free_inode(%d)\n", inum);

    inode* node = get_inode(inum);

	if (node->ptrs[0] != -1)
		free_page(node->ptrs[0]);	

	if (node->ptrs[1] != -1)
		free_page(node->ptrs[1]);

	if (node->iptr != -1)
		free_page(node->iptr);

    memset(node, 0, sizeof(inode));
}

