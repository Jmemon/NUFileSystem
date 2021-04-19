#include <stdio.h>
#include <sys/stat.h>

#include "globals.h"
#include "inode.h"

// Page
const int PAGE_COUNT = 256;
const int PAGE_SIZE = 4096;
const int NUFS_SIZE = 256 * 4096; // 1MB
int   pages_fd = -1;
void* pages_base = NULL;

// Inode
int INODE_COUNT = 0;
inode* inode_base = NULL;

// Permissions
const int default_file_mode    = S_IFREG | S_IRWXU | S_IRGRP | 
							     S_IXGRP | S_IROTH | S_IXOTH;
const int default_dir_mode     = S_IFDIR | S_IRWXU | S_IRGRP | 
							     S_IXGRP | S_IROTH | S_IXOTH;
const int default_symlink_mode = S_IFLNK | S_IRWXU | S_IRGRP | 
							     S_IXGRP | S_IROTH | S_IXOTH;

// Mounting
int num_mounts = 0;

void
globals_reset()
{
	pages_fd = -1;
	pages_base = NULL;

	INODE_COUNT = 0;
	inode_base = NULL;

	num_mounts = 0;
}

void
globals_print()
{
	printf("========PAGE  VARS=======\n");
	printf("PAGE_COUNT: %d\n", PAGE_COUNT);
	printf("PAGE_SIZE : %d\n", PAGE_SIZE);
	printf("NUFS_SIZE : %d\n", NUFS_SIZE);
	printf("pages_fd  : %d\n", pages_fd);
	printf("pages_base: %p\n", pages_base);
	printf("\n");

	printf("========INODE VARS=======\n");
	printf("INODE_COUNT  : %d\n", INODE_COUNT);
	printf("inode_base   : %p\n", inode_base);
	printf("\n");
}

int
globals_init_check()
{
	// return 1 if init, else -1
	char page_check = pages_fd == -1 || !pages_base;
	char inod_check = INODE_COUNT == 0 || !inode_base;

	if (page_check || inod_check) {
		printf("globals_init_check: Something is not set\n\n");
		globals_print();
		return -1;
	}

	return 1;
}

int
globals_pinit_check()
{
	char page_check = pages_fd == -1 || !pages_base;

	if (page_check) {
		printf("globals_pinit_check: some page var(s) not set\n\n");
		printf("========PAGE  VARS=======\n");
		printf("PAGE_COUNT: %d\n", PAGE_COUNT);
		printf("PAGE_SIZE : %d\n", PAGE_SIZE);
		printf("NUFS_SIZE : %d\n", NUFS_SIZE);
		printf("pages_fd  : %d\n", pages_fd);
		printf("pages_base: %p\n", pages_base);
		printf("\n");

		return -1;
	}

	return 1;
}

int
globals_iinit_check()
{
	char inod_check = INODE_COUNT == 0 || !inode_base;

	if (inod_check) {
		printf("globals_iinit_check: some inode var(s) not set\n\n");
		printf("========INODE VARS=======\n");
		printf("INODE_COUNT  : %d\n", INODE_COUNT);
		printf("inode_base   : %p\n", inode_base);
		printf("\n");

		return -1;
	}

	return 1;
}

