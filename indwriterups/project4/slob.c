/*
 * SLOB Allocator: Simple List Of Blocks
 *
 * Matt Mackall <mpm@selenic.com> 12/30/03
 *
 * NUMA support by Paul Mundt, 2007.
 *
 * How SLOB works:
 *
 * The core of SLOB is a traditional K&R style heap allocator, with
 * support for returning aligned objects. The granularity of this
 * allocator is as little as 2 bytes, however typically most architectures
 * will require 4 bytes on 32-bit and 8 bytes on 64-bit.
 *
 * The slob heap is a set of linked list of pages from alloc_pages(),
 * and within each page, there is a singly-linked list of free blocks
 * (slob_t). The heap is grown on demand. To reduce fragmentation,
 * heap pages are segregated into three lists, with objects less than
 * 256 bytes, objects less than 1024 bytes, and all other objects.
 *
 * Allocation from heap involves first searching for a page with
 * sufficient free blocks (using a next-fit-like approach) followed by
 * a first-fit scan of the page. Deallocation inserts objects back
 * into the free list in address order, so this is effectively an
 * address-ordered first fit.
 *
 * Above this is an implementation of kmalloc/kfree. Blocks returned
 * from kmalloc are prepended with a 4-byte header with the kmalloc size.
 * If kmalloc is asked for objects of PAGE_SIZE or larger, it calls
 * alloc_pages() directly, allocating compound pages so the page order
 * does not have to be separately tracked, and also stores the exact
 * allocation size in page->private so that it can be used to accurately
 * provide ksize(). These objects are detected in kfree() because slob_page()
 * is false for them.
 *
 * SLAB is emulated on top of SLOB by simply calling constructors and
 * destructors for every SLAB allocation. Objects are returned with the
 * 4-byte alignment unless the SLAB_HWCACHE_ALIGN flag is set, in which
 * case the low-level allocator will fragment blocks to create the proper
 * alignment. Again, objects of page-size or greater are allocated by
 * calling alloc_pages(). As SLAB objects know their size, no separate
 * size bookkeeping is necessary and there is essentially no allocation
 * space overhead, and compound pages aren't needed for multi-page
 * allocations.
 *
 * NUMA support in SLOB is fairly simplistic, pushing most of the real
 * logic down to the page allocator, and simply doing the node accounting
 * on the upper levels. In the event that a node id is explicitly
 * provided, alloc_pages_exact_node() with the specified node id is used
 * instead. The common case (or when the node id isn't explicitly provided)
 * will default to the current node, as per numa_node_id().
 *
 * Node aware pages are still inserted in to the global freelist, and
 * these are scanned for by matching against the node id encoded in the
 * page flags. As a result, block allocations that can be satisfied from
 * the freelist will only be done so on pages residing on the same node,
 * in order to prevent random node placement.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/swap.h> /* struct reclaim_state */
#include <linux/cache.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/rcupdate.h>
#include <linux/list.h>
#include <linux/kmemleak.h>
#include "slob.h"

#include <trace/events/kmem.h>

#include <asm/atomic.h>

/*
 * slob_block has a field 'units', which indicates size of block if +ve,
 * or offset of next block if -ve (in SLOB_UNITs).
 *
 * Free blocks of size 1 unit simply contain the offset of the next block.
 * Those with larger size contain their size in the first SLOB_UNIT of
 * memory, and the offset of the next free block in the second SLOB_UNIT.
 */
#if PAGE_SIZE <= (32767 * 2)
typedef s16 slobidx_t;
#else
typedef s32 slobidx_t;
#endif

struct slob_block {
	slobidx_t units;
};
typedef struct slob_block slob_t;

/*
 * We use struct page fields to manage some slob allocation aspects,
 * however to avoid the horrible mess in include/linux/mm_types.h, we'll
 * just define our own struct page type variant here.
 */
struct slob_page {
	union {
		struct {
			unsigned long flags;	/* mandatory */
			atomic_t _count;	/* mandatory */
			slobidx_t units;	/* free units left in page */
			unsigned long pad[2];
			slob_t *free;		/* first free slob_t in page */
			struct list_head list;	/* linked list of free pages */
		};
		struct page page;
	};
};

