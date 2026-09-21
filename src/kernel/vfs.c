/*
 * vfs.c - volumes, paths and open files
 */
#include "vfs.h"
#include "blk.h"
#include "bcache.h"
#include "task.h"
#include "kstring.h"
#include "cpu.h"

#define MAX_FS 4

struct mount {
    char                 volume[16];
    const struct fs_ops *ops;           /* NULL: slot free */
    void                *fsdata;
    const struct blkdev *raw;           /* the device underneath */
    struct blkdev        cached;        /* what the driver is given */
    struct mutex         lock;
    unsigned long        open_count;
};

struct handle {
    struct mount   *mount;              /* NULL: slot free */
    struct task    *owner;
    struct vfs_node node;
    unsigned long   pos;                /* file offset, or readdir cookie */
    int             is_dir;
    int             writable;
};

static const struct fs_ops *filesystems[MAX_FS];
static struct mount  mounts[VFS_MAX_MOUNTS];
static struct handle handles[VFS_MAX_HANDLES];

/* ------------------------------------------------------ the cached device --- */

/* Drivers never touch a block device. They get one of these, whose hw is
 * the real device and whose read and write go through the cache - which is
 * how fat16.c, written for the ROM with no cache in mind, runs cached here
 * without a line changed. */
static int cached_read(const struct blkdev *dev, unsigned long lba,
                       unsigned count, void *buf)
{
    return bc_read((const struct blkdev *)dev->hw, lba, count, buf);
}

static int cached_write(const struct blkdev *dev, unsigned long lba,
                        unsigned count, const void *buf)
{
    return bc_write((const struct blkdev *)dev->hw, lba, count, buf);
}

static int direct_read(const struct blkdev *dev, unsigned long lba,
                       unsigned count, void *buf)
{
    return bc_read_direct((const struct blkdev *)dev->hw, lba, count, buf);
}

static int direct_write(const struct blkdev *dev, unsigned long lba,
                        unsigned count, const void *buf)
{
    return bc_write_direct((const struct blkdev *)dev->hw, lba, count, buf);
}

static int cached_present(const struct blkdev *dev)
{
    const struct blkdev *raw = dev->hw;

    return raw->present(raw);
}

/* ---------------------------------------------------------------- mounts --- */

static struct mount *find_mount(const char *volume, unsigned long len)
{
    int i;

    for (i = 0; i < VFS_MAX_MOUNTS; i++) {
        const char *v = mounts[i].volume;
        unsigned long n;

        if (!mounts[i].ops)
            continue;
        for (n = 0; n < len && v[n]; n++)
            if ((v[n] | 0x20) != (volume[n] | 0x20))
                break;
        if (n == len && !v[n])
            return &mounts[i];
    }
    return 0;
}

int vfs_register_fs(const struct fs_ops *ops)
{
    int i;

    for (i = 0; i < MAX_FS; i++)
        if (!filesystems[i] || filesystems[i] == ops) {
            filesystems[i] = ops;
            return VFS_OK;
        }
    return VFS_EMFILE;
}

int vfs_mount(const char *volume, const char *devname, const char *fstype)
{
    const struct blkdev *raw = blk_find(devname);
    const struct fs_ops *ops = 0;
    struct mount *m = 0;
    int i, rc;

    if (!raw)
        return VFS_ENODEV;
    if (!*volume || str_len(volume) >= sizeof m->volume)
        return VFS_EINVAL;
    if (find_mount(volume, str_len(volume)))
        return VFS_EBUSY;

    for (i = 0; i < VFS_MAX_MOUNTS && !m; i++)
        if (!mounts[i].ops)
            m = &mounts[i];
    if (!m)
        return VFS_EMFILE;

    m->raw = raw;
    m->cached.name    = raw->name;
    m->cached.read    = cached_read;
    m->cached.present = cached_present;
    m->cached.hw      = raw;
    m->cached.write   = raw->write ? cached_write : 0;
    m->cached.blocks  = raw->blocks;
    m->cached.bulk_read  = direct_read;
    m->cached.bulk_write = raw->write ? direct_write : 0;

    /* A named type is believed only if its own probe agrees: mounting the
     * wrong driver on a disk is how disks get eaten. */
    for (i = 0; i < MAX_FS && !ops; i++) {
        const struct fs_ops *f = filesystems[i];

        if (f && (!fstype || str_caseeq(f->name, fstype)) && f->probe(&m->cached))
            ops = f;
    }
    if (!ops)
        return VFS_ENODEV;

    rc = ops->mount(&m->cached, &m->fsdata);
    if (rc != VFS_OK)
        return rc;

    for (i = 0; volume[i]; i++)
        m->volume[i] = volume[i];
    m->volume[i] = '\0';
    m->lock.owner = 0;
    m->lock.waiters.head = m->lock.waiters.tail = 0;
    m->open_count = 0;
    m->ops = ops;                       /* last: this is what makes it visible */
    return VFS_OK;
}

