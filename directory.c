
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
#include <alloca.h>

#include "directory.h"
#include "pages.h"
#include "slist.h"
#include "util.h"
#include "inode.h"

#include "globals.h"

extern const int PAGE_SIZE;
extern void* pages_base;

extern const int default_dir_mode;

void
directory_init()
{
    inode* rn = get_inode(1);

    if (rn->mode == 0) {
		rn->refs = 2;
        rn->mode = default_dir_mode;
		rn->size = 0;
		rn->ptrs[0] = -1;
		rn->ptrs[1] = -1;
		rn->iptr = -1;
    }

	printf("Root Inode (Inum 1): \n");
	print_inode(rn);
}

dirent*
directory_get(inode* dd, const char* name)
{
	int num_entries = dd->size / sizeof(dirent);
	dirent* ent = (dirent*)pages_get_page(dd->ptrs[0]);
	int curr_ptr = 0;

	long i = 0;
	while (i < num_entries) {

		if (streq(name, ent->name))
			return ent;

		i += 1;
		if (i % (PAGE_SIZE / sizeof(dirent)) == 0 && curr_ptr == 0) {
			ent = (dirent*)pages_get_page(dd->ptrs[1]);
			curr_ptr += 1;
		}
		else if (i % (PAGE_SIZE / sizeof(dirent)) == 0) {
			int* pgs = (int*)pages_get_page(dd->iptr);
			curr_ptr += 1;
			ent = (dirent*)pages_get_page(pgs[curr_ptr - 2]);
		}
		else 
			ent += 1;
	}

	return NULL;
}

int
directory_lookup(inode* dd, const char* name)
{
	dirent* ent = directory_get(dd, name);
	if (ent)
		return ent->inum;
	else
		return -ENOENT;
}

int
tree_lookup(const char* path)
{
	slist* p_tok = s_split(path, '/');
	if (p_tok)
		p_tok = p_tok->next;

	// if path is just root
	if (!p_tok)
		return 1;

	int inum = -1;
	inode* node = get_inode(1);

	while (p_tok) {
		inum = directory_lookup(node, p_tok->data);

		if (inum != -ENOENT)
			node = get_inode(inum);
		else
			return -ENOENT;

		p_tok = p_tok->next;
	}

	s_free(p_tok);

	printf("tree_lookup: inum %d\n", inum);
	return inum;
}

int
directory_put(const char* dir_path, const char* name, int inum)
{
	int p_inum = tree_lookup(dir_path);
	if (p_inum == -ENOENT) {
		printf("directory_put: Directory not found\n");
		return -ENOENT;
	}
	inode* node = get_inode(p_inum);

	int num_entries = node->size / sizeof(dirent);
	int ents_d = PAGE_SIZE / sizeof(dirent);
	dirent* ent = NULL;

	grow_inode(node, node->size + sizeof(dirent));

	if (num_entries < ents_d - 1) {
		ent = (dirent*)pages_get_page(node->ptrs[0]);
		ent += num_entries;
	}
	else if (num_entries < 2 * ents_d - 1) {
		ent = (dirent*)pages_get_page(node->ptrs[1]);
		ent += num_entries - ents_d;
	}
	else {
		int* ipgs = (int*)pages_get_page(node->iptr);
		int ents_d = PAGE_SIZE / sizeof(dirent);
		int ipg = 0;
		while (num_entries < (ipg + 2) * ents_d - 1)
			ipg += 1;

		ent = (dirent*)pages_get_page(ipg);
		ent += num_entries - (ipg + 2) * ents_d;
	}

	strlcpy(ent->name, name, strlen(name) + 1);
	ent->inum = inum;

    return 0;
}