// Implements Best Fit algorithm instead of First Fit
#define BEST_FIT 1

// Best Fit struct
// - Keeps track of the best fit slob/page
#if BEST_FIT
struct best_slob {
	struct slob_page *page;
	slob_t *prev;
	slob_t *slob;
	slob_t *next;
	slobidx_t size;
};
#endif

static inline void struct_slob_page_wrong_size(void)
{ BUILD_BUG_ON(sizeof(struct slob_page) != sizeof(struct page)); }

/*
 * free_slob_page: call before a slob_page is returned to the page allocator.
 */
static inline void free_slob_page(struct slob_page *sp)
{
	reset_page_mapcount(&sp->page);
	sp->page.mapping = NULL;
}

/*
 * All partially free slob pages go on these lists.
 */
#define SLOB_BREAK0 4
#define SLOB_BREAK1 8
#define SLOB_BREAK2 16
#define SLOB_BREAK3 32
#define SLOB_BREAK4 64
#define SLOB_BREAK5 128
#define SLOB_BREAK6 256
#define SLOB_BREAK7 512
#define SLOB_BREAK8 1024
#define SLOB_BREAK9 2048
static LIST_HEAD(free_slob_reallydamnsmall);
static LIST_HEAD(free_slob_femto);
static LIST_HEAD(free_slob_pico);
static LIST_HEAD(free_slob_nano);
static LIST_HEAD(free_slob_micro);
static LIST_HEAD(free_slob_small);
static LIST_HEAD(free_slob_smedium);
static LIST_HEAD(free_slob_medium);
static LIST_HEAD(free_slob_large);
static LIST_HEAD(free_slob_mega);
static LIST_HEAD(free_slob_giga);

/*
 * is_slob_page: True for all slob pages (false for bigblock pages)
 */
static inline int is_slob_page(struct slob_page *sp)
{
	return PageSlab((struct page *)sp);
}

static inline void set_slob_page(struct slob_page *sp)
{
	__SetPageSlab((struct page *)sp);
}

static inline void clear_slob_page(struct slob_page *sp)
{
	__ClearPageSlab((struct page *)sp);
}

static inline struct slob_page *slob_page(const void *addr)
{
	return (struct slob_page *)virt_to_page(addr);
}

/*
 * slob_page_free: true for pages on free_slob_pages list.
 */
static inline int slob_page_free(struct slob_page *sp)
{
	return PageSlobFree((struct page *)sp);
}

static void set_slob_page_free(struct slob_page *sp, struct list_head *list)
{
	list_add(&sp->list, list);
	__SetPageSlobFree((struct page *)sp);
}

static inline void clear_slob_page_free(struct slob_page *sp)
{
	list_del(&sp->list);
	__ClearPageSlobFree((struct page *)sp);
}

#define SLOB_UNIT sizeof(slob_t)
#define SLOB_UNITS(size) (((size) + SLOB_UNIT - 1)/SLOB_UNIT)
#define SLOB_ALIGN L1_CACHE_BYTES

/*
 * struct slob_rcu is inserted at the tail of allocated slob blocks, which
 * were created with a SLAB_DESTROY_BY_RCU slab. slob_rcu is used to free
 * the block using call_rcu.
 */
struct slob_rcu {
	struct rcu_head head;
	int size;
};

/*
 * slob_lock protects all slob allocator structures.
 */
static DEFINE_SPINLOCK(slob_lock);

/*
 * Encode the given size and next info into a free slob block s.
 */
static void set_slob(slob_t *s, slobidx_t size, slob_t *next)
{
	slob_t *base = (slob_t *)((unsigned long)s & PAGE_MASK);
	slobidx_t offset = next - base;

	if (size > 1) {
		s[0].units = size;
		s[1].units = offset;
	} else
		s[0].units = -offset;
}

/*
 * Return the size of a slob block.
 */
static slobidx_t slob_units(slob_t *s)
{
	if (s->units > 0)
		return s->units;
	return 1;
}

/*
 * Return the next free slob block pointer after this one.
 */