int vfs_unmount(const char *volume)
{
    struct mount *m = find_mount(volume, str_len(volume));

    if (!m)
        return VFS_ENOENT;
    mutex_lock(&m->lock);
    if (m->open_count) {
        mutex_unlock(&m->lock);
        return VFS_EBUSY;
    }
    if (m->ops->sync)
        m->ops->sync(m->fsdata);
    if (m->ops->unmount)
        m->ops->unmount(m->fsdata);
    bc_forget(m->raw);
    m->ops = 0;
    mutex_unlock(&m->lock);
    return VFS_OK;
}

int vfs_mount_info(unsigned long index, const char **volume,
                   const char **devname, const char **fstype)
{
    int i;

    for (i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!mounts[i].ops)
            continue;
        if (index-- == 0) {
            *volume  = mounts[i].volume;
            *devname = mounts[i].raw->name;
            *fstype  = mounts[i].ops->name;
            return 1;
        }
    }
    return 0;
}

/* ----------------------------------------------------------------- paths --- */

/*
 * "volume:dir/file" -> the mount, and the node the path names. With no
 * "volume:" the first mounted volume is meant; there is no notion of a
 * current directory yet for it to be relative to.
 *
 * Returns with the mount's lock HELD on success, since whatever the caller
 * does next with the node has to be under it.
 */
static int walk_to(const char *path, struct mount **mount_out, struct vfs_node *out,
                   char *leaf);

static int walk(const char *path, struct mount **mount_out, struct vfs_node *out)
{
    return walk_to(path, mount_out, out, 0);
}

/* With `leaf` non-NULL, stop one short: *out is the directory and leaf gets
 * the last component's name, which need not exist. leaf[0] is 0 if the path
 * named a volume's root and so has no last component. */
static int walk_to(const char *path, struct mount **mount_out, struct vfs_node *out,
                   char *leaf)
{
    char name[VFS_NAME_MAX + 1];
    const char *p = path, *colon;
    struct mount *m = 0;
    struct vfs_node node;
    int i, rc = VFS_OK;

    if (leaf)
        leaf[0] = '\0';
    for (colon = path; *colon && *colon != ':'; colon++)
        ;
    if (*colon) {
        m = find_mount(path, (unsigned long)(colon - path));
        p = colon + 1;
    } else {
        for (i = 0; i < VFS_MAX_MOUNTS && !m; i++)
            if (mounts[i].ops)
                m = &mounts[i];
    }
    if (!m)
        return VFS_ENOENT;

    mutex_lock(&m->lock);
    if (!m->ops) {                      /* unmounted while we waited */
        mutex_unlock(&m->lock);
        return VFS_ENOENT;
    }
    m->ops->root(m->fsdata, &node);

    while (rc == VFS_OK) {
        unsigned long n = 0;

        while (*p == '/')
            p++;
        if (!*p)
            break;
        while (*p && *p != '/') {
            if (n == VFS_NAME_MAX) {
                rc = VFS_ENOENT;        /* nothing has a name that long */
                break;
            }
            name[n++] = *p++;
        }
        name[n] = '\0';
        if (rc == VFS_OK && leaf) {
            const char *rest = p;

            while (*rest == '/')
                rest++;
            if (!*rest) {               /* that was the last component */
                for (n = 0; name[n]; n++)
                    leaf[n] = name[n];
                leaf[n] = '\0';
                if (node.type != VFS_DIR)
                    rc = VFS_ENOTDIR;
                break;
            }
        }
        if (rc == VFS_OK) {
            if (node.type != VFS_DIR)
                rc = VFS_ENOTDIR;
            else
                rc = m->ops->lookup(m->fsdata, &node, name, &node);
        }
    }

    if (rc != VFS_OK) {
        mutex_unlock(&m->lock);
        return rc;
    }
    *mount_out = m;
    *out = node;
    return VFS_OK;
}

