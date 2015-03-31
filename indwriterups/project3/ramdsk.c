/*
 * The majority of this source code is derived from Linux Device Drivers, 3rd
 * edition, by Jonathan Corbet, Alessandro Rubini, and Greg Kroah-Hartman. In
 * particular, chapter 16 "Block Drivers" was extensively referenced.
 */

/*
 * This is the source file for the project 3 encrypted ram disk. That means we
 * we develop shit here.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/crypto.h>
#include <linux/scatterlist.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/kernel.h>
#include <linux/vmalloc.h>
#include <linux/blkdev.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/hdreg.h>
#include <linux/kdev_t.h>
#include <linux/fcntl.h>

#define DISK_SIZE_MEGS 1
#define NUM_DISKS 1
#define RAMDSK_MINORS 16
#define KERNEL_SECTOR_SIZE 512

static int ramdsk_major = 0;
module_param(ramdsk_major, int, 0);
static int hardsect_size = 512;
module_param(hardsect_size, int, 0);
static int nsectors = DISK_SIZE_MEGS * 2048; /* How big the drive is */
module_param(nsectors, int, 0);
static int ndevices = NUM_DISKS;
module_param(ndevices, int, 0);

// Encryption params
#define RAMDSK_ENCRYPTION 1

// Encryption algorithm
static char *algorithm = "ecb(aes)";
module_param(algorithm, charp, S_IRUGO);

// Key used for crypto

/* Change the key:
 * echo x > /sys/module/ramdsk/parameters/key
 * where x is a 16 character key
 */
static char *key = "group_nine_rules";
module_param(key, charp, 0644);

struct ramdsk_dev {
	int size;
	u8 *data;
	spinlock_t lock;
	struct request_queue *queue;
	struct gendisk *gd;
};

static struct ramdsk_dev *ramdsk_device;

static void fill_sgl(struct scatterlist *sgl, unsigned long sgl_l, 
		     char *buffer, unsigned long buf_l) {
	struct scatterlist *s;
	unsigned long i;
	unsigned long page_off = 0;
	printk(KERN_INFO "[RAMDSK] fill_sgl start");
	for_each_sg(sgl, s, sgl_l, i) {
		unsigned int size = PAGE_SIZE;
		if(page_off + PAGE_SIZE > buf_l)
			size = buf_l - page_off;
		sg_set_buf(s, buffer + page_off, size);
		page_off += PAGE_SIZE;
	}
	printk(KERN_INFO "[RAMDSK] fill_sgl end");
}