static slob_t *slob_next(slob_t *s)
{
	slob_t *base = (slob_t *)((unsigned long)s & PAGE_MASK);
	slobidx_t next;

	if (s[0].units < 0)
		next = -s[0].units;
	else
		next = s[1].units;
	return base+next;
}

/*
 * Returns true if s is the last free block in its page.
 */
static int slob_last(slob_t *s)
{
	return !((unsigned long)slob_next(s) & ~PAGE_MASK);
}

static void *slob_new_pages(gfp_t gfp, int order, int node)
{
	void *page;

#ifdef CONFIG_NUMA
	if (node != -1)
		page = alloc_pages_exact_node(node, gfp, order);
	else
#endif
		page = alloc_pages(gfp, order);

	if (!page)
		return NULL;

	return page_address(page);
}

static void slob_free_pages(void *b, int order)
{
	if (current->reclaim_state)
		current->reclaim_state->reclaimed_slab += 1 << order;
	free_pages((unsigned long)b, order);
}

long get_fragmentation(int which)
{
	int i;

	long page_total = 0;
	long page_free = 0;

	long free_total;
	long free_max;
	long pages = 0;

	struct slob_page *sp;
	struct list_head *slob_list;
	struct list_head *slob_lists_n_stuff[11];
	unsigned long flags;

	slob_lists_n_stuff[0] = &free_slob_reallydamnsmall;
	slob_lists_n_stuff[1] = &free_slob_femto;
	slob_lists_n_stuff[2] = &free_slob_pico;
	slob_lists_n_stuff[3] = &free_slob_nano;
	slob_lists_n_stuff[4] = &free_slob_micro;
	slob_lists_n_stuff[5] = &free_slob_small;
	slob_lists_n_stuff[6] = &free_slob_smedium;
	slob_lists_n_stuff[7] = &free_slob_medium;
	slob_lists_n_stuff[8] = &free_slob_large;
	slob_lists_n_stuff[9] = &free_slob_mega;
	slob_lists_n_stuff[10] = &free_slob_giga;

	free_max = 0;
	free_total = 0;

	for(i = 0; i < 11; i++) {
		slob_list = slob_lists_n_stuff[i];

		spin_lock_irqsave(&slob_lock, flags);
		list_for_each_entry(sp, slob_list, list) {
			slob_t *prev, *cur;
			page_total = 0;
			page_free = 0;

			for (prev = NULL, cur = sp->free; ; prev = cur,
					     cur = slob_next(cur)) {
					if (slob_units(cur) > page_free)
						page_free = slob_units(cur);

					page_total += slob_units(cur);

					if (slob_last(cur))
						break;
				}
			}

		++pages;
		free_max += page_free;
		free_total += page_total;
		printk(KERN_INFO "[SLOB] Page max : %ld", page_free);
		printk(KERN_INFO "[SLOB] Page total : %ld", page_total);

		spin_unlock_irqrestore(&slob_lock, flags);
	}
	printk(KERN_INFO "[SLOB] Pages : %ld", pages);
	printk(KERN_INFO "[SLOB] Free max : %ld", free_max);
	printk(KERN_INFO "[SLOB] Free total : %ld", free_total);
	if (which)
		return free_total * sizeof(slobidx_t) / pages;
	else
		return free_max * sizeof(slobidx_t) / pages;
}
EXPORT_SYMBOL(get_fragmentation);

asmlinkage long sys_get_free_total(void)
{
	return 1;
}

asmlinkage long sys_get_free_max(void)
{
	return 1;
}

