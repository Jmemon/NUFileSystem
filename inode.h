#ifndef INODE_H
#define INODE_H

#include "pages.h"

typedef struct inode {
	char refs;
    int mode; // permission & type; zero for unused
    int size; // bytes
	int ptrs[2]; // direct pointers
	int iptr; // single indirect pointer`
	long acc; // last access time
	long mod; // last modification time
} inode;

void print_inode(inode* node);
void init_inode_gvars();
inode* get_inode(int inum);
int alloc_inode();
int grow_inode(inode* node, int size);
int shrink_inode(inode* node, int size);
void free_inode();
int inode_get_pnum(inode* node, int fpn);

#endif
