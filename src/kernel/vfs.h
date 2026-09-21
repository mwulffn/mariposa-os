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
#define VFS_NAME_MAX     107        /* what PFS3 allows; ext2's 255 is cut */

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

#define VFS_FILE  1
#define VFS_DIR   2

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

int  vfs_open(const char *path);            /* a handle >= 1, or an error */
long vfs_read(int handle, void *buf, unsigned long len);
long vfs_seek(int handle, unsigned long offset);
int  vfs_close(int handle);

int  vfs_opendir(const char *path);
int  vfs_readdir(int handle, struct vfs_dirent *out);   /* 1, 0 at the end, or < 0 */

unsigned long vfs_handles_in_use(void);

#endif /* VFS_H */