#if BEST_FIT
static int best_fit(struct list_head *slob_list, size_t size, int align, struct slob_page *page, slob_t *best, slob_t *prev)
{
	//page stuff
	struct slob_page *sp;
	unsigned long flags;
	
	//block stuff
	slob_t *prev_slob, *cur_slob, *aligned_slob = NULL;
	int delta = 0, units = SLOB_UNITS(size);

	//best slob stuff
	struct best_slob bs;
	bs.size = 0;
	bs.page = NULL;
	bs.prev = NULL;
	bs.slob = NULL;
	bs.next = NULL;
	
	spin_lock_irqsave(&slob_lock, flags);
	list_for_each_entry(sp, slob_list, list) {
		prev_slob = NULL;
		cur_slob = NULL;
		aligned_slob = NULL;
		delta = 0;

		for (prev_slob = NULL, cur_slob = sp->free; ; prev_slob = cur_slob,
				cur_slob = slob_next(cur_slob)) {
			slobidx_t avail = slob_units(cur_slob);
			
			if (align) {
				aligned_slob = (slob_t *)ALIGN((unsigned long)cur_slob, align);
				delta = aligned_slob - cur_slob;
			}
			if (avail >= units + delta) { /* room enough? */
				if (avail < bs.size || bs.size == 0) {
					bs.size = avail;
					bs.slob = cur_slob;
					bs.prev = prev_slob;
					bs.next = slob_next(cur_slob);
					bs.page = sp;
					
					if (avail == units + delta)
						goto break_out;
				}
			}
			
			if (slob_last(cur_slob))
				break;
		}
	}
	
break_out:
	
	//move block to front of page
	//if (bs.slob && bs.prev) {
	//	set_slob(bs.prev, slob_units(bs.prev), bs.next);
	//	set_slob(bs.slob, slob_units(bs.slob), bs.page->free);
	//	bs.page->free = bs.slob;
	//}
	
	spin_unlock_irqrestore(&slob_lock, flags);
	
	if (!bs.slob) {
		return 0;
	}
	page = bs.page;
	prev = bs.prev;
	best = bs.slob;
	
	return 1;
}
#endif

/*
 * Allocate a slob block within a given slob_page sp.
 */
static void *slob_page_alloc(struct slob_page *sp, size_t size, int align, slob_t *best_slob, slob_t *prev_slob)
{
	slob_t *prev, *cur, *aligned = NULL;
	int delta = 0, units = SLOB_UNITS(size);
	
	if (prev_slob != NULL) {
		prev = prev_slob;
	} else {
		prev = NULL;
	}
	if (best_slob != NULL) {
		cur = best_slob;
	} else {
		cur = sp->free;
	}

	for (; ; prev = cur, cur = slob_next(cur)) {
		slobidx_t avail = slob_units(cur);

		if (align) {
			aligned = (slob_t *)ALIGN((unsigned long)cur, align);
			delta = aligned - cur;
		}
		if (avail >= units + delta) { /* room enough? */
			slob_t *next;

			if (delta) { /* need to fragment head to align? */
				next = slob_next(cur);
				set_slob(aligned, avail - delta, next);
				set_slob(cur, delta, aligned);
				prev = cur;
				cur = aligned;
				avail = slob_units(cur);
			}

			next = slob_next(cur);
			if (avail == units) { /* exact fit? unlink. */
				if (prev)
					set_slob(prev, slob_units(prev), next);
				else
					sp->free = next;
			} else { /* fragment */
				if (prev)
					set_slob(prev, slob_units(prev), cur + units);
				else
					sp->free = cur + units;
				set_slob(cur + units, avail - units, next);
			}

			sp->units -= units;
			if (!sp->units)
				clear_slob_page_free(sp);
			return cur;
		}
		if (slob_last(cur))
			return NULL;
	}
}

/*
 * slob_alloc: entry point into the slob allocator.
 */
