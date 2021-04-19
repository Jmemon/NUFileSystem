
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <assert.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <alloca.h>
#include <string.h>
#include <libgen.h>
#include <bsd/string.h>
#include <stdint.h>

#include "storage.h"
#include "slist.h"
#include "util.h"
#include "pages.h"
#include "inode.h"
#include "directory.h"

#include "globals.h"

extern const int PAGE_SIZE;

extern const int default_file_mode;

void
storage_init(const char* path)
{
	// Initialize disk image
    pages_init(path);

	// Initialize Block 0 Layout
	init_inode_gvars();

	// Initialize Node for Block 0
	inode* node = get_inode(0);
	node->refs = 0;
	node->mode = default_file_mode;
	node->size = PAGE_SIZE;
	node->ptrs[0] = 0;
	node->ptrs[1] = -1;
	node->iptr = -1;

	// Set up Root
	directory_init();
}

int
storage_stat(const char* path, struct stat* st)
{
    printf("+ storage_stat(%s)\n", path);
    int inum = tree_lookup(path);
    if (inum < 0)
        return -ENOENT;

    inode* node = get_inode(inum);
    printf("+ storage_stat(%s); inode %d\n", path, inum);
    print_inode(node);

    memset(st, 0, sizeof(struct stat));
    st->st_uid   = getuid();
    st->st_mode  = node->mode;
    st->st_size  = node->size;
	st->st_ino   = inum;
    st->st_nlink = node->refs;
    return 0;
}

int
storage_read(const char* path, char* buf, size_t size, off_t offset)
{
    int inum = tree_lookup(path);
    if (inum < 0)
        return inum;

    inode* node = get_inode(inum);
    printf("+ storage_read(%s); inode %d\n", path, inum);
    print_inode(node);

    if (offset >= node->size)
        return 0;

    if (offset + size >= node->size)
        size = node->size - offset;

	if (offset + size <= PAGE_SIZE) {
		void* data = pages_get_page(node->ptrs[0]);
		printf(" + reading from page: %d\n", node->ptrs[0]);
		memcpy(buf, data + offset, size);
	}
	else if (PAGE_SIZE < offset + size <= 2 * PAGE_SIZE) {
		void* data = NULL;

		if (offset < PAGE_SIZE) {
			data = pages_get_page(node->ptrs[0]);
			printf(" + reading from page: %d\n", node->ptrs[0]);
			memcpy(buf, data + offset, PAGE_SIZE - offset);

			data = pages_get_page(node->ptrs[1]);
			printf(" + reading from page: %d\n", node->ptrs[1]);
			memcpy(buf + PAGE_SIZE - offset, data, size + offset - PAGE_SIZE);
		}
		else {
			data = pages_get_page(node->ptrs[1]);
			printf(" + reading from page: %d\n", node->ptrs[1]);
			memcpy(buf, data + offset - PAGE_SIZE, size);
		}
	}
	else {
		// TODO: Indirect Pointer
	}

    return size;
}

int
storage_write(const char* path, const char* buf, size_t size, off_t offset)
{
    int trv = storage_truncate(path, offset + size);
    if (trv < 0)
        return trv;

    int inum = tree_lookup(path);
    if (inum < 0)
        return inum;

    inode* node = get_inode(inum);
	printf(" + storage_write(%s); inode %d\n", path, inum);
	print_inode(node);

    if (offset >= node->size)
        return 0;

    if (offset + size >= node->size)
        size = node->size - offset;

	if (offset + size <= PAGE_SIZE) {
		void* data = pages_get_page(node->ptrs[0]);
		printf(" + writing to page: %d\n", node->ptrs[0]);
		memcpy(data + offset, (void*)buf, size);
	}
	else if (PAGE_SIZE < offset + size <= 2 * PAGE_SIZE) {
		void* data = NULL;

		if (offset < PAGE_SIZE) {
			data = pages_get_page(node->ptrs[0]);
			printf(" + writing to page: %d\n", node->ptrs[0]);
			memcpy(data + offset, (void*)buf, PAGE_SIZE - offset);

			data = pages_get_page(node->ptrs[1]);
			printf(" + reading from page: %d\n", node->ptrs[1]);
			memcpy(data, (void*)(buf) + PAGE_SIZE - offset, size + offset - PAGE_SIZE);
		}
		else {
			data = pages_get_page(node->ptrs[1]);
			printf(" + reading from page: %d\n", node->ptrs[1]);
			memcpy(data + offset - PAGE_SIZE, (void*)buf, size);
		}
	}
	else {
		// TODO: Indirect Pointer
	}

    return size;
}

int
storage_truncate(const char *path, off_t size)
{
    int inum = tree_lookup(path);
    if (inum < 0)
        return inum;

    inode* node = get_inode(inum);
    grow_inode(node, size);

    return 0;
}

int
storage_mknod(const char* path, int mode)
{
	char* dir_path = NULL;
	strlcpy(dir_path, path, strlen(path));

	char* name = dir_path + strlen(path);

	while (name[0] != '/')
		name--;

	name[0] = '\0';
	name++;

	int p_inum = tree_lookup(dir_path);
	if (p_inum == -ENOENT) {
		printf("storage_mknod: parent directory not found\n");
		return -ENOENT;
	}
	inode* p_node = get_inode(p_inum);

    if (directory_lookup(p_node, name) != -ENOENT) {
        printf("mknod fail: already exist\n");
        return -EEXIST;
    }

    int inum = alloc_inode();
    inode* node = get_inode(inum);
    node->mode = mode;
    node->size = 0;

    printf("+ mknod create %s in %s [%04o] - #%d\n", name, dir_path, mode, inum);

    int rv = directory_put(dir_path, name, inum);
	
	return rv;
}

slist*
storage_list(const char* path)
{
	int inum = tree_lookup(path);
	inode* dd = get_inode(inum);
    return directory_list(dd);
}

int
storage_unlink(const char* path)
{
    const char* name = path + 1;
    return directory_delete(name);
}

int
storage_link(const char* from, const char* to)
{
    return -ENOENT;
}

int
storage_rename(const char* from, const char* to)
{
/*
    int inum = tree_lookup(from);
    if (inum < 0) {
        printf("mknod fail");
        return inum;
    }

    char* ent = directory_get(inum);
    strlcpy(ent, to + 1, 16);
*/
    return 0;
}

int
storage_set_time(const char* path, const struct timespec ts[2])
{
    // Maybe we need space in a pnode for timestamps.
    return 0;
}
