#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/stat.h>
#include <bsd/string.h>
#include <assert.h>
#include <alloca.h>

#define FUSE_USE_VERSION 26
#include <fuse.h>

#include "storage.h"
#include "directory.h"
#include "slist.h"
#include "util.h"
#include "inode.h"

#include "globals.h"

extern int num_mounts;

extern const int default_symlink_mode;

// implementation for: man 2 access
// Checks if a file exists.
int
nufs_access(const char *path, int mask)
{
    int rv = 0;

	/*int inum = tree_lookup(path);
	if (inum == -ENOENT) {
		rv = -ENOENT;
		goto access_done;
	}

	inode* node = get_inode(inum);
	int val = node->mode && mask;

	if (val && S_IRWXU > 0)
		rv = -EACCES;

access_done:*/
    printf("access(%s, %04o) -> %d\n", path, mask, rv);
    return rv;
}

// implementation for: man 2 stat
// gets an object's attributes (type, permissions, size, etc)
int
nufs_getattr(const char *path, struct stat *st)
{
    int rv = storage_stat(path, st);
    printf("getattr(%s) -> (%d) {mode: %04o, size: %ld}\n", path, rv, st->st_mode, st->st_size);
    return rv;
}

// implementation for: man 2 readdir
// lists the contents of a directory
int
nufs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
             off_t offset, struct fuse_file_info *fi)
{
    struct stat st;
    char item_path[256];
    int rv;

    rv = storage_stat(path, &st);
    assert(rv == 0);

    filler(buf, ".", &st, 0);

    slist* items = storage_list(path);

	memset(item_path, 0, 256 * sizeof(char));
    strlcpy(item_path, path, strlen(path) + 1);

	char* start = item_path + strlen(path) + 1;
	start[0] = '/';
	start++;

	while (items) {
        printf("+ looking at path: '%s'\n", items->data);

		strlcpy(start, items->data, strlen(items->data) + 1);
		printf("%s\n", item_path);

        rv = storage_stat(item_path, &st);
        assert(rv == 0);

        filler(buf, items->data, &st, 0);
	
		items = items->next;
    }

    s_free(items);

    printf("readdir(%s) -> %d\n", path, rv);
    return rv;
}

// mknod makes a filesystem object like a file or directory
// called for: man 2 open, man 2 link
int
nufs_mknod(const char *path, mode_t mode, dev_t rdev)
{
    int rv = storage_mknod(path, mode);
    printf("mknod(%s, %04o) -> %d\n", path, mode, rv);
    return rv;
}

// most of the following callbacks implement
// another system call; see section 2 of the manual
int
nufs_mkdir(const char *path, mode_t mode)
{
	mode = S_IFDIR | mode;
    int rv = storage_mknod(path, mode);
    printf("mkdir(%s) -> %d\n", path, rv);
    return rv;
}

int
nufs_unlink(const char *path)
{
    int rv = storage_unlink(path);
    printf("unlink(%s) -> %d\n", path, rv);
    return rv;
}

int
nufs_link(const char *from, const char *to)
{
    int rv = storage_link(from, to);
    printf("link(%s => %s) -> %d\n", from, to, rv);
	return rv;
}

int
nufs_symlink(const char *from, const char *to)
{
	mode_t mode = default_symlink_mode;
	int rv = storage_mknod(to, mode);

	rv = storage_write(to, from, strlen(from) + 1, 0);
	if (rv == strlen(from) + 1)
		rv = 0;
	else
		rv = -1;

    printf("symlink(%s => %s) -> %d\n", from, to, rv);
	return rv;
}

int
nufs_readlink(const char *path, char* buf, size_t size)
{
	int rv = storage_read(path, buf, size, 0);
	int inum = tree_lookup(path);
	inode* node = get_inode(inum);

	if (rv != node->size)
		rv = -1;
	else
		rv = 0;

    printf("readlink(%s) -> %d\n", path, rv);
	return rv;
}

int
nufs_rmdir(const char *path)
{
    int rv = storage_unlink(path);
    printf("rmdir(%s) -> %d\n", path, rv);
    return rv;
}

// implements: man 2 rename
// called to move a file within the same filesystem
int
nufs_rename(const char *from, const char *to)
{
    int rv = storage_rename(from, to);
    printf("rename(%s => %s) -> %d\n", from, to, rv);
    return rv;
}

int
nufs_chmod(const char *path, mode_t mode)
{
    int rv = 0;
	int inum = tree_lookup(path);
	inode* node = get_inode(inum);
	node->mode = mode;
    printf("chmod(%s, %04o) -> %d\n", path, mode, rv);
    return rv;
}

int
nufs_truncate(const char *path, off_t size)
{
    int rv = storage_truncate(path, size);
    printf("truncate(%s, %ld bytes) -> %d\n", path, size, rv);
    return rv;
}

// this is called on open, but doesn't need to do much
// since FUSE doesn't assume you maintain state for
// open files.
int
nufs_open(const char *path, struct fuse_file_info *fi)
{
    int rv = nufs_access(path, 0);
    printf("open(%s) -> %d\n", path, rv);
    return rv;
}

// Actually read data
int
nufs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    int rv = storage_read(path, buf, size, offset);
    printf("read(%s, %ld bytes, @+%ld) -> %d\n", path, size, offset, rv);
    return rv;
}

// Actually write data
int
nufs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    int rv = storage_write(path, buf, size, offset);
    printf("write(%s, %ld bytes, @+%ld) -> %d\n", path, size, offset, rv);
    return rv;
}

// Update the timestamps on a file or directory.
int
nufs_utimens(const char* path, const struct timespec ts[2])
{
    int rv = storage_set_time(path, ts);
    printf("utimens(%s, [%ld, %ld; %ld %ld]) -> %d\n",
           path, ts[0].tv_sec, ts[0].tv_nsec, ts[1].tv_sec, ts[1].tv_nsec, rv);
	return rv;
}

// Extended operations
int
nufs_ioctl(const char* path, int cmd, void* arg, struct fuse_file_info* fi,
           unsigned int flags, void* data)
{
    int rv = -1;
    printf("ioctl(%s, %d, ...) -> %d\n", path, cmd, rv);
    return rv;
}

void
nufs_init_ops(struct fuse_operations* ops)
{
    memset(ops, 0, sizeof(struct fuse_operations));
    ops->access   = nufs_access;
    ops->getattr  = nufs_getattr;
    ops->readdir  = nufs_readdir;
    ops->mknod    = nufs_mknod;
    ops->mkdir    = nufs_mkdir;
    ops->link     = nufs_link;
    ops->unlink   = nufs_unlink;
	ops->symlink  = nufs_symlink;
	ops->readlink = nufs_readlink;
    ops->rmdir    = nufs_rmdir;
    ops->rename   = nufs_rename;
    ops->chmod    = nufs_chmod;
    ops->truncate = nufs_truncate;
    ops->open	  = nufs_open;
    ops->read     = nufs_read;
    ops->write    = nufs_write;
    ops->utimens  = nufs_utimens;
    ops->ioctl    = nufs_ioctl;
};

struct fuse_operations nufs_ops;

int
main(int argc, char *argv[])
{
    assert(argc > 2 && argc < 6);

	if (num_mounts == 0)
    	storage_init(argv[--argc]);

	num_mounts += 1;

	int err = globals_init_check();
	if (err == -1) {
		printf("Some Globals Vars Not Set\n");
		return -1;
	}

    nufs_init_ops(&nufs_ops);
    return fuse_main(argc, argv, &nufs_ops, NULL);
}

