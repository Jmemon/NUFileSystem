
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
			// TODO: Indirect Ptrs
			ent = (dirent*)pages_get_page(dd->iptr);
			curr_ptr += 1;
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
	dirent* ent = NULL;

	grow_inode(node, node->size + sizeof(dirent));

	if (num_entries < PAGE_SIZE / sizeof(dirent)) {
		ent = (dirent*)pages_get_page(node->ptrs[0]);
		ent += num_entries;
	}
	else if (num_entries < 2 * PAGE_SIZE / sizeof(dirent)) {
		ent = (dirent*)pages_get_page(node->ptrs[1]);
		ent += num_entries - (PAGE_SIZE / sizeof(dirent));
	}
	else {
		;// TODO: Indirect Pointers
	}

	strlcpy(ent->name, name, strlen(name) + 1);
	ent->inum = inum;

    return 0;
}

int
directory_delete(const char* path)
{
    printf(" + directory_delete(%s)\n", path);

	// TODO: Add in case where we are deleting a directory

	slist* p_tok = s_split(path, '/');

	// if trying to delete the root
	if (streq(p_tok->data, "") && !p_tok->next) {
		printf("directory_delete: Cannot delete root\n");
		return -EINVAL;
	}

	// Get to directory to delete from
	while (p_tok && p_tok->next && p_tok->next->next)
		p_tok = p_tok->next;

	// get Parent directory inode
    int inum = tree_lookup(p_tok->data);
	inode* p_dir = get_inode(inum);

	// get file name to delete
	p_tok = p_tok->next;
	char* name = p_tok->data;

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
			// TODO: Indirect Ptrs
			ent = (dirent*)pages_get_page(p_dir->iptr);
			curr_ptr += 1;
		}
		else
			ent += 1;
	}

	i += 1;
	int ents_d = PAGE_SIZE / sizeof(dirent);
	if (i == num_entries)
		memset(ent, 0, sizeof(dirent));
	else if (i < num_entries) {
		ent += 1;
		if (i <= ents_d) {
			if (num_entries <= ents_d) {
				// Shift Entries of first block back
				void* dir_end = pages_get_page(p_dir->ptrs[0]) + num_entries * sizeof(dirent);
				size_t size = (intptr_t)dir_end - (intptr_t)ent;
				memcpy((void*)(ent - 1), (void*)ent, size);
				memset(dir_end - sizeof(dirent), 0, sizeof(dirent));
			}
			else if (ents_d < num_entries <= 2 * ents_d) {
				// Shift Remaining entries of first block back
				void* dir_end = pages_get_page(p_dir->ptrs[0]) + PAGE_SIZE;
				size_t size = (intptr_t)dir_end - (intptr_t)ent;
				memcpy((void*)(ent - 1), (void*)ent, size);

				// Shift first entry of second block to last entry of first block
				void* dir_start = pages_get_page(p_dir->ptrs[1]);
				size = sizeof(dirent);
				memcpy(dir_end - sizeof(dirent), dir_start, size);

				// Shift Remaining elements of second block back one entry
				dir_end = dir_start + num_entries * sizeof(dirent);
				size = (intptr_t)dir_end - (intptr_t)dir_start - sizeof(dirent);
				memcpy(dir_start, dir_start + sizeof(dirent), size);
				memset(dir_end - sizeof(dirent), 0, sizeof(dirent));
			}
			else {
				// TODO: Indirect Pointers
			}
		}
		else if (ents_d < i <= 2 * ents_d) {
			if (ents_d < num_entries <= 2 * ents_d) {
				// Shift Entries of second block back
				void* dir_end = pages_get_page(p_dir->ptrs[1]) + (num_entries - ents_d) * sizeof(dirent);
				size_t size = (intptr_t)dir_end - (intptr_t)ent;
				memcpy((void*)(ent - 1), (void*)ent, size);
				memset(dir_end - sizeof(dirent), 0, sizeof(dirent));
			}
			else {
				// TODO: Indirect Pointers
			}
		}
		else {
			// TODO: Indirect Pointers
		}
	}
	else {
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
			else if (curr_ptr == 1) {
				// TODO: Indirect Pointers
				ent = (dirent*)pages_get_page(dd->iptr);
				curr_ptr += 1;
			}
			else {
				//int* indir_page = (int*)pages_get_page();
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
