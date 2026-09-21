/*
 * vfs.h - volumes, paths and open files
 *
 * docs/fs_design.md. Names are Amiga-shaped - "boot:docs/notes.txt" - and
 * looked up without regard to case. Open files are handles, small integers,
 * validated on every call and closed when their owner exits.
 */
#ifndef VFS_H
#define VFS_H

#include "blkdev.h"

#define VFS_MAX_MOUNTS   8
#define VFS_MAX_HANDLES  32
#define VFS_NAME_MAX     255        /* ext2's limit; PFS3 stops at 107 */

/* Errors are negative. */
#define VFS_OK        0
#define VFS_ENOENT   (-1)   /* no such file, directory or volume */
#define VFS_EIO      (-2)   /* the disk said no */
#define VFS_EBADF    (-3)   /* not an open handle, or not of that kind */
#define VFS_ENOTDIR  (-4)
#define VFS_EISDIR   (-5)
#define VFS_EMFILE   (-6)   /* out of handles, or of mounts */
#define VFS_ENODEV   (-7)   /* no such device, or no filesystem on it */
#define VFS_EBUSY    (-8)   /* volume name taken; or unmounting with files open */
#define VFS_EINVAL   (-9)
#define VFS_EROFS    (-10)  /* read-only filesystem */
#define VFS_EEXIST   (-11)  /* the name is taken - perhaps only by case */
#define VFS_ENOSPC   (-12)  /* no blocks, or no inodes, left */
#define VFS_ENOTEMPTY (-13)

/* vfs_open_flags */
#define VFS_O_READ    0x00
#define VFS_O_WRITE   0x01
#define VFS_O_CREATE  0x02  /* make it if it is not there */
#define VFS_O_TRUNC   0x04  /* empty it if it is */

#define VFS_FILE  1
#define VFS_DIR   2
#define VFS_LINK  3      /* a symbolic link: listed, not followed, not opened */

/* What a filesystem driver calls a file. The VFS keeps it in the handle and
 * never looks inside priv. */
struct vfs_node {
    unsigned long type;             /* VFS_FILE or VFS_DIR */
    unsigned long size;
    unsigned long priv[4];
};

struct vfs_dirent {
    char          name[VFS_NAME_MAX + 1];
    unsigned long type;
    unsigned long size;
};

struct vfs_stat {
    unsigned long type;
    unsigned long size;
};

/*
 * A filesystem driver. Every call is made with the mount's lock held, so a
 * driver need not be reentrant. `dev` reads and writes through the block
 * cache. Anything a read-only driver does not do is NULL.
 */
struct fs_ops {
    const char *name;

    /* Is there one of these on dev? Cheap: a superblock's worth of reading. */
    int  (*probe)(const struct blkdev *dev);
    int  (*mount)(const struct blkdev *dev, void **fsdata);
    void (*unmount)(void *fsdata);

    void (*root)(void *fsdata, struct vfs_node *out);
    /* name is one path component, to be matched without regard to case. */
    int  (*lookup)(void *fsdata, const struct vfs_node *dir, const char *name,
                   struct vfs_node *out);
    long (*read)(void *fsdata, struct vfs_node *node, unsigned long offset,
                 void *buf, unsigned long len);
    /* *cookie is 0 to start and the driver's to advance. 1 if an entry was
     * produced, 0 at the end, negative on error. */
    int  (*readdir)(void *fsdata, const struct vfs_node *dir,
                    unsigned long *cookie, struct vfs_dirent *out);

    /* --- a writable filesystem has all of these; a read-only one, none --- */

    /* type is VFS_FILE or VFS_DIR. The name is known not to exist. */
    int  (*create)(void *fsdata, const struct vfs_node *dir, const char *name,
                   unsigned long type, struct vfs_node *out);
    /* Extends the file as needed; node->size is kept up to date. */
    long (*write)(void *fsdata, struct vfs_node *node, unsigned long offset,
                  const void *buf, unsigned long len);
    int  (*truncate)(void *fsdata, struct vfs_node *node);          /* to nothing */
    /* A file, or an empty directory. */
    int  (*remove)(void *fsdata, const struct vfs_node *dir, const char *name);
    /* Everything the driver has been keeping to itself, onto the disk. */
    int  (*sync)(void *fsdata);
};

void vfs_init(void);
int  vfs_register_fs(const struct fs_ops *ops);

/* fstype NULL means ask each driver in turn. */
int  vfs_mount(const char *volume, const char *devname, const char *fstype);
int  vfs_unmount(const char *volume);

/* One line per mount, for the console: index 0.. until it returns 0. */
int  vfs_mount_info(unsigned long index, const char **volume,
                    const char **devname, const char **fstype);

int  vfs_stat(const char *path, struct vfs_stat *out);

int  vfs_open(const char *path);            /* to read: a handle >= 1, or an error */
int  vfs_open_flags(const char *path, unsigned long flags);
long vfs_write(int handle, const void *buf, unsigned long len);
int  vfs_mkdir(const char *path);
int  vfs_remove(const char *path);          /* a file, or an empty directory */
int  vfs_sync(void);                        /* every mounted volume */
long vfs_read(int handle, void *buf, unsigned long len);
long vfs_seek(int handle, unsigned long offset);
int  vfs_close(int handle);

int  vfs_opendir(const char *path);
int  vfs_readdir(int handle, struct vfs_dirent *out);   /* 1, 0 at the end, or < 0 */

unsigned long vfs_handles_in_use(void);

#endif /* VFS_H */