static void *slob_alloc(size_t size, gfp_t gfp, int align, int node)
{
	struct slob_page *sp;
	struct list_head *prev;
	struct list_head *slob_list;
	slob_t *b = NULL;
	unsigned long flags;
	
	#if BEST_FIT
	struct slob_page *best_page = NULL;
	#endif
	slob_t *best_slob = NULL;
	slob_t *prev_slob = NULL;
	
	if (size < SLOB_BREAK0)
		slob_list = &free_slob_reallydamnsmall;
	else if (size < SLOB_BREAK1)
		slob_list = &free_slob_femto;
	else if (size < SLOB_BREAK2)
		slob_list = &free_slob_pico;
	else if (size < SLOB_BREAK3)
		slob_list = &free_slob_nano;
	else if (size < SLOB_BREAK4)
		slob_list = &free_slob_micro;
	else if (size < SLOB_BREAK5)
		slob_list = &free_slob_small;
	else if (size < SLOB_BREAK6)
		slob_list = &free_slob_smedium;
	else if (size < SLOB_BREAK7)
		slob_list = &free_slob_medium;
	else if (size < SLOB_BREAK8)
		slob_list = &free_slob_large;
	else if (size < SLOB_BREAK9)
		slob_list = &free_slob_mega;
	else
		slob_list = &free_slob_giga;

	#if BEST_FIT
	best_fit(slob_list, size, align, best_page, best_slob, prev_slob);
	if (!best_page)
		goto skip_loop;
	#endif
		
	spin_lock_irqsave(&slob_lock, flags);
	/* Iterate through each partially free page, try to find room */
	list_for_each_entry(sp, slob_list, list) {
	
	#if BEST_FIT
	sp = best_page;
	#endif
	
#ifdef CONFIG_NUMA
		/*
		 * If there's a node specification, search for a partial
		 * page with a matching node id in the freelist.
		 */
		if (node != -1 && page_to_nid(&sp->page) != node)
			continue;
#endif
		/* Enough room on this page? */
		if (sp->units < SLOB_UNITS(size))
			continue;

		/* Attempt to alloc */
		prev = sp->list.prev;
		b = slob_page_alloc(sp, size, align, best_slob, prev_slob);
		if (!b)
			continue;

		/* Improve fragment distribution and reduce our average
		 * search time by starting our next search here. (see
		 * Knuth vol 1, sec 2.5, pg 449) */
		if (prev != slob_list->prev &&
				slob_list->next != prev->next)
			list_move_tail(slob_list, prev->next);
		break;
	}
	spin_unlock_irqrestore(&slob_lock, flags);
	
#if BEST_FIT
skip_loop:
#endif
	/* Not enough space: must allocate a new page */
	if (!b) {
		b = slob_new_pages(gfp & ~__GFP_ZERO, 0, node);
		if (!b)
			return NULL;
		sp = slob_page(b);
		set_slob_page(sp);

		spin_lock_irqsave(&slob_lock, flags);
		sp->units = SLOB_UNITS(PAGE_SIZE);
		sp->free = b;
		INIT_LIST_HEAD(&sp->list);
		set_slob(b, SLOB_UNITS(PAGE_SIZE), b + SLOB_UNITS(PAGE_SIZE));
		set_slob_page_free(sp, slob_list);
		b = slob_page_alloc(sp, size, align, NULL, NULL);
		BUG_ON(!b);
		spin_unlock_irqrestore(&slob_lock, flags);
	}
	if (unlikely((gfp & __GFP_ZERO) && b))
		memset(b, 0, size);
	return b;
}

/*
 * slob_free: entry point into the slob allocator.
 */
