/**
 *		Tempesta Memory Reservation
 *
 * Copyright (C) 2015-2018 Tempesta Technologies, Inc.
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
#include <linux/gfp.h>
#include <linux/hugetlb.h>
#include <linux/tempesta.h>
#include <linux/topology.h>
#include <linux/vmalloc.h>

#include "internal.h"

#define MAX_PGORDER		16	/* 128GB per one table */
#define MIN_PGORDER		4	/* 32MB */
#define DEFAULT_PGORDER		8	/* 512MB */
/* Modern processors support up to 1.5TB of RAM, be ready for 2TB. */
#define GREEDY_ARNUM		(1024 * 1024 + 1)
#define PGNUM			(1 << pgorder)
#define PGNUM4K			(PGNUM * (1 << HUGETLB_PAGE_ORDER))

static int pgorder = DEFAULT_PGORDER;
static gfp_t gfp_f = GFP_HIGHUSER | __GFP_COMP | __GFP_THISNODE | __GFP_ZERO
		     | __GFP_RETRY_MAYFAIL;
static TempestaMapping map[MAX_NUMNODES];
/*
 * Modern x86-64 has not more than 512GB RAM per physical node.
 * This is very large amount of memory, but it will be freed when
 * initialization phase ends.
 */
static struct page *greedy[GREEDY_ARNUM] __initdata = { 0 };

static int __init
tempesta_setup_pages(char *str)
{
	get_option(&str, &pgorder);
	if (pgorder < MIN_PGORDER) {
		pr_err("Tempesta: bad dbmem value %d, must be [%d:%d]\n",
		       pgorder, MIN_PGORDER, MAX_PGORDER);
		pgorder = MIN_PGORDER;
	}
	if (pgorder > MAX_PGORDER) {
		pr_err("Tempesta: bad dbmem value %d, must be [%d:%d]\n",
		       pgorder, MIN_PGORDER, MAX_PGORDER);
		pgorder = MAX_PGORDER;
	}

	return 1;
}
__setup("tempesta_dbmem=", tempesta_setup_pages);

/**
 * The code is somewhat stollen from mm/hugetlb.c.
 */
static struct page *
tempesta_alloc_hpage(int nid)
{
	struct page *p;

	p = alloc_pages_node(nid, gfp_f, HUGETLB_PAGE_ORDER);
	if (!p)
		return NULL;

	count_vm_event(HTLB_BUDDY_PGALLOC);

	__ClearPageReserved(p);
	prep_compound_page(p, HUGETLB_PAGE_ORDER);

	/* Acquire the page immediately. */
	set_page_refcounted(p);

	return p;
}

static void
tempesta_free_hpage(struct page *p)
{
	__free_pages(p, HUGETLB_PAGE_ORDER);
}

/**
 * Greedely alloc huge pages and try to find continous region organized
 * by sorted set of allocated pages. When the region is found, all pages
 * out of it are returned to system.
 */