static void ramdsk_transfer(struct ramdsk_dev *dev, unsigned long sector,
                            unsigned long nsect, char *buffer, int datadir)
{
	unsigned long offset = sector * KERNEL_SECTOR_SIZE;
	unsigned long nbytes = nsect * KERNEL_SECTOR_SIZE;

	struct crypto_blkcipher *tfm;
	struct blkcipher_desc desc;

	struct scatterlist *sg_in;      // Source 
	struct scatterlist *sg_out;     // Destination 

	unsigned int ret;
	unsigned int iv_len;
	char iv[128];

	unsigned int npages = (unsigned int) nbytes / PAGE_SIZE + 1;
	
	printk(KERN_INFO "[RAMDSK] pagesize calc\n");
	if (nbytes % PAGE_SIZE == 0) 
		--npages;
	
	sg_in = vmalloc(sizeof(struct scatterlist) * npages);
	sg_out = vmalloc(sizeof(struct scatterlist) * npages);

	// Crypto init
	printk(KERN_INFO "[RAMDSK] alloc tfm\n");
	// Allocate transform
	tfm = crypto_alloc_blkcipher(algorithm, 0, 0);
	if (IS_ERR(tfm)) {
		printk(KERN_INFO "[RAMDSK] Error making transform (%s): %ld \n", algorithm, PTR_ERR(tfm));
		return;
	}
	printk(KERN_INFO "[RAMDSK] desc set");
	desc.tfm = tfm;
	desc.flags = 0;

	// Set key
	printk(KERN_INFO "[RAMDSK] set key %s, len = %d", key, strlen(key));
	ret = crypto_blkcipher_setkey(tfm, key, strlen(key));
	if (ret) {
		printk(KERN_INFO "[RAMDSK] Error setting key, failed flags=%x\n"
				, crypto_blkcipher_get_flags(tfm));
		return;
	}

	// Scatterlists
	printk(KERN_INFO "[RAMDSK] init sgls\n");
	sg_init_table(sg_in, npages);
	sg_init_table(sg_out, npages);

	if ((offset + nbytes) > dev->size) {
		printk(KERN_NOTICE "[RAMDSK] Beyond device size (%ld %ld)\n", offset,
		       nbytes);
		return;
	}

	if (datadir) {
		// Write
	
		printk(KERN_INFO "[RAMDSK] encrypt begin\n");
		// Fill Scatterlists
		printk(KERN_INFO "[RAMDSK] fill all sgls\n");
		fill_sgl(sg_in, npages, buffer, nbytes);
		fill_sgl(sg_out, npages, dev->data + offset, nbytes);

		// Needed?
		printk(KERN_INFO "[RAMDSK] iv stuff\n");
		iv_len = crypto_blkcipher_ivsize(tfm);
		if (iv_len) {
			memset(&iv, 0xFF, iv_len);
			crypto_blkcipher_set_iv(tfm, iv, iv_len);
		}

		// Encrypt to same spot
		printk(KERN_INFO "[RAMDSK] encrypt start\n");
		ret = crypto_blkcipher_encrypt(&desc, sg_out, sg_in, nbytes);
		if (ret) {
			printk(KERN_INFO "[RAMDSK] Error encrypting\n");
		}
		printk(KERN_INFO "[RAMDSK] encrypt end\n");
	} else {
		// Read
		printk(KERN_INFO "[RAMDSK] decrypt begin\n");
		
		// Fill Scatterlists
		printk(KERN_INFO "[RAMDSK] fill all sgls\n");
		fill_sgl(sg_in, npages, dev->data + offset, nbytes);
		fill_sgl(sg_out, npages, buffer, nbytes);

		// Needed?
		printk(KERN_INFO "[RAMDSK] iv stuff\n");
		iv_len = crypto_blkcipher_ivsize(tfm);
		if (iv_len) {
			memset(&iv, 0xFF, iv_len);
			crypto_blkcipher_set_iv(tfm, iv, iv_len);
		}

		// Encrypt to same spot
		printk(KERN_INFO "[RAMDSK] decrypt start\n");
		ret = crypto_blkcipher_decrypt(&desc, sg_out, sg_in, nbytes);
		if (ret) {
			printk(KERN_INFO "[RAMDSK] Error encrypting\n");
		}
		printk(KERN_INFO "[RAMDSK] decrypt end\n");
	}

	vfree(sg_in);
	vfree(sg_out);
}

static void ramdsk_request(struct request_queue *queue)
{
	struct request *rq;

	rq = blk_fetch_request(queue);
	while(rq != NULL) {
		struct ramdsk_dev *dev = rq->rq_disk->private_data;
		if ((rq)->cmd_type != REQ_TYPE_FS) {
			printk(KERN_NOTICE "[RAMDSK] Not a file system request, skipping."
			       "\n");
			__blk_end_request_all(rq, -EIO);
			continue;
		}
		ramdsk_transfer(dev, blk_rq_pos(rq), blk_rq_cur_sectors(rq),
		                rq->buffer, rq_data_dir(rq));
		
		if (!__blk_end_request_cur(rq, 0)) {
			rq = blk_fetch_request(queue); 
		}
	}
}

static int ramdsk_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	long size;

	size = nsectors * hardsect_size * (hardsect_size/KERNEL_SECTOR_SIZE);
	geo->cylinders = (size & ~0x3f) >> 6;
	geo->heads = 4;
	geo->sectors = 16;
	geo->start = 0;
	
	return 0;
}

static int ramdsk_ioctl(struct block_device *bdev, fmode_t mode,
                        unsigned int cmd, unsigned long arg)
{
	switch(cmd) {
	case HDIO_GETGEO:
		printk(KERN_NOTICE "I DON'T KNOW WHAT THAT MEANS!!");
		break;
	}
	printk(KERN_NOTICE "[RAMDSK] WHIT IN TH' HEEL URR YE DAEIN' YE CAKEY"
	       "DOBBER?!");
	return 0;
}