int
shift_back(int num_entries, dirent* ent, int ent_idx, int ptrs[2], int iptr)
{
	// ent_idx is zero-indexed
	int ents_b = PAGE_SIZE / sizeof(dirent);
	int ent_blk = ent_idx / ents_b;
	int num_blks = num_entries / ents_b;

	dirent* tmp_ent1 = alloca(sizeof(dirent));
	dirent* tmp_ent2 = alloca(sizeof(dirent));

	int blk = num_blks;

	do {
		int* ipgs = NULL;
		void* dir_end = NULL;
		void* dir_start = NULL;
		size_t size = -1;

		if (blk == num_blks) {
			if (blk == 0 || blk == 1) {
				dir_start = pages_get_page(ptrs[blk]);
				dir_end = dir_start + (num_entries - (num_blks - 1) * ents_b) * sizeof(dirent);
			}
			else {
				ipgs = pages_get_page(iptr);
				dir_start = pages_get_page(ipgs[blk - 2]);
				dir_end = dir_start + (num_entries - (num_blks - 1) * ents_b) * sizeof(dirent);
			}

			if (dir_start == (void*)(-1))
				return -1;

			if (blk == ent_blk) {
				size = (intptr_t)dir_end - (intptr_t)(ent + 1);
				memcpy((void*)ent, (void*)(ent + 1), size);
				memset(dir_end - sizeof(dirent), 0, sizeof(dirent));
				break;
			}
			else {
				size = (intptr_t)dir_end - (intptr_t)dir_start - sizeof(dirent);
				memcpy((void*)tmp_ent2, dir_start, sizeof(dirent));
				memcpy(dir_start, dir_start + sizeof(dirent), size);
				memset(dir_end - sizeof(dirent), 0, sizeof(dirent));
				continue;
			}
		}

		if (blk == ent_blk) {
			if (blk == 0 || blk == 1) {
				dir_end = pages_get_page(ptrs[blk]) + PAGE_SIZE;
			}
			else {
				ipgs = pages_get_page(iptr);
				dir_end = pages_get_page(ipgs[blk - 2]) + PAGE_SIZE;
			}

			size = (intptr_t)dir_end - (intptr_t)(ent + 1);
			memcpy((void*)ent, (void*)(ent + 1), size);
			memcpy(dir_end - sizeof(dirent), tmp_ent2, sizeof(dirent));

			break;
		}

		if (blk == 0 || blk == 1) {
			dir_start = pages_get_page(ptrs[blk]);
			dir_end = dir_start + PAGE_SIZE;
		}
		else {
			ipgs = pages_get_page(iptr);
			dir_start = pages_get_page(ipgs[blk - 2]);
			dir_end = dir_start + PAGE_SIZE;
		}

		if (dir_start == (void*)(-1))
			return -1;

		memcpy(tmp_ent1, tmp_ent2, sizeof(dirent));
		memcpy(tmp_ent2, dir_start, sizeof(dirent));

		size = (intptr_t)dir_end - (intptr_t)dir_start - sizeof(dirent);
		memcpy(dir_start, dir_start + sizeof(dirent), size);
		memcpy(dir_end - sizeof(dirent), tmp_ent1, sizeof(dirent));

		blk -= 1;
	} while(blk >= 0);

	return 0;
}

int
directory_delete(const char* path)
{
    printf(" + directory_delete(%s)\n", path);

	// TODO: Add in case where we are deleting a directory

	// if trying to delete the root
	if (streq(path, "/")) {
		printf("directory_delete: Cannot delete root\n");
		return -EINVAL;
	}

	int inum = tree_lookup(path);
	inode* node = get_inode(inum);

	// Get parent directory path, and deletee's name
	char* dir_path = alloca(strlen(path) + 1);
	strlcpy(dir_path, path, strlen(path) + 1);

	char* name = dir_path + strlen(path) + 1;

	while (name[0] != '/')
		name--;
	
	name[0] = '\0';
	name++;

	printf("path    : %s\n", path);
	printf("dir_path: %s\n", dir_path);
	printf("name    : %s\n", name);

//	if(S_ISREG(node->mode))
//		goto dir_delete_file;

//dir_delete_file:
	// get Parent directory inode
    int p_inum = tree_lookup(dir_path);
	inode* p_dir = get_inode(p_inum);

	// Remove directory entry
	int num_entries = p_dir->size / sizeof(dirent);
	dirent* ent = (dirent*)pages_get_page(p_dir->ptrs[0]);
	int curr_ptr = 0;

	long i = 0;
	while (i < num_entries) {

		if (streq(name, ent->name))
			break;

		i += 1;
		if (i % (PAGE_SIZE / sizeof(dirent)) == 0 && curr_ptr == 0) {
			ent = (dirent*)pages_get_page(p_dir->ptrs[1]);
			curr_ptr += 1;
		}
		else if (i % (PAGE_SIZE / sizeof(dirent)) == 0) {
			int* pgs = (int*)pages_get_page(p_dir->iptr);
			curr_ptr += 1;
			ent = (dirent*)pages_get_page(pgs[curr_ptr - 2]);
		}
		else
			ent += 1;
	}

	int err = shift_back(num_entries, ent, i, p_dir->ptrs, p_dir->iptr);

	if (err == -1) {
		printf("directory_delete: Item to delete not found\n");
		return -ENOENT;
	}

	shrink_inode(p_dir, p_dir->size - sizeof(dirent));
    return 0;
}

slist*
directory_list(inode* dd)
{
    printf("+ directory_list()\n");

    slist* ys = NULL;

	int num_entries = dd->size / sizeof(dirent);
	dirent* ent = (dirent*)pages_get_page(dd->ptrs[0]);
	int curr_ptr = 0;

	long i = 0;
	while (i < num_entries) {

		ys = s_cons(ent->name, ys);

		i += 1;
		if (i % (PAGE_SIZE / sizeof(dirent)) == 0) {
			if (curr_ptr == 0) {
				ent = (dirent*)pages_get_page(dd->ptrs[1]);
				curr_ptr += 1;
			}
			else {
				int* ipgs = (int*)pages_get_page(dd->iptr);
				ent = (dirent*)pages_get_page(ipgs[curr_ptr - 1]);
				curr_ptr += 1;
			}
		}
		else 
			ent += 1;
	}

    return ys;
}

void
print_directory(inode* dd)
{
    slist* items = directory_list(dd);

	printf("Contents:\n");
	while (items) {
		printf(" - %s\n", items->data);
		items = items->next;
	}

    printf("(end of contents)\n");
}
