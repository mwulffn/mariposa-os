/*
 * dev.h - the device registry
 *
 * See docs/driver_design.md. Devices are typed: a class says which ops
 * table `ops` points at. The registry exists so that "a console on ser0"
 * can be said without naming the driver that provides it.
 *
 * A struct device belongs to its driver, which is normally a static. The
 * registry links them together and allocates nothing, so a driver can
 * register before the allocator is up.
 */
#ifndef DEV_H
#define DEV_H

#define DEV_CHAR   1    /* ops is a struct chardev_ops, chardev.h */
#define DEV_BLOCK  2
#define DEV_INPUT  3

struct device {
    const char    *name;
    unsigned long  class;
    const void    *ops;
    void          *hw;      /* the driver's own state */
    struct device *next;    /* the registry's */
};

/* 0, or -1 if the name is taken - which includes registering twice. */
int dev_register(struct device *dev);

/* Exact name match. NULL if there is none. */
struct device *dev_find(const char *name);

/* Walk the registry: pass NULL for the first. Oldest registration first. */
struct device *dev_next(const struct device *dev);

#endif /* DEV_H */