static struct block_device_operations ramdsk_ops = {
	.owner = THIS_MODULE,
	.ioctl = ramdsk_ioctl,
	.getgeo = ramdsk_getgeo
};

static void ramdsk_device_init(struct ramdsk_dev *ramdsk_device, int i)
{
	memset(ramdsk_device, 0, sizeof(struct ramdsk_dev));
	ramdsk_device->size = nsectors * hardsect_size;
	ramdsk_device->data = vmalloc(ramdsk_device->size);
	if (ramdsk_device->data == NULL) {
		printk(KERN_NOTICE "[RAMDSK] vmalloc failure.\n");
		return;
	}
	spin_lock_init(&ramdsk_device->lock);
	ramdsk_device->queue = blk_init_queue(ramdsk_request,
	                                      &ramdsk_device->lock);
	if (ramdsk_device->queue == NULL) {
		printk(KERN_NOTICE "[RAMDSK] request queue init failure.\n");
		goto out_vfree;
	}

	blk_queue_logical_block_size(ramdsk_device->queue, hardsect_size);
	ramdsk_device->queue->queuedata = ramdsk_device;
	ramdsk_device->gd = alloc_disk(RAMDSK_MINORS);
	if (!ramdsk_device->gd) {
		printk(KERN_NOTICE "[RAMDSK] alloc_disk failure.\n");
		goto out_queuefree;
	}

	ramdsk_device->gd->major = ramdsk_major;
	ramdsk_device->gd->first_minor = RAMDSK_MINORS * i;
	ramdsk_device->gd->fops = &ramdsk_ops;
	ramdsk_device->gd->queue = ramdsk_device->queue;
	ramdsk_device->gd->private_data = ramdsk_device;
	snprintf(ramdsk_device->gd->disk_name, 32, "ramdsk%c", i + 'a');	
	set_capacity(ramdsk_device->gd, 0);
	add_disk(ramdsk_device->gd);
	set_capacity(ramdsk_device->gd,
	             nsectors * (hardsect_size / KERNEL_SECTOR_SIZE));
	
	return;

 out_queuefree:
	blk_cleanup_queue(ramdsk_device->queue);
	del_gendisk((ramdsk_device + 1)->gd);
 out_vfree:
	vfree(ramdsk_device->data);
	/*
	 * This here was one of the reasons for the null pointer dereference
	 * errors that were causing oops's and and kernel panics. The stupid SOB
	 * caused so many problems I'm leaving him in to shame him. Perfectly safe
	 * to remove otherwise.
	 */
	//out_kfree:
	//kfree(ramdsk_device);
}

static int __init ramdsk_init(void)
{
	//init stuff
	int i;

	ramdsk_major = register_blkdev(ramdsk_major, "ramdsk");
	if (ramdsk_major <= 0) {
		printk(KERN_WARNING "[RAMDSK] unable to get major number\n");
		return -EBUSY;
	}

	ramdsk_device = kmalloc(ndevices * sizeof(struct ramdsk_dev), GFP_KERNEL);
	if (ramdsk_device == NULL)
		goto out_unregister;

	for(i = 0; i < ndevices; i++) {
		ramdsk_device_init(ramdsk_device + i, i);
	}
	
	return 0;

 out_unregister:
	unregister_blkdev(ramdsk_major, "ramdsk");
	return -ENOMEM;
}

static void __exit ramdsk_exit(void)
{
	//exit stuff
	int i;

	for(i = 0; i < ndevices; i++) {
		put_disk((ramdsk_device+i)->gd);
		blk_cleanup_queue((ramdsk_device + i)->queue);
		del_gendisk((ramdsk_device + 1)->gd);
		vfree((ramdsk_device + i)->data);
	}
	kfree(ramdsk_device);
	
	unregister_blkdev(ramdsk_major, "ramdsk");
}

module_init(ramdsk_init);
module_exit(ramdsk_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nick, Li, Josh, Chavis");
MODULE_DESCRIPTION("Encrypted Ram Disk Module");
