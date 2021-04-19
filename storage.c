
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
	void* data = NULL;
	int* ipgs = NULL;

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
	int start_idx = 1;
	while ((start_idx - 1) * PAGE_SIZE < offset && offset <= start_idx * PAGE_SIZE)
		start_idx++;
	start_idx -= 2; // block where read starts (zero-indexed)

	int end_idx = 1;
	while ((end_idx - 1) * PAGE_SIZE < (offset + size) && (offset + size) <= end_idx * PAGE_SIZE)
		end_idx++;
	end_idx -= 2; // block where read ends (zero-indexed)
	// ================================================================

	int blk = -1;
	for (int i = start_idx; i <= end_idx; i++) {

		if (i == 0 || i == 1)
			blk = node->ptrs[i];
		else {
			ipgs = (int*)pages_get_page(node->iptr);
			blk = ipgs[i - 2];
		}

		data = pages_get_page(blk);

		if (i == start_idx && i == end_idx)
			memcpy(buf, data + (offset - start_idx * PAGE_SIZE), size);
		else if (i == start_idx)
			memcpy(buf, data + (offset - start_idx * PAGE_SIZE), (1 + start_idx) * PAGE_SIZE - offset);
		else if (i == end_idx)
			memcpy(buf, data, size - end_idx * PAGE_SIZE + offset);
		else
			memcpy(buf, data, PAGE_SIZE);
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
        size = node->size - offset;

	// =================== Get Start and End Blocks ===================
	int start_idx = 1;
	while ((start_idx - 1) * PAGE_SIZE < offset && offset <= start_idx * PAGE_SIZE)
		start_idx++;
	start_idx -= 2; // block where write starts (zero-indexed)

	int end_idx = 1;
	while ((end_idx - 1) * PAGE_SIZE < (offset + size) && (offset + size) <= end_idx * PAGE_SIZE)
		end_idx++;
	end_idx -= 2; // block where write ends (zero-indexed)
	// ================================================================

	int blk = -1;
	int num_pages = end_idx - start_idx + 1;
	for (int i = start_idx; i <= end_idx; i++) {

		if (i == 0 || i == 1)
			blk = node->ptrs[i];
		else {
			ipgs = (int*)pages_get_page(node->iptr);
			blk = ipgs[i - 2];
		}

		data = pages_get_page(blk);

		if (i == start_idx && i == end_idx)
			memcpy(data + (offset - start_idx * PAGE_SIZE), buf, size);
		else if (i == start_idx)
			memcpy(data + (offset - start_idx * PAGE_SIZE), buf, (1 + start_idx) * PAGE_SIZE - offset);
		else if (i == end_idx)
			memcpy(data, buf, size - end_idx * PAGE_SIZE + offset);
		else
			memcpy(data, buf, PAGE_SIZE);
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
	// TODO: Include case where need to create some parent directories

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
    // Maybe we need space in a pnode for timestamps.
    return 0;
}
