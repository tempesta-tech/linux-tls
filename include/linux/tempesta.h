/**
 * Linux interface for Tempesta FW.
 *
 * Copyright (C) 2014 NatSys Lab. (info@natsys-lab.com).
 * Copyright (C) 2015-2016 Tempesta Technologies, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#ifndef __TEMPESTA_H__
#define __TEMPESTA_H__

#include <net/sock.h>

typedef void (*TempestaTxAction)(void);

typedef struct {
	int (*sk_alloc)(struct sock *sk);
	void (*sk_free)(struct sock *sk);
	int (*sock_tcp_rcv)(struct sock *sk, struct sk_buff *skb);
} TempestaOps;

typedef struct {
	unsigned long	addr;
	unsigned long	pages; /* number of 4KB pages */
} TempestaMapping;

/* Security hooks. */
int tempesta_new_clntsk(struct sock *newsk);
void tempesta_register_ops(TempestaOps *tops);
void tempesta_unregister_ops(TempestaOps *tops);

/* Network hooks. */
void tempesta_set_tx_action(TempestaTxAction action);
void tempesta_del_tx_action(void);

/* Memory management. */
void tempesta_reserve_pages(void);
void tempesta_reserve_vmpages(void);
int tempesta_get_mapping(int node, TempestaMapping **tm);

#endif /* __TEMPESTA_H__ */