static void slob_free(void *block, int size)
{
	struct slob_page *sp;
	slob_t *prev, *next, *b = (slob_t *)block;
	slobidx_t units;
	unsigned long flags;
	struct list_head *slob_list;

	if (unlikely(ZERO_OR_NULL_PTR(block)))
		return;
	BUG_ON(!size);

	sp = slob_page(block);
	units = SLOB_UNITS(size);

	spin_lock_irqsave(&slob_lock, flags);

	if (sp->units + units == SLOB_UNITS(PAGE_SIZE)) {
		/* Go directly to page allocator. Do not pass slob allocator */
		if (slob_page_free(sp))
			clear_slob_page_free(sp);
		spin_unlock_irqrestore(&slob_lock, flags);
		clear_slob_page(sp);
		free_slob_page(sp);
		slob_free_pages(b, 0);
		return;
	}

	if (!slob_page_free(sp)) {
		/* This slob page is about to become partially free. Easy! */
		sp->units = units;
		sp->free = b;
		set_slob(b, units,
			(void *)((unsigned long)(b +
					SLOB_UNITS(PAGE_SIZE)) & PAGE_MASK));
		if (size < SLOB_BREAK0)
			slob_list = &free_slob_reallydamnsmall;
		else if (size < SLOB_BREAK1)
			slob_list = &free_slob_femto;
		else if (size < SLOB_BREAK2)
			slob_list = &free_slob_pico;
		else if (size < SLOB_BREAK3)
			slob_list = &free_slob_nano;
		else if (size < SLOB_BREAK4)
			slob_list = &free_slob_micro;
		else if (size < SLOB_BREAK5)
			slob_list = &free_slob_small;
		else if (size < SLOB_BREAK6)
			slob_list = &free_slob_smedium;
		else if (size < SLOB_BREAK7)
			slob_list = &free_slob_medium;
		else if (size < SLOB_BREAK8)
			slob_list = &free_slob_large;
		else if (size < SLOB_BREAK9)
			slob_list = &free_slob_mega;
		else
			slob_list = &free_slob_giga;
		set_slob_page_free(sp, slob_list);
		goto out;
	}

	/*
	 * Otherwise the page is already partially free, so find reinsertion
	 * point.
	 */
	sp->units += units;

	if (b < sp->free) {
		if (b + units == sp->free) {
			units += slob_units(sp->free);
			sp->free = slob_next(sp->free);
		}
		set_slob(b, units, sp->free);
		sp->free = b;
	} else {
		prev = sp->free;
		next = slob_next(prev);
		while (b > next) {
			prev = next;
			next = slob_next(prev);
		}

		if (!slob_last(prev) && b + units == next) {
			units += slob_units(next);
			set_slob(b, units, slob_next(next));
		} else
			set_slob(b, units, next);

		if (prev + slob_units(prev) == b) {
			units = slob_units(b) + slob_units(prev);
			set_slob(prev, units, slob_next(b));
		} else
			set_slob(prev, slob_units(prev), b);
	}
out:
	spin_unlock_irqrestore(&slob_lock, flags);
}

/*
 * End of slob allocator proper. Begin kmem_cache_alloc and kmalloc frontend.
 */

void *__kmalloc_node(size_t size, gfp_t gfp, int node)
{
	unsigned int *m;
	int align = max(ARCH_KMALLOC_MINALIGN, ARCH_SLAB_MINALIGN);
	void *ret;

	lockdep_trace_alloc(gfp);

	if (size < PAGE_SIZE - align) {
		if (!size)
			return ZERO_SIZE_PTR;

		m = slob_alloc(size + align, gfp, align, node);

		if (!m)
			return NULL;
		*m = size;
		ret = (void *)m + align;

		trace_kmalloc_node(_RET_IP_, ret,
				   size, size + align, gfp, node);
	} else {
		unsigned int order = get_order(size);

		if (likely(order))
			gfp |= __GFP_COMP;
		ret = slob_new_pages(gfp, order, node);
		if (ret) {
			struct page *page;
			page = virt_to_page(ret);
			page->private = size;
		}

		trace_kmalloc_node(_RET_IP_, ret,
				   size, PAGE_SIZE << order, gfp, node);
	}

	kmemleak_alloc(ret, size, 1, gfp);
	return ret;
}
EXPORT_SYMBOL(__kmalloc_node);

void kfree(const void *block)
{
	struct slob_page *sp;

	trace_kfree(_RET_IP_, block);

	if (unlikely(ZERO_OR_NULL_PTR(block)))
		return;
	kmemleak_free(block);

	sp = slob_page(block);
	if (is_slob_page(sp)) {
		int align = max(ARCH_KMALLOC_MINALIGN, ARCH_SLAB_MINALIGN);
		unsigned int *m = (unsigned int *)(block - align);
		slob_free(m, *m + align);
	} else
		put_page(&sp->page);
}
EXPORT_SYMBOL(kfree);

/* can't use ksize for kmem_cache_alloc memory, only kmalloc */
size_t ksize(const void *block)
{
	struct slob_page *sp;

	BUG_ON(!block);
	if (unlikely(block == ZERO_SIZE_PTR))
		return 0;

	sp = slob_page(block);
	if (is_slob_page(sp)) {
		int align = max(ARCH_KMALLOC_MINALIGN, ARCH_SLAB_MINALIGN);
		unsigned int *m = (unsigned int *)(block - align);
		return SLOB_UNITS(*m) * SLOB_UNIT;
	} else
		return sp->page.private;
}
EXPORT_SYMBOL(ksize);

