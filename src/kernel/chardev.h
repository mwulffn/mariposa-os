/*
 * chardev.h - character devices: byte streams
 *
 * The ops table of a DEV_CHAR device. Serial today; a console on a screen
 * later, which is the reason the console task talks to this and not to
 * serial.c.
 */
#ifndef CHARDEV_H
#define CHARDEV_H

#include "dev.h"

struct chardev_ops {
    /* Block until at least one byte is available, then return as many as
     * are waiting, up to len. Task context only. */
    long (*read)(struct device *dev, void *buf, unsigned long len);

    /* Queue len bytes for output. Any context. Returns len. */
    long (*write)(struct device *dev, const void *buf, unsigned long len);

    /* Bytes a read would return without blocking. */
    unsigned long (*rx_ready)(struct device *dev);
};

#define CHR_OPS(dev)  ((const struct chardev_ops *)(dev)->ops)

#define chr_read(dev, buf, len)   CHR_OPS(dev)->read((dev), (buf), (len))
#define chr_write(dev, buf, len)  CHR_OPS(dev)->write((dev), (buf), (len))
#define chr_rx_ready(dev)         CHR_OPS(dev)->rx_ready(dev)

#endif /* CHARDEV_H */
