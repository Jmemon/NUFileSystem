
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

	// =================== Get Start and End Blocks ===================
	int start_idx = 0;
	while (offset >= start_idx * PAGE_SIZE)
		start_idx++;
	start_idx -= 1; // block where read starts (zero-indexed)

	int end_idx = 0;
	while (offset + size >= end_idx * PAGE_SIZE)
		end_idx++;
	end_idx -= 1; // block where read ends (zero-indexed)
	// ================================================================

	void* data = NULL;
	int* ipgs = NULL;

	int blk = -1;
	size_t sz = 0;
	size_t total_read = 0;
	for (int i = start_idx; i <= end_idx; i++) {

		if (i == 0 || i == 1)
			blk = node->ptrs[i];
		else {
			ipgs = (int*)pages_get_page(node->iptr);
			blk = ipgs[i - 2];
		}

		data = pages_get_page(blk);

		if (i == start_idx && i == end_idx) {
			sz = size;
			memcpy(buf + total_read, data + (offset - start_idx * PAGE_SIZE), sz);
		}
		else if (i == start_idx) {
			sz = (1 + start_idx) * PAGE_SIZE - offset;
			memcpy(buf + total_read, data + (offset - start_idx * PAGE_SIZE), sz);
		}
		else if (i == end_idx) {
			sz = size + offset - end_idx * PAGE_SIZE;
			memcpy(buf + total_read, data, sz);
		}
		else {
			sz = PAGE_SIZE;
			memcpy(buf + total_read, data, sz);
		}

		total_read += sz;
	}
	if (total_read != size) {
		printf("storage_read: req read size %d; actual read size %d\n", size, total_read);
		size = total_read;
	}

    return size;
}

int
storage_write(const char* path, const char* buf, size_t size, off_t offset)
{
	int* ipgs = NULL;
	void* data = NULL;

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
        grow_inode(node, offset + size);

	// =================== Get Start and End Blocks ===================
	int start_idx = 0;
	while (offset >= start_idx * PAGE_SIZE)
		start_idx++;
	start_idx -= 1; // block where write starts (zero-indexed)

	int end_idx = 0;
	while (offset + size >= end_idx * PAGE_SIZE)
		end_idx++;
	end_idx -= 1; // block where write ends (zero-indexed)
	// ================================================================

	printf("start_idx: %d\n", start_idx);
	printf("end_idx: %d\n\n", end_idx);

	int blk = -1;
	int sz = 0;
	int total_write = 0;
	for (int i = start_idx; i <= end_idx; i++) {

		if (i == 0 || i == 1)
			blk = node->ptrs[i];
		else {
			ipgs = (int*)pages_get_page(node->iptr);
			blk = ipgs[i - 2];
		}

		printf("blk : %d\n", blk);

		data = pages_get_page(blk);

		if (i == start_idx && i == end_idx) {
			sz = size;
			memcpy(data + (offset - start_idx * PAGE_SIZE), buf + total_write, sz);
		}
		else if (i == start_idx) {
			sz = (1 + start_idx) * PAGE_SIZE - offset;
			memcpy(data + (offset - start_idx * PAGE_SIZE), buf + total_write, sz);
		}
		else if (i == end_idx) {
			sz = size + offset - end_idx * PAGE_SIZE;
			memcpy(data, buf + total_write, sz);
		}
		else {
			sz = PAGE_SIZE;
			memcpy(data, buf + total_write, sz);
		}

		total_write += sz;
		printf("sz  : %d\n", sz);
		printf("total_write: %d\n\n", total_write);
	}

	if (total_write != size) {
		printf("storage_write: req write size %d; actual write size %d\n", size, total_write);
		size = total_write;
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
	char* dir_path = alloca(strlen(path) + 1);
	strlcpy(dir_path, path, strlen(path) + 1);

	char* name = dir_path + strlen(path) + 1;

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
	if (S_ISDIR(mode))
		node->refs = 2;
	else if (S_ISREG(mode))
		node->refs = 1;
	node->ptrs[0] = -1;
	node->ptrs[1] = -1;
	node->iptr = -1;

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
	int inum = tree_lookup(path);
	inode* node = get_inode(inum);
	node->refs -= 1;
	if (node->refs == 0)
		free_inode(inum);
    return directory_delete(path);
}

int
storage_link(const char* from, const char* to)
{
	int rv = 0;
	int inum = tree_lookup(from);

	int plen = strlen(to);
	char* dir_path = alloca(plen + 1);
	strlcpy(dir_path, to, plen + 1);

	char* name = dir_path + plen + 1;

	while (name[0] != '/')
		name--;

	name[0] = '\0';
	name++;
	
	rv = directory_put(dir_path, name, inum);

	inode* node = get_inode(inum);
	node->refs += 1;

    return rv;
}

int
storage_rename(const char* from, const char* to)
{
	int inum = tree_lookup(from);
	directory_delete(from);

	int plen = strlen(to);
	char* dir_path = alloca(plen + 1);
	strlcpy(dir_path, to, plen + 1);

	char* name = dir_path + plen + 1;

	while (name[0] != '/')
		name--;

	name[0] = '\0';
	name++;

	directory_put(dir_path, name, inum);

    return 0;
}

int
storage_set_time(const char* path, const struct timespec ts[2])
{
	int inum = tree_lookup(path);
	inode* node = get_inode(inum);

	node->acc = ts[0].tv_sec;
	node->mod = ts[1].tv_sec;

    return 0;
}
