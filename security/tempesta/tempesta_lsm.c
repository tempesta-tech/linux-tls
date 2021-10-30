/**
 *		Tempesta FW
 *
 * Copyright (C) 2014 NatSys Lab. (info@natsys-lab.com).
 * Copyright (C) 2015-2021 Tempesta Technologies, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#include <linux/ipv6.h>
#include <linux/lsm_hooks.h>
#include <linux/spinlock.h>
#include <linux/tempesta.h>

static TempestaOps __rcu *tempesta_ops = NULL;
static DEFINE_SPINLOCK(tops_lock);

void
tempesta_register_ops(TempestaOps *tops)
{
	spin_lock(&tops_lock);

	BUG_ON(tempesta_ops);

	rcu_assign_pointer(tempesta_ops, tops);

	spin_unlock(&tops_lock);
}
EXPORT_SYMBOL(tempesta_register_ops);

void
tempesta_unregister_ops(TempestaOps *tops)
{
	spin_lock(&tops_lock);

	BUG_ON(tempesta_ops != tops);

	rcu_assign_pointer(tempesta_ops, NULL);

	spin_unlock(&tops_lock);

	/*
	 * tempesta_ops is called in softirq only, so if there are some users
	 * of the structures then they are active on their CPUs.
	 * After the below we can be sure that nobody refers @tops and we can
	 * go forward and destroy it.
	 */
	synchronize_rcu();
}
EXPORT_SYMBOL(tempesta_unregister_ops);

int
tempesta_new_clntsk(struct sock *newsk)
{
	int r = 0;

	TempestaOps *tops;

	WARN_ON(newsk->sk_security);

	rcu_read_lock();

	tops = rcu_dereference(tempesta_ops);
	if (likely(tops))
		r = tops->sk_alloc(newsk);

	rcu_read_unlock();

	return r;
}
EXPORT_SYMBOL(tempesta_new_clntsk);

static void
tempesta_sk_free(struct sock *sk)
{
	TempestaOps *tops;

	if (!sk->sk_security)
		return;

	rcu_read_lock();

	tops = rcu_dereference(tempesta_ops);
	if (likely(tops))
		tops->sk_free(sk);

	rcu_read_unlock();
}

static int
tempesta_sock_tcp_rcv(struct sock *sk, struct sk_buff *skb)
{
	int r = 0;
	TempestaOps *tops;

	rcu_read_lock();

	tops = rcu_dereference(tempesta_ops);
	if (likely(tops)) {
		if (skb->protocol == htons(ETH_P_IP))
			r = tops->sock_tcp_rcv(sk, skb);
	}

	rcu_read_unlock();

	return r;
}

static struct security_hook_list tempesta_hooks[] __read_mostly = {
	LSM_HOOK_INIT(sk_free_security, tempesta_sk_free),
	LSM_HOOK_INIT(socket_sock_rcv_skb, tempesta_sock_tcp_rcv),
};

static __init int
tempesta_init(void)
{
	security_add_hooks(tempesta_hooks, ARRAY_SIZE(tempesta_hooks),
			   "tempesta");

	return 0;
}

DEFINE_LSM(smack) = {
	.name = "tempesta",
	.flags = LSM_FLAG_LEGACY_MAJOR | LSM_FLAG_EXCLUSIVE,
	.init = tempesta_init,
};