struct kmem_cache {
	unsigned int size, align;
	unsigned long flags;
	const char *name;
	void (*ctor)(void *);
};

struct kmem_cache *kmem_cache_create(const char *name, size_t size,
	size_t align, unsigned long flags, void (*ctor)(void *))
{
	struct kmem_cache *c;

	c = slob_alloc(sizeof(struct kmem_cache),
		GFP_KERNEL, ARCH_KMALLOC_MINALIGN, -1);

	if (c) {
		c->name = name;
		c->size = size;
		if (flags & SLAB_DESTROY_BY_RCU) {
			/* leave room for rcu footer at the end of object */
			c->size += sizeof(struct slob_rcu);
		}
		c->flags = flags;
		c->ctor = ctor;
		/* ignore alignment unless it's forced */
		c->align = (flags & SLAB_HWCACHE_ALIGN) ? SLOB_ALIGN : 0;
		if (c->align < ARCH_SLAB_MINALIGN)
			c->align = ARCH_SLAB_MINALIGN;
		if (c->align < align)
			c->align = align;
	} else if (flags & SLAB_PANIC)
		panic("Cannot create slab cache %s\n", name);

	kmemleak_alloc(c, sizeof(struct kmem_cache), 1, GFP_KERNEL);
	return c;
}
EXPORT_SYMBOL(kmem_cache_create);

void kmem_cache_destroy(struct kmem_cache *c)
{
	kmemleak_free(c);
	if (c->flags & SLAB_DESTROY_BY_RCU)
		rcu_barrier();
	slob_free(c, sizeof(struct kmem_cache));
}
EXPORT_SYMBOL(kmem_cache_destroy);

void *kmem_cache_alloc_node(struct kmem_cache *c, gfp_t flags, int node)
{
	void *b;

	if (c->size < PAGE_SIZE) {
		b = slob_alloc(c->size, flags, c->align, node);
		trace_kmem_cache_alloc_node(_RET_IP_, b, c->size,
					    SLOB_UNITS(c->size) * SLOB_UNIT,
					    flags, node);
	} else {
		b = slob_new_pages(flags, get_order(c->size), node);
		trace_kmem_cache_alloc_node(_RET_IP_, b, c->size,
					    PAGE_SIZE << get_order(c->size),
					    flags, node);
	}

	if (c->ctor)
		c->ctor(b);

	kmemleak_alloc_recursive(b, c->size, 1, c->flags, flags);
	return b;
}
EXPORT_SYMBOL(kmem_cache_alloc_node);

static void __kmem_cache_free(void *b, int size)
{
	if (size < PAGE_SIZE)
		slob_free(b, size);
	else
		slob_free_pages(b, get_order(size));
}

static void kmem_rcu_free(struct rcu_head *head)
{
	struct slob_rcu *slob_rcu = (struct slob_rcu *)head;
	void *b = (void *)slob_rcu - (slob_rcu->size - sizeof(struct slob_rcu));

	__kmem_cache_free(b, slob_rcu->size);
}

void kmem_cache_free(struct kmem_cache *c, void *b)
{
	kmemleak_free_recursive(b, c->flags);
	if (unlikely(c->flags & SLAB_DESTROY_BY_RCU)) {
		struct slob_rcu *slob_rcu;
		slob_rcu = b + (c->size - sizeof(struct slob_rcu));
		slob_rcu->size = c->size;
		call_rcu(&slob_rcu->head, kmem_rcu_free);
	} else {
		__kmem_cache_free(b, c->size);
	}

	trace_kmem_cache_free(_RET_IP_, b);
}
EXPORT_SYMBOL(kmem_cache_free);

unsigned int kmem_cache_size(struct kmem_cache *c)
{
	return c->size;
}
EXPORT_SYMBOL(kmem_cache_size);

int kmem_cache_shrink(struct kmem_cache *d)
{
	return 0;
}
EXPORT_SYMBOL(kmem_cache_shrink);

static unsigned int slob_ready __read_mostly;

int slab_is_available(void)
{
	return slob_ready;
}

void __init kmem_cache_init(void)
{
	slob_ready = 1;
}

void __init kmem_cache_init_late(void)
{
	/* Nothing to do */
}
