/*
 * dev.c - the device registry
 */
#include "dev.h"
#include "cpu.h"
#include "kstring.h"

static struct device *devices;

struct device *dev_find(const char *name)
{
    struct device *d;

    for (d = devices; d; d = d->next)
        if (str_eq(d->name, name))
            return d;
    return 0;
}

int dev_register(struct device *dev)
{
    struct device **link;
    int rc = -1;

    CRITICAL_ENTER();
    if (!dev_find(dev->name)) {
        for (link = &devices; *link; link = &(*link)->next)
            ;
        dev->next = 0;
        *link = dev;
        rc = 0;
    }
    CRITICAL_EXIT();
    return rc;
}

struct device *dev_next(const struct device *dev)
{
    return dev ? dev->next : devices;
}