int vfs_stat(const char *path, struct vfs_stat *out)
{
    struct vfs_node node;
    struct mount *m;
    int rc = walk(path, &m, &node);

    if (rc != VFS_OK)
        return rc;
    out->type = node.type;
    out->size = node.size;
    mutex_unlock(&m->lock);
    return VFS_OK;
}

/* --------------------------------------------------------------- handles --- */

/* Find path for writing, creating or emptying it as flags say. Returns with
 * the mount locked on success, like walk(). */
static int walk_for_write(const char *path, unsigned long flags,
                          struct mount **mount_out, struct vfs_node *out)
{
    char leaf[VFS_NAME_MAX + 1];
    struct vfs_node dir;
    struct mount *m;
    int rc = walk_to(path, &m, &dir, leaf);

    if (rc != VFS_OK)
        return rc;
    if (!m->ops->write)
        rc = VFS_EROFS;
    else if (!leaf[0])
        rc = VFS_EISDIR;                /* a volume's root is not a file */
    else {
        rc = m->ops->lookup(m->fsdata, &dir, leaf, out);
        if (rc == VFS_ENOENT && (flags & VFS_O_CREATE))
            rc = m->ops->create(m->fsdata, &dir, leaf, VFS_FILE, out);
        else if (rc == VFS_OK && out->type == VFS_FILE && (flags & VFS_O_TRUNC))
            rc = m->ops->truncate(m->fsdata, out);
    }
    if (rc != VFS_OK) {
        mutex_unlock(&m->lock);
        return rc;
    }
    *mount_out = m;
    return VFS_OK;
}

static int open_as(const char *path, int want_dir, unsigned long flags)
{
    struct vfs_node node;
    struct mount *m;
    int i, rc;

    rc = (flags & VFS_O_WRITE) ? walk_for_write(path, flags, &m, &node)
                               : walk(path, &m, &node);
    if (rc != VFS_OK)
        return rc;

    if (want_dir && node.type != VFS_DIR)
        rc = VFS_ENOTDIR;
    else if (!want_dir && node.type == VFS_DIR)
        rc = VFS_EISDIR;
    else if (node.type != VFS_FILE && node.type != VFS_DIR)
        rc = VFS_EINVAL;                /* a symlink: nothing follows them yet */
    else {
        rc = VFS_EMFILE;
        CRITICAL_ENTER();
        for (i = 0; i < VFS_MAX_HANDLES; i++)
            if (!handles[i].mount) {
                handles[i].mount  = m;
                handles[i].owner  = task_current();
                handles[i].node   = node;
                handles[i].pos    = 0;
                handles[i].is_dir = want_dir;
                handles[i].writable = (flags & VFS_O_WRITE) != 0;
                rc = i + 1;
                break;
            }
        CRITICAL_EXIT();
        if (rc > 0)
            m->open_count++;
    }
    mutex_unlock(&m->lock);
    return rc;
}

int vfs_open(const char *path)    { return open_as(path, 0, VFS_O_READ); }
int vfs_opendir(const char *path) { return open_as(path, 1, VFS_O_READ); }

int vfs_open_flags(const char *path, unsigned long flags)
{
    return open_as(path, 0, flags);
}

static struct handle *get(int handle, int want_dir)
{
    struct handle *h;

    if (handle < 1 || handle > VFS_MAX_HANDLES)
        return 0;
    h = &handles[handle - 1];
    return (h->mount && h->is_dir == want_dir) ? h : 0;
}

long vfs_read(int handle, void *buf, unsigned long len)
{
    struct handle *h = get(handle, 0);
    long n;

    if (!h)
        return VFS_EBADF;
    mutex_lock(&h->mount->lock);
    n = h->mount->ops->read(h->mount->fsdata, &h->node, h->pos, buf, len);
    if (n > 0)
        h->pos += (unsigned long)n;
    mutex_unlock(&h->mount->lock);
    return n;
}

