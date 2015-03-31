/*
 * Shortest Seek Time First (SSTF)
 * Project 2 - CS 411
 *
 */

#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>

#define SSTF_NEXT 0
#define SSTF_PREV 1

struct sstf_data {
	char direction;
	sector_t prevsec;
	struct list_head queue;
};

static void sstf_merged_requests(struct request_queue *q, struct request *rq,
				 struct request *next)
{
	list_del_init(&next->queuelist);
}

static int sstf_dispatch(struct request_queue *q, int force)
{
	struct sstf_data *nd = q->elevator->elevator_data;

	if (!list_empty(&nd->queue)) 
	{
		struct request *rq_next;
		struct list_head *next_head;
		
		printk(KERN_DEBUG "[SSTF] Last request sector: %lu\n", (unsigned long) nd->prevsec);
		
		if (nd->direction == SSTF_NEXT) 
			{
				printk(KERN_DEBUG "[SSTF] Increasing\n");
				list_for_each(next_head, &nd->queue) 
					{
						rq_next = list_entry_rq(next_head);
						if (blk_rq_pos(rq_next) > nd->prevsec) 
						{
							goto dispatch;
						}
					}
				printk(KERN_EMERG "[SSTF] No new requests in current direction, changing directions\n");
				nd->direction = SSTF_PREV;
				rq_next = list_entry_rq(next_head->prev);
			} 
		else 
			{
				printk(KERN_DEBUG "[SSTF] Decreasing\n");
				list_for_each_prev(next_head, &nd->queue) 
					{
						rq_next = list_entry_rq(next_head);
						if (blk_rq_pos(rq_next) < nd->prevsec) 
						{
							goto dispatch;
						}
					}
				printk(KERN_EMERG "[SSTF] No new requests in current direction, changing directions\n");
				nd->direction = SSTF_NEXT;
				rq_next = list_entry_rq(next_head->next);
			}
dispatch:
		printk(KERN_EMERG "[SSTF] Dispatching request to sector: %lu Difference: %lld\n"
				, (unsigned long) blk_rq_pos(rq_next)
				, (long long) blk_rq_pos(rq_next) - nd->prevsec);
		nd->prevsec = blk_rq_pos(rq_next);
		list_del_init(&rq_next->queuelist);

		elv_dispatch_add_tail(q, rq_next);
		return 1;
	}
	printk(KERN_DEBUG "[SSTF] Request queue empty\n");
	return 0;
}

static void sstf_add_request(struct request_queue *q, struct request *rq)
{
	struct sstf_data *nd = q->elevator->elevator_data;
	struct list_head *lo = &nd->queue;

	list_for_each_prev(lo, &nd->queue) {
		struct request *pos = list_entry_rq(lo);
		if (blk_rq_pos(pos) < blk_rq_pos(rq)) {
			break;
		}
	}

	printk(KERN_DEBUG "[SSTF] Added request at %lu\n"
	       , (unsigned long) blk_rq_pos(rq));

	list_add(&rq->queuelist, lo);
	
	/*
	 * Prints entire request queue on every request added to request queue
	 * event.
	 */
	/*
	printk(KERN_DEBUG "[SSTF] Request queue:");
	list_for_each(lo, &nd->queue) {
		struct request *pos = list_entry_rq(lo);
		printk(KERN_DEBUG "%lu,", (unsigned long) blk_rq_pos(pos));
	}
	*/
}

static struct request * sstf_former_request(struct request_queue *q, struct request *rq)
{
	struct sstf_data *nd = q->elevator->elevator_data;

	if (rq->queuelist.prev == &nd->queue)
		return NULL;
	return list_entry(rq->queuelist.prev, struct request, queuelist);
}

static struct request *
sstf_latter_request(struct request_queue *q, struct request *rq)
{
	struct sstf_data *nd = q->elevator->elevator_data;

	if (rq->queuelist.next == &nd->queue)
		return NULL;
	return list_entry(rq->queuelist.next, struct request, queuelist);
}

static void *sstf_init_queue(struct request_queue *q)
{
	struct sstf_data *nd;

	nd = kmalloc_node(sizeof(*nd), GFP_KERNEL, q->node);
	if (!nd)
		return NULL;
	INIT_LIST_HEAD(&nd->queue);
	nd->prevsec = 0;
	nd->direction = SSTF_NEXT;
	return nd;
}

static void sstf_exit_queue(struct elevator_queue *e)
{
	struct sstf_data *nd = e->elevator_data;

	BUG_ON(!list_empty(&nd->queue));
	kfree(nd);
}

static struct elevator_type elevator_sstf = {
	.ops = {
		.elevator_merge_req_fn		= sstf_merged_requests,
		.elevator_dispatch_fn		= sstf_dispatch,
		.elevator_add_req_fn		= sstf_add_request,
		.elevator_former_req_fn		= sstf_former_request,
		.elevator_latter_req_fn		= sstf_latter_request,
		.elevator_init_fn		= sstf_init_queue,
		.elevator_exit_fn		= sstf_exit_queue,
	},
	.elevator_name = "sstf",
	.elevator_owner = THIS_MODULE,
};

static int __init sstf_init(void)
{
	elv_register(&elevator_sstf);

	return 0;
}

static void __exit sstf_exit(void)
{
	elv_unregister(&elevator_sstf);
}

module_init(sstf_init);
module_exit(sstf_exit);


MODULE_AUTHOR("Nick, Li, Josh, Chavis");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SSTF IO scheuler");
