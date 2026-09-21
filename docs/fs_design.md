# Storage and Filesystems

> **Status.** Implemented: the kernel block layer (`blk.c`, `ide.c`), the
> block cache (`bcache.c`), the VFS (`vfs.c`) and read-only FAT16
> (`fs_fat16.c` over the shared `fat16.c`), pinned by `kblk.*` and `kvfs.*`;
> the console has `mount`, `ls` and `cat`. **Not yet:** ext2 (read-write in
> the kernel, read in the ROM), writing of any kind through the VFS, and
> interrupt-driven IDE.

## The plan

| Where | Filesystems |
|---|---|
| ROM | FAT16 read, ext2 read - enough to find and load the kernel, nothing more |
| Kernel | FAT16 read; ext2 read-write, the default system filesystem; others behind the VFS later (PFS3 is the one worth having) |

The ROM is ours, so nothing forces FAT16 to be special: the boot partition
may be either. ext2 is the default because it is simple, thoroughly
documented, and every modern host can build, check and mount it, which
matters most while there is no userspace to do any of that here. PFS3 is the
better filesystem for this machine - atomic commit, extents, Amiga metadata -
and is the intended second system filesystem once the VFS has been proven by
a first.

## Layers

```
console, loader, ...       vfs_open / vfs_read / vfs_readdir / ...
vfs.c                      volumes, paths, handles, the per-mount lock
fs_fat16.c  fs_ext2.c      filesystem drivers: struct fs_ops
bcache.c                   the block cache
blk.c                      block devices and partitions (RDB)
ide.c  ->  shared/ata.c    Gayle IDE, PIO
```

### Block devices

`struct blkdev` (shared with the ROM) gains `write` and a block count. The
kernel registers `ide0` and one device per RDB partition - `ide0p0`,
`ide0p1` - following `PART_NEXT` to the end of the list, which the ROM's
boot path never did. A partition is a window onto its parent: block 0 of
`ide0p1` is wherever the RDB says, and a read past its end fails rather than
reading the neighbour.

**IDE is polled PIO, and serialised by a mutex.** A transfer is a sequence
of register writes and a data loop; two tasks interleaving them would
corrupt both. This is the first user of a sleeping lock
(`docs/task_design.md`). Sleeping on the drive's interrupt instead of
polling its status is the obvious next step and needs Gayle's interrupt
registers; on a CF card, which answers at once, it buys little.

**Known issue: IDENTIFY DEVICE returns nothing under FS-UAE.** The drive
raises DRQ and then hands over 512 zero bytes - with either drive-select
value, from either data port address - while READ SECTORS through the same
loop works. So `ide0` has an unknown size there (`blocks` is 0, which means
"unknown", and the whole disk gets no bounds check); partitions take their
size from the RDB and are unaffected. The harness models IDENTIFY and
`kblk.disk_found_and_sized` passes against it, so this wants a real drive to
say which of the two is telling the truth.

### The block cache

Gayle's IDE is PIO with no DMA - every word of every sector goes through
the CPU - so a block read twice is a cost paid twice. Commodore could not
spend RAM on this. This machine can: fast RAM spent to save the slowest
thing in it (`docs/driver_design.md`).

- 512-byte blocks, keyed by (device, block).
- **Sized from free memory, allocated at `bc_init`**: an eighth of what is
  free, fast RAM for preference, between 64 and 4096 blocks - about 115KB on
  a 1MB machine, about a megabyte on an 8MB one. It was a static 128KB array
  at first, which fixed the size whatever the machine had, hid the memory
  from every report, and grew the kernel image far enough to land on top of
  the test heaps. With no memory to be had the cache is off, and reads and
  writes go straight through.
- **`bc_read` coalesces.** A run of missing blocks is fetched with one ATA
  command, not one per block - the per-command overhead is what hurts.
- **Writes are write-through.** `bc_write` goes to the disk before it
  returns, and updates the cache. Delayed writes would be faster and make
  the order in which metadata reaches the disk - the thing crash safety
  rests on - the cache's decision instead of the filesystem's. That trade
  is not taken until a filesystem asks for it, with its eyes open.
- Least recently *used*, kept as a list so that the buffer to replace is
  always the tail: scanning a few thousand buffers on every miss would cost
  more than the disk read being avoided. Hit and miss counters, because a
  cache whose effect cannot be seen cannot be tuned.

Filesystem drivers never touch a block device; they get a `struct blkdev`
whose `read` and `write` go through the cache. That is how the shared
`fat16.c`, written for the ROM with no cache in mind, runs cached in the
kernel without a line changed.

### The VFS

**Names are Amiga-shaped: `volume:dir/file`.** A volume is a mounted
filesystem with a name - `boot:`, `sys:` - and there is no single root that
everything must hang from. A path with no volume is relative to the
caller's current volume, when there is such a thing.

**Lookup is case-insensitive and case-preserving**, as on AmigaOS. On a
case-sensitive filesystem (ext2) that means a directory can hold `Readme`
and `README` if a host tool put them there; lookup takes an exact match
first and otherwise the first match in directory order, and creating a name
that differs from an existing one only by case is refused.

**Handles, not pointers.** `vfs_open` returns a small integer; every call
validates it; a handle belongs to a task and is closed when the task exits
(`task_on_exit`). Same reasons as bitmaps: native libraries and Wasm host
calls will sit on top of this.

**One lock per mount.** Every operation on a volume holds that volume's
mutex. It is coarse, it is obviously correct, and the disk is the
bottleneck, not the lock.

A filesystem driver is a `struct fs_ops`: `mount`, `unmount`, `lookup`,
`read`, `readdir`, `stat`, and for a writable one `write`, `create`,
`remove`, `mkdir`, `truncate`, `sync`. A node is whatever the driver needs
it to be, opaque to the VFS.

## FAT16

Read-only, 8.3 names, subdirectories. Long file names are ignored: their
directory entries are skipped, and the 8.3 alias is what is seen. It exists
for the boot partition and for exchanging files with other machines, not as
somewhere to live.

## ext2 (not yet)

Revision 1, no optional features: no htree, no extents, no journal. Crash
safety in three steps - ordered metadata writes and a dirty flag now, host
`e2fsck` during development, an ext3-compatible journal when it is wanted.
Amiga protection bits and file comments, if wanted, go in extended
attributes.