long vfs_write(int handle, const void *buf, unsigned long len)
{
    struct handle *h = get(handle, 0);
    long n;

    if (!h || !h->writable)
        return VFS_EBADF;
    mutex_lock(&h->mount->lock);
    n = h->mount->ops->write(h->mount->fsdata, &h->node, h->pos, buf, len);
    if (n > 0)
        h->pos += (unsigned long)n;
    mutex_unlock(&h->mount->lock);
    return n;
}

int vfs_mkdir(const char *path)
{
    char leaf[VFS_NAME_MAX + 1];
    struct vfs_node dir, made;
    struct mount *m;
    int rc = walk_to(path, &m, &dir, leaf);

    if (rc != VFS_OK)
        return rc;
    if (!m->ops->create)
        rc = VFS_EROFS;
    else if (!leaf[0])
        rc = VFS_EEXIST;
    else if (m->ops->lookup(m->fsdata, &dir, leaf, &made) == VFS_OK)
        rc = VFS_EEXIST;                /* including by case: Docs and docs */
    else
        rc = m->ops->create(m->fsdata, &dir, leaf, VFS_DIR, &made);
    mutex_unlock(&m->lock);
    return rc;
}

int vfs_remove(const char *path)
{
    char leaf[VFS_NAME_MAX + 1];
    struct vfs_node dir;
    struct mount *m;
    int rc = walk_to(path, &m, &dir, leaf);

    if (rc != VFS_OK)
        return rc;
    if (!m->ops->remove)
        rc = VFS_EROFS;
    else if (!leaf[0])
        rc = VFS_EBUSY;                 /* a volume's root */
    else
        rc = m->ops->remove(m->fsdata, &dir, leaf);
    mutex_unlock(&m->lock);
    return rc;
}

int vfs_sync(void)
{
    int i, rc = VFS_OK;

    for (i = 0; i < VFS_MAX_MOUNTS; i++) {
        struct mount *m = &mounts[i];

        if (!m->ops || !m->ops->sync)
            continue;
        mutex_lock(&m->lock);
        if (m->ops && m->ops->sync(m->fsdata) != VFS_OK)
            rc = VFS_EIO;
        mutex_unlock(&m->lock);
    }
    return rc;
}

long vfs_seek(int handle, unsigned long offset)
{
    struct handle *h = get(handle, 0);

    if (!h)
        return VFS_EBADF;
    h->pos = offset;                    /* past the end is legal: reads return 0 */
    return (long)offset;
}

int vfs_readdir(int handle, struct vfs_dirent *out)
{
    struct handle *h = get(handle, 1);
    int rc;

    if (!h)
        return VFS_EBADF;
    mutex_lock(&h->mount->lock);
    rc = h->mount->ops->readdir(h->mount->fsdata, &h->node, &h->pos, out);
    mutex_unlock(&h->mount->lock);
    return rc;
}

int vfs_close(int handle)
{
    struct handle *h;
    struct mount *m;

    if (handle < 1 || handle > VFS_MAX_HANDLES || !handles[handle - 1].mount)
        return VFS_EBADF;
    h = &handles[handle - 1];
    m = h->mount;
    mutex_lock(&m->lock);
    if (h->writable && m->ops->sync)
        m->ops->sync(m->fsdata);        /* closing a written file is a promise */
    h->mount = 0;
    m->open_count--;
    mutex_unlock(&m->lock);
    return VFS_OK;
}

unsigned long vfs_handles_in_use(void)
{
    unsigned long n = 0;
    int i;

    for (i = 0; i < VFS_MAX_HANDLES; i++)
        if (handles[i].mount)
            n++;
    return n;
}

/* A task that exits with files open has them closed for it. */
static void task_gone(struct task *t)
{
    int i;

    for (i = 0; i < VFS_MAX_HANDLES; i++)
        if (handles[i].mount && handles[i].owner == t)
            vfs_close(i + 1);
}

void vfs_init(void)
{
    int i;

    for (i = 0; i < VFS_MAX_MOUNTS; i++)
        mounts[i].ops = 0;
    for (i = 0; i < VFS_MAX_HANDLES; i++)
        handles[i].mount = 0;
    for (i = 0; i < MAX_FS; i++)
        filesystems[i] = 0;
    task_on_exit(task_gone);
}
