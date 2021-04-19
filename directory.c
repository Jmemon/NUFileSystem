
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

	int err_check = grow_inode(rn, PAGE_SIZE);
	assert(err_check == PAGE_SIZE);

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
	do {

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

	} while(i < num_entries);

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

	// if path is just root
	if (streq(p_tok->data, ""))
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

	strlcpy(ent->name, name, strlen(name));
	ent->inum = inum;

    return 0;
}

int
directory_delete(const char* path)
{
    printf(" + directory_delete(%s)\n", path);

	// TODO: Add in case where p_tok is length one (file is in root)
	// TODO: Add in case where we are deleting a directory

	// get name of directory to delete from
	slist* p_tok = s_split(path, '/');
	if (!p_tok) {
		printf("directory_delete: Cannot delete root\n");
		return -EINVAL;
	}

	// Get to directory of thing to delete
	while (p_tok && p_tok->next && p_tok->next->next)
		p_tok = p_tok->next;

	// get Parent directory inode
    int inum = tree_lookup(p_tok->data);
	inode* p_dir = get_inode(inum);

	// Free node and its pages within p_dir
	p_tok = p_tok->next;
	char* name = p_tok->data;
	inum = directory_lookup(p_dir, name);
    free_inode(inum);

	// Remove directory entry
    dirent* ent = directory_get(p_dir, name);
	memset(ent, 0, sizeof(dirent));
	// TODO: shift all dirents after removed back one dirent

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
	do {

		s_cons(ent->name, ys);

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

	} while(i < num_entries);

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