static struct page *
tempesta_alloc_contmem(int nid)
{
	long min = -1, start = -1, curr = 0, end = -1, max = -1;
	struct page *p;

	while (1) {
		p = tempesta_alloc_hpage(nid);
		if (!p)
			goto err;
		curr = ((long)page_address(p) - PAGE_OFFSET) >> HPAGE_SHIFT;
		/*
		 * The first kernel mapped page is always reserved.
		 * Keep untouched (zero) bounds for faster lookups.
		 */
		BUG_ON(curr < 1 || curr >= GREEDY_ARNUM);
		greedy[curr] = p;

		/* First time initialization. */
		if (min < 0) {
			min = start = end = max = curr;
		} else {
			/* Update bounds for faster pages return. */
			if (min > curr)
				min = curr;
			if (max < curr)
				max = curr;
			/* Update continous memory segment bounds. */
			if (curr == end + 1) {
				while (end <= max && greedy[end + 1])
					++end;
			}
			else if (curr + 1 == start) {
				while (start >= min && greedy[start - 1])
					--start;
			}
			else {
				/* Try to find new continous segment. */
				long i, d_max = 0, good_start = start = min;
				for (i = min; i <= max; ++i) {
					if (greedy[i]) {
						if (start == -1)
							start = i;
						end = i;
						if (i - start + 1 == PGNUM)
							break;
						continue;
					}

					if (start > 0 && end - start > d_max) {
						good_start = start;
						d_max = end - start;
					}
					start = -1;
				}
				if (end - start < d_max) {
					start = good_start;
					end = start + d_max;
				}
			}
		}

		if (end - start + 1 == PGNUM)
			break; /* continous space is built! */
	}

	/* Return unnecessary pages. */
	BUG_ON(min < 0 || start < 0 || end < 0 || max < 0);
	for ( ; min < start; ++min)
		if (greedy[min]) {
			tempesta_free_hpage(greedy[min]);
			greedy[min] = NULL;
		}
	for ( ; max > end; --max)
		if (greedy[max]) {
			tempesta_free_hpage(greedy[max]);
			greedy[max] = NULL;
		}
	return greedy[start];

err:
	pr_err("Tempesta: cannot allocate %u continous huge pages at node"
	       " %d\n", PGNUM, nid);
	for ( ; min >= 0 && min <= max; ++min)
		if (greedy[min]) {
			tempesta_free_hpage(greedy[min]);
			greedy[min] = NULL;
		}
	return NULL;
}

/**
 * Allocate continous virtual space of huge pages for Tempesta.
 * We do not use giantic 1GB pages since not all modern x86-64 CPUs
 * allows them in virtualized mode.
 *
 * TODO try firstly to allocate giantic pages, next huge pages and finally
 * fallback to common 4KB pages allocation if previous tries failed.
 */
void __init
tempesta_reserve_pages(void)
{
	int nid;
	struct page *p;

	for_each_online_node(nid) {
		p = tempesta_alloc_contmem(nid);
		if (!p)
			goto err;

		map[nid].addr = (unsigned long)page_address(p);
		map[nid].pages = PGNUM4K;

		pr_info("Tempesta: allocated huge pages space %p %luMB at node"
			" %d\n", page_address(p),
			PGNUM4K * PAGE_SIZE / (1024 * 1024), nid);
	}

	return;
err:
	for_each_online_node(nid) {
		struct page *pend;
		if (!map[nid].addr)
			continue;
		for (p = virt_to_page(map[nid].addr), pend = p + PGNUM4K;
		     p < pend; p += 1 << HUGETLB_PAGE_ORDER)
			tempesta_free_hpage(p);
	}
	memset(map, 0, sizeof(map));
}

/**
 * Allocates necessary space if tempesta_reserve_pages() failed.
 */
void __init
tempesta_reserve_vmpages(void)
{
	int nid, maps = 0;
	size_t vmsize = PGNUM * (1 << HPAGE_SHIFT);

	for_each_online_node(nid)
		maps += !!map[nid].addr;

	BUG_ON(maps && maps < nr_online_nodes);
	if (maps == nr_online_nodes)
		return;

	for_each_online_node(nid) {
		pr_warn("Tempesta: allocate %u vmalloc pages at node %d\n",
			PGNUM4K, nid);

		map[nid].addr = (unsigned long)vzalloc_node(vmsize, nid);
		if (!map[nid].addr)
			goto err;
		map[nid].pages = PGNUM4K;
	}

	return;
err:
	pr_err("Tempesta: cannot vmalloc area of %lu bytes at node %d\n",
	       vmsize, nid);
	for_each_online_node(nid)
		if (map[nid].addr)
			vfree((void *)map[nid].addr);
	memset(map, 0, sizeof(map));
}

int
tempesta_get_mapping(int nid, TempestaMapping **tm)
{
	if (unlikely(!map[nid].addr))
		return -ENOMEM;

	*tm = &map[nid];

	return 0;
}
EXPORT_SYMBOL(tempesta_get_mapping);

