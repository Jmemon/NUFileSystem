
#ifndef GLOBALS_H
#define GLOBALS_H

#include <time.h>

#include "inode.h"

// Page
extern const int PAGE_COUNT;
extern const int PAGE_SIZE;
extern const int NUFS_SIZE;
extern int   pages_fd;
extern void* pages_base;

// Inode
extern int INODE_COUNT;
extern inode* inode_base;

// Default Permissions
extern const int default_file_mode;
extern const int default_dir_mode;
extern const int default_symlink_mode;

// Mounting
extern int num_mounts;

// Utility Functions
void globals_reset();
void globals_print();
int globals_init_check();
int globals_pinit_check();
int globals_iinit_check();

#endif
