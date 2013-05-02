/*
 * Copyright (c) 2000 Blue Mug, Inc.  All Rights Reserved.
 */
#ifndef _SHOEHORN_ETH_H
#define _SHOEHORN_ETH_H

extern void eth_open(const char *netif);
extern void eth_write(const void *buf, size_t count);
extern void eth_close(void);

#endif /* _SHOEHORN_ETH_H */
