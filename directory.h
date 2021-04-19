#ifndef DIRECTORY_H
#define DIRECTORY_H

#include <bsd/string.h>

#include "slist.h"
#include "pages.h"
#include "inode.h"
#include "directory.h"

typedef struct dirent {
	char name[48];
	int inum;
} dirent;

void directory_init();
dirent* directory_get(inode* dd, const char* name);
int directory_lookup(inode* dd, const char* name);
int tree_lookup(const char* path);
int directory_put(const char* dir_path, const char* name, int inum);
int directory_delete(const char* name);
slist* directory_list(inode* dd);
void print_directory(inode* dd);

#endif

