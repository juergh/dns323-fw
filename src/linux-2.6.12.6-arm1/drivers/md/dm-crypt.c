/*
 * Copyright (C) 2003 Christophe Saout <christophe@saout.de>
 * Copyright (C) 2004 Clemens Fruhwirth <clemens@endorphin.org>
 *
 * This file is released under the GPL.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/mempool.h>
#include <linux/slab.h>
#include <linux/crypto.h>
#include <linux/workqueue.h>
#include <asm/atomic.h>
#include <asm/scatterlist.h>
#include <asm/page.h>

#if defined(CONFIG_OCF_DM_CRYPT)

#undef DM_DEBUG
#ifdef DM_DEBUG
#define dmprintk printk
#else
#define dmprintk(fmt,args...) 
#endif

#include <../crypto/ocf/cryptodev.h>

#endif /* CONFIG_OCF_DM_CRYPT */

#include "dm.h"

#define PFX	"crypt: "

/*
 * per bio private data
 */
struct crypt_io {
	struct dm_target *target;
	struct bio *bio;
	struct bio *first_clone;
	struct work_struct work;
	atomic_t pending;
	int error;
};

/*
 * context holding the current state of a multi-part conversion
 */
struct convert_context {
	struct bio *bio_in;
	struct bio *bio_out;
	unsigned int offset_in;
	unsigned int offset_out;
	unsigned int idx_in;
	unsigned int idx_out;
	sector_t sector;
	int write;
};

struct crypt_config;

struct crypt_iv_operations {
	int (*ctr)(struct crypt_config *cc, struct dm_target *ti,
	           const char *opts);
	void (*dtr)(struct crypt_config *cc);
	const char *(*status)(struct crypt_config *cc);
	int (*generator)(struct crypt_config *cc, u8 *iv, sector_t sector);
};

/*
 * Crypt: maps a linear range of a block device
 * and encrypts / decrypts at the same time.
 */
struct crypt_config {
	struct dm_dev *dev;
	sector_t start;

	/*
	 * pool for per bio private data and
	 * for encryption buffer pages
	 */
	mempool_t *io_pool;
	mempool_t *page_pool;

	/*
	 * crypto related data
	 */
	struct crypt_iv_operations *iv_gen_ops;
	char *iv_mode;
	void *iv_gen_private;
	sector_t iv_offset;
	unsigned int iv_size;

#if defined(CONFIG_OCF_DM_CRYPT)
	struct cryptoini 	cr_dm;    		/* OCF session */
	char  			ocf_alg_name[CRYPTO_MAX_ALG_NAME];
	uint64_t 	 	ocf_cryptoid;		/* OCF sesssion ID */
#else
	struct crypto_tfm *tfm;
#endif
	unsigned int key_size;
	u8 key[0];
};

#define MIN_IOS        256
#define MIN_POOL_PAGES 32
#define MIN_BIO_PAGES  8

static kmem_cache_t *_crypt_io_pool;

/*
 * Mempool alloc and free functions for the page
 */
static void *mempool_alloc_page(unsigned int __nocast gfp_mask, void *data)
{
	return alloc_page(gfp_mask);
}

static void mempool_free_page(void *page, void *data)
{
	__free_page(page);
}


/*
 * Different IV generation algorithms:
 *
 * plain: the initial vector is the 32-bit low-endian version of the sector
 *        number, padded with zeros if neccessary.
 *
 * ess_iv: "encrypted sector|salt initial vector", the sector number is
 *         encrypted with the bulk cipher using a salt as key. The salt
 *         should be derived from the bulk cipher's key via hashing.
 *
 * plumb: unimplemented, see:
 * http://article.gmane.org/gmane.linux.kernel.device-mapper.dm-crypt/454
 */

static int crypt_iv_plain_gen(struct crypt_config *cc, u8 *iv, sector_t sector)
{
	memset(iv, 0, cc->iv_size);
	*(u32 *)iv = cpu_to_le32(sector & 0xffffffff);

	return 0;
}

static int crypt_iv_essiv_ctr(struct crypt_config *cc, struct dm_target *ti,
	                      const char *opts)
{
	struct crypto_tfm *essiv_tfm;
	struct crypto_tfm *hash_tfm;
	struct scatterlist sg;
	unsigned int saltsize;
	u8 *salt;

	if (opts == NULL) {
		ti->error = PFX "Digest algorithm missing for ESSIV mode";
		return -EINVAL;
	}

	/* Hash the cipher key with the given hash algorithm */
	hash_tfm = crypto_alloc_tfm(opts, 0);
	if (hash_tfm == NULL) {
		ti->error = PFX "Error initializing ESSIV hash";
		return -EINVAL;
	}

	if (crypto_tfm_alg_type(hash_tfm) != CRYPTO_ALG_TYPE_DIGEST) {
		ti->error = PFX "Expected digest algorithm for ESSIV hash";
		crypto_free_tfm(hash_tfm);
		return -EINVAL;
	}

	saltsize = crypto_tfm_alg_digestsize(hash_tfm);
	salt = kmalloc(saltsize, GFP_KERNEL);
	if (salt == NULL) {
		ti->error = PFX "Error kmallocing salt storage in ESSIV";
		crypto_free_tfm(hash_tfm);
		return -ENOMEM;
	}

	sg.page = virt_to_page(cc->key);
	sg.offset = offset_in_page(cc->key);
	sg.length = cc->key_size;
	crypto_digest_digest(hash_tfm, &sg, 1, salt);
	crypto_free_tfm(hash_tfm);

	/* Setup the essiv_tfm with the given salt */
#if defined(CONFIG_OCF_DM_CRYPT)
	essiv_tfm = crypto_alloc_tfm(cc->ocf_alg_name,
	                             CRYPTO_TFM_MODE_ECB);
#else
	essiv_tfm = crypto_alloc_tfm(crypto_tfm_alg_name(cc->tfm),
	                             CRYPTO_TFM_MODE_ECB);
#endif
	if (essiv_tfm == NULL) {
		ti->error = PFX "Error allocating crypto tfm for ESSIV";
		kfree(salt);
		return -EINVAL;
	}
#if  defined(CONFIG_OCF_DM_CRYPT)
	if (crypto_tfm_alg_blocksize(essiv_tfm) != cc->iv_size) {
#else
	if (crypto_tfm_alg_blocksize(essiv_tfm)
	    != crypto_tfm_alg_ivsize(cc->tfm)) {
#endif
		printk("crypto_tfm_alg_blocksize %x cc->iv_size %x \n",crypto_tfm_alg_blocksize(essiv_tfm), cc->iv_size );
		ti->error = PFX "Block size of ESSIV cipher does "
			        "not match IV size of block cipher";
		crypto_free_tfm(essiv_tfm);
		kfree(salt);
		return -EINVAL;
	}

	/* If Salt is too big then cut it .. */
	if(saltsize >= crypto_tfm_alg_max_keysize(essiv_tfm))
		saltsize = crypto_tfm_alg_max_keysize(essiv_tfm);

	if (crypto_cipher_setkey(essiv_tfm, salt, saltsize) < 0) {
		printk("error flags is %x \n",essiv_tfm->crt_flags);
		printk("saltsize is %x and essiv cipher min key size is %x \n", saltsize, crypto_tfm_alg_min_keysize(essiv_tfm));
		ti->error = PFX "Failed to set key for ESSIV cipher";
		crypto_free_tfm(essiv_tfm);
		kfree(salt);
		return -EINVAL;
	}
	kfree(salt);

	cc->iv_gen_private = (void *)essiv_tfm;
	return 0;
}

static void crypt_iv_essiv_dtr(struct crypt_config *cc)
{
	crypto_free_tfm((struct crypto_tfm *)cc->iv_gen_private);
	cc->iv_gen_private = NULL;
}

static int crypt_iv_essiv_gen(struct crypt_config *cc, u8 *iv, sector_t sector)
{
	struct scatterlist sg = { NULL, };

	memset(iv, 0, cc->iv_size);
	*(u64 *)iv = cpu_to_le64(sector);

	sg.page = virt_to_page(iv);
	sg.offset = offset_in_page(iv);
	sg.length = cc->iv_size;
	crypto_cipher_encrypt((struct crypto_tfm *)cc->iv_gen_private,
	                      &sg, &sg, cc->iv_size);

	return 0;
}

static struct crypt_iv_operations crypt_iv_plain_ops = {
	.generator = crypt_iv_plain_gen
};

static struct crypt_iv_operations crypt_iv_essiv_ops = {
	.ctr       = crypt_iv_essiv_ctr,
	.dtr       = crypt_iv_essiv_dtr,
	.generator = crypt_iv_essiv_gen
};


#if defined(CONFIG_OCF_DM_CRYPT)
static void dec_pending(struct crypt_io *io, int error);

struct ocf_wr_priv {
	u32 		 	dm_ocf_wr_completed;	/* Num of wr completions */
	u32 		 	dm_ocf_wr_pending;	/* Num of wr pendings */
	wait_queue_head_t	dm_ocf_wr_queue;	/* waiting Q, for wr completion */
};

static int dm_ocf_wr_cb(struct cryptop *crp)
{
	struct ocf_wr_priv *ocf_wr_priv;

	if(crp == NULL) {
		printk("dm_ocf_wr_cb: crp is NULL!! \n");
		return 0;
	}

	ocf_wr_priv = (struct ocf_wr_priv*)crp->crp_opaque;

	ocf_wr_priv->dm_ocf_wr_completed++;
	
	/* if no more pending for read, wake up the read task. */
	if(ocf_wr_priv->dm_ocf_wr_completed == ocf_wr_priv->dm_ocf_wr_pending)
		wake_up(&ocf_wr_priv->dm_ocf_wr_queue);

	crypto_freereq(crp);
	return 0;
}

static int dm_ocf_rd_cb(struct cryptop *crp)
{
	struct crypt_io *io;

	if(crp == NULL) {
		printk("dm_ocf_rd_cb: crp is NULL!! \n");
		return 0;
	}

	io = (struct crypt_io *)crp->crp_opaque;

	crypto_freereq(crp);

	if(io != NULL)
		dec_pending(io, 0);

	return 0;
}

static inline int dm_ocf_process(struct crypt_config *cc, struct scatterlist *out, 
		struct scatterlist *in, unsigned int len, u8 *iv, int iv_size, int write, void *priv)
{
	struct cryptop *crp;
	struct cryptodesc *crda = NULL;

	if(!iv) {
		printk("dm_ocf_process: only CBC mode is supported\n");
		return -EPERM;	
	}

	crp = crypto_getreq(1);	 /* only encryption/decryption */
	if (!crp) {
		printk("dm_ocf_process: crypto_getreq failed!!\n");
		return -ENOMEM;
	}
	
	crda = crp->crp_desc;

	crda->crd_flags  = (write)? CRD_F_ENCRYPT: 0;	
	crda->crd_alg    = cc->cr_dm.cri_alg;
	crda->crd_skip   = 0;
	crda->crd_len    = len;
	crda->crd_inject = 0; /* NA */
	crda->crd_klen   = cc->cr_dm.cri_klen;
	crda->crd_key    = cc->cr_dm.cri_key;

	if (iv) {
		crda->crd_flags |= (CRD_F_IV_EXPLICIT | CRD_F_IV_PRESENT);
		if( iv_size > EALG_MAX_BLOCK_LEN ) {
			printk("dm_ocf_process: iv is too big!!\n");
		}
		memcpy(&crda->crd_iv, iv, iv_size);		
	}

	/* according to the current implementation the in and the out are the same buffer for read, and different for write*/
	if((page_address(out->page) + out->offset) != (page_address(in->page) + in->offset)) {
		memcpy((page_address(out->page) + out->offset) , (page_address(in->page) + in->offset) , len);
		dmprintk("dm_ocf_process: copy buffers!! \n");
	}

	dmprintk("len: %d",len);
	crp->crp_ilen = len; /* Total input length */
        crp->crp_flags = CRYPTO_F_CBIMM /* | CRYPTO_F_BATCH*/;
        crp->crp_buf = page_address(out->page) + out->offset;
	crp->crp_opaque = priv;
	if(write) {
        	crp->crp_callback = dm_ocf_wr_cb;
	}
	else {
		crp->crp_callback = dm_ocf_rd_cb;
	}
        crp->crp_sid = cc->ocf_cryptoid;
        if(crypto_dispatch(crp) != 0) {
		printk("dm_ocf_process: crypto_dispatch failed!!\n");
		return -ENOMEM;
	}

	return 0;
	
}

static inline int
ocf_crypt_convert_scatterlist(struct crypt_config *cc, struct scatterlist *out,
                          struct scatterlist *in, unsigned int length,
                          int write, sector_t sector, void *priv)
{
	u8 iv[cc->iv_size];
	int r;

	if (cc->iv_gen_ops) {
		r = cc->iv_gen_ops->generator(cc, iv, sector);
		if (r < 0)
			return r;
		r = dm_ocf_process(cc, out, in, length, iv, cc->iv_size, write, priv);
	} else {
		r = dm_ocf_process(cc, out, in, length, NULL, 0, write, priv);
	}

	return r;
}

/*
 * Encrypt / decrypt data from one bio to another one (can be the same one)
 */
static int ocf_crypt_convert(struct crypt_config *cc,
                         struct convert_context *ctx, struct crypt_io *io)
{
	int r = 0;
	long wr_timeout = 30000;
	long wr_tm;
	int num = 0;
	void *priv = NULL;
	struct ocf_wr_priv *ocf_wr_priv = NULL;

	if(ctx->write) {
		ocf_wr_priv = kmalloc(sizeof(struct ocf_wr_priv),GFP_KERNEL);
		if(!ocf_wr_priv) {
			printk("ocf_crypt_convert: out of memory \n");
			return -ENOMEM;
		}
		ocf_wr_priv->dm_ocf_wr_pending = 0;
		ocf_wr_priv->dm_ocf_wr_completed = 0;
		init_waitqueue_head(&ocf_wr_priv->dm_ocf_wr_queue);
		priv = ocf_wr_priv;
	}

	while(ctx->idx_in < ctx->bio_in->bi_vcnt &&
	      ctx->idx_out < ctx->bio_out->bi_vcnt) {
		struct bio_vec *bv_in = bio_iovec_idx(ctx->bio_in, ctx->idx_in);
		struct bio_vec *bv_out = bio_iovec_idx(ctx->bio_out, ctx->idx_out);
		struct scatterlist sg_in = {
			.page = bv_in->bv_page,
			.offset = bv_in->bv_offset + ctx->offset_in,
			.length = 1 << SECTOR_SHIFT
		};
		struct scatterlist sg_out = {
			.page = bv_out->bv_page,
			.offset = bv_out->bv_offset + ctx->offset_out,
			.length = 1 << SECTOR_SHIFT
		};

		ctx->offset_in += sg_in.length;
		if (ctx->offset_in >= bv_in->bv_len) {
			ctx->offset_in = 0;
			ctx->idx_in++;
		}

		ctx->offset_out += sg_out.length;
		if (ctx->offset_out >= bv_out->bv_len) {
			ctx->offset_out = 0;
			ctx->idx_out++;
		}

		if(ctx->write) {
			num++;
		}
		/* if last read in the context - send the io, so the OCF read callback will release the IO. */
		else if(!(ctx->idx_in < ctx->bio_in->bi_vcnt && ctx->idx_out < ctx->bio_out->bi_vcnt)) {
			priv = io;
		}

		r = ocf_crypt_convert_scatterlist(cc, &sg_out, &sg_in, sg_in.length,
		                              ctx->write, ctx->sector, priv);
		if (r < 0){
			printk("ocf_crypt_convert: ocf_crypt_convert_scatterlist failed \n");
			break;
		}

		ctx->sector++;
	}

	if(ctx->write) {
		ocf_wr_priv->dm_ocf_wr_pending += num;
		wr_tm = wait_event_timeout(ocf_wr_priv->dm_ocf_wr_queue, 
				(ocf_wr_priv->dm_ocf_wr_pending == ocf_wr_priv->dm_ocf_wr_completed)
									, msecs_to_jiffies(wr_timeout) );
		if (!wr_tm) {
			printk("ocf_crypt_convert: wr work was not finished in %ld msecs, %d pending %d completed.\n", 
				wr_timeout, ocf_wr_priv->dm_ocf_wr_pending, ocf_wr_priv->dm_ocf_wr_completed);
		}
		kfree(ocf_wr_priv);
	}

	return r;
}

#else

static inline int
crypt_convert_scatterlist(struct crypt_config *cc, struct scatterlist *out,
                          struct scatterlist *in, unsigned int length,
                          int write, sector_t sector)
{
	u8 iv[cc->iv_size];
	int r;

	if (cc->iv_gen_ops) {
		r = cc->iv_gen_ops->generator(cc, iv, sector);
		if (r < 0)
			return r;

		if (write)
			r = crypto_cipher_encrypt_iv(cc->tfm, out, in, length, iv);
		else
			r = crypto_cipher_decrypt_iv(cc->tfm, out, in, length, iv);
	} else {
		if (write)
			r = crypto_cipher_encrypt(cc->tfm, out, in, length);
		else
			r = crypto_cipher_decrypt(cc->tfm, out, in, length);
	}

	return r;
}

/*
 * Encrypt / decrypt data from one bio to another one (can be the same one)
 */
static int crypt_convert(struct crypt_config *cc,
                         struct convert_context *ctx)
{
	int r = 0;

	while(ctx->idx_in < ctx->bio_in->bi_vcnt &&
	      ctx->idx_out < ctx->bio_out->bi_vcnt) {
		struct bio_vec *bv_in = bio_iovec_idx(ctx->bio_in, ctx->idx_in);
		struct bio_vec *bv_out = bio_iovec_idx(ctx->bio_out, ctx->idx_out);
		struct scatterlist sg_in = {
			.page = bv_in->bv_page,
			.offset = bv_in->bv_offset + ctx->offset_in,
			.length = 1 << SECTOR_SHIFT
		};
		struct scatterlist sg_out = {
			.page = bv_out->bv_page,
			.offset = bv_out->bv_offset + ctx->offset_out,
			.length = 1 << SECTOR_SHIFT
		};

		ctx->offset_in += sg_in.length;
		if (ctx->offset_in >= bv_in->bv_len) {
			ctx->offset_in = 0;
			ctx->idx_in++;
		}

		ctx->offset_out += sg_out.length;
		if (ctx->offset_out >= bv_out->bv_len) {
			ctx->offset_out = 0;
			ctx->idx_out++;
		}

		r = crypt_convert_scatterlist(cc, &sg_out, &sg_in, sg_in.length,
		                              ctx->write, ctx->sector);
		if (r < 0)
			break;

		ctx->sector++;
	}

	return r;
}

#endif

static void
crypt_convert_init(struct crypt_config *cc, struct convert_context *ctx,
                   struct bio *bio_out, struct bio *bio_in,
                   sector_t sector, int write)
{
	ctx->bio_in = bio_in;
	ctx->bio_out = bio_out;
	ctx->offset_in = 0;
	ctx->offset_out = 0;
	ctx->idx_in = bio_in ? bio_in->bi_idx : 0;
	ctx->idx_out = bio_out ? bio_out->bi_idx : 0;
	ctx->sector = sector + cc->iv_offset;
	ctx->write = write;
}


/*
 * Generate a new unfragmented bio with the given size
 * This should never violate the device limitations
 * May return a smaller bio when running out of pages
 */
static struct bio *
crypt_alloc_buffer(struct crypt_config *cc, unsigned int size,
                   struct bio *base_bio, unsigned int *bio_vec_idx)
{
	struct bio *bio;
	unsigned int nr_iovecs = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	int gfp_mask = GFP_NOIO | __GFP_HIGHMEM;
	unsigned int i;

	/*
	 * Use __GFP_NOMEMALLOC to tell the VM to act less aggressively and
	 * to fail earlier.  This is not necessary but increases throughput.
	 * FIXME: Is this really intelligent?
	 */
	if (base_bio)
		bio = bio_clone(base_bio, GFP_NOIO|__GFP_NOMEMALLOC);
	else
		bio = bio_alloc(GFP_NOIO|__GFP_NOMEMALLOC, nr_iovecs);
	if (!bio)
		return NULL;

	/* if the last bio was not complete, continue where that one ended */
	bio->bi_idx = *bio_vec_idx;
	bio->bi_vcnt = *bio_vec_idx;
	bio->bi_size = 0;
	bio->bi_flags &= ~(1 << BIO_SEG_VALID);

	/* bio->bi_idx pages have already been allocated */
	size -= bio->bi_idx * PAGE_SIZE;

	for(i = bio->bi_idx; i < nr_iovecs; i++) {
		struct bio_vec *bv = bio_iovec_idx(bio, i);

		bv->bv_page = mempool_alloc(cc->page_pool, gfp_mask);
		if (!bv->bv_page)
			break;

		/*
		 * if additional pages cannot be allocated without waiting,
		 * return a partially allocated bio, the caller will then try
		 * to allocate additional bios while submitting this partial bio
		 */
		if ((i - bio->bi_idx) == (MIN_BIO_PAGES - 1))
			gfp_mask = (gfp_mask | __GFP_NOWARN) & ~__GFP_WAIT;

		bv->bv_offset = 0;
		if (size > PAGE_SIZE)
			bv->bv_len = PAGE_SIZE;
		else
			bv->bv_len = size;

		bio->bi_size += bv->bv_len;
		bio->bi_vcnt++;
		size -= bv->bv_len;
	}

	if (!bio->bi_size) {
		bio_put(bio);
		return NULL;
	}

	/*
	 * Remember the last bio_vec allocated to be able
	 * to correctly continue after the splitting.
	 */
	*bio_vec_idx = bio->bi_vcnt;

	return bio;
}

static void crypt_free_buffer_pages(struct crypt_config *cc,
                                    struct bio *bio, unsigned int bytes)
{
	unsigned int i, start, end;
	struct bio_vec *bv;

	/*
	 * This is ugly, but Jens Axboe thinks that using bi_idx in the
	 * endio function is too dangerous at the moment, so I calculate the
	 * correct position using bi_vcnt and bi_size.
	 * The bv_offset and bv_len fields might already be modified but we
	 * know that we always allocated whole pages.
	 * A fix to the bi_idx issue in the kernel is in the works, so
	 * we will hopefully be able to revert to the cleaner solution soon.
	 */
	i = bio->bi_vcnt - 1;
	bv = bio_iovec_idx(bio, i);
	end = (i << PAGE_SHIFT) + (bv->bv_offset + bv->bv_len) - bio->bi_size;
	start = end - bytes;

	start >>= PAGE_SHIFT;
	if (!bio->bi_size)
		end = bio->bi_vcnt;
	else
		end >>= PAGE_SHIFT;

	for(i = start; i < end; i++) {
		bv = bio_iovec_idx(bio, i);
		BUG_ON(!bv->bv_page);
		mempool_free(bv->bv_page, cc->page_pool);
		bv->bv_page = NULL;
	}
}

/*
 * One of the bios was finished. Check for completion of
 * the whole request and correctly clean up the buffer.
 */
static void dec_pending(struct crypt_io *io, int error)
{
	struct crypt_config *cc = (struct crypt_config *) io->target->private;

	if (error < 0)
		io->error = error;

	if (!atomic_dec_and_test(&io->pending))
		return;

	if (io->first_clone)
		bio_put(io->first_clone);

	bio_endio(io->bio, io->bio->bi_size, io->error);

	mempool_free(io, cc->io_pool);
}

/*
 * kcryptd:
 *
 * Needed because it would be very unwise to do decryption in an
 * interrupt context, so bios returning from read requests get
 * queued here.
 */
static struct workqueue_struct *_kcryptd_workqueue;

static void kcryptd_do_work(void *data)
{
	struct crypt_io *io = (struct crypt_io *) data;
	struct crypt_config *cc = (struct crypt_config *) io->target->private;
	struct convert_context ctx;
	int r;

	crypt_convert_init(cc, &ctx, io->bio, io->bio,
	                   io->bio->bi_sector - io->target->begin, 0);
#if defined(CONFIG_OCF_DM_CRYPT)
	r = ocf_crypt_convert(cc, &ctx, io);

	if(r < 0) {
		u32 rd_failed_timeout = 500;
		wait_queue_head_t dm_ocf_rd_failed_queu;

		init_waitqueue_head(&dm_ocf_rd_failed_queu);

		/* wait a bit before freeing the io, maybe few requests are still in process */
		wait_event_timeout(dm_ocf_rd_failed_queu, 0, msecs_to_jiffies(rd_failed_timeout) );

		dec_pending(io, r); 
	}
#else
	r = crypt_convert(cc, &ctx);

	dec_pending(io, r);
#endif
}

static void kcryptd_queue_io(struct crypt_io *io)
{
	INIT_WORK(&io->work, kcryptd_do_work, io);
	queue_work(_kcryptd_workqueue, &io->work);
}

/*
 * Decode key from its hex representation
 */
static int crypt_decode_key(u8 *key, char *hex, unsigned int size)
{
	char buffer[3];
	char *endp;
	unsigned int i;

	buffer[2] = '\0';

	for(i = 0; i < size; i++) {
		buffer[0] = *hex++;
		buffer[1] = *hex++;

		key[i] = (u8)simple_strtoul(buffer, &endp, 16);

		if (endp != &buffer[2])
			return -EINVAL;
	}

	if (*hex != '\0')
		return -EINVAL;

	return 0;
}

/*
 * Encode key into its hex representation
 */
static void crypt_encode_key(char *hex, u8 *key, unsigned int size)
{
	unsigned int i;

	for(i = 0; i < size; i++) {
		sprintf(hex, "%02x", *key);
		hex += 2;
		key++;
	}
}

/*
 * Construct an encryption mapping:
 * <cipher> <key> <iv_offset> <dev_path> <start>
 */
static int crypt_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	struct crypt_config *cc;
	unsigned int crypto_flags;
#ifndef CONFIG_OCF_DM_CRYPT
	struct crypto_tfm *tfm;
#endif
	char *tmp;
	char *cipher;
	char *chainmode;
	char *ivmode;
	char *ivopts;
	unsigned int key_size;

	if (argc != 5) {
		ti->error = PFX "Not enough arguments";
		return -EINVAL;
	}

	tmp = argv[0];
	cipher = strsep(&tmp, "-");
	chainmode = strsep(&tmp, "-");
	ivopts = strsep(&tmp, "-");
	ivmode = strsep(&ivopts, ":");

	if (tmp)
		DMWARN(PFX "Unexpected additional cipher options");

	key_size = strlen(argv[1]) >> 1;

	cc = kmalloc(sizeof(*cc) + key_size * sizeof(u8), GFP_KERNEL);
	if (cc == NULL) {
		ti->error =
			PFX "Cannot allocate transparent encryption context";
		return -ENOMEM;
	}

	cc->key_size = key_size;
	if ((!key_size && strcmp(argv[1], "-") != 0) ||
	    (key_size && crypt_decode_key(cc->key, argv[1], key_size) < 0)) {
		ti->error = PFX "Error decoding key";
		goto bad1;
	}

	/* Compatiblity mode for old dm-crypt cipher strings */
	if (!chainmode || (strcmp(chainmode, "plain") == 0 && !ivmode)) {
		chainmode = "cbc";
		ivmode = "plain";
	}

	/* Choose crypto_flags according to chainmode */
	if (strcmp(chainmode, "cbc") == 0)
		crypto_flags = CRYPTO_TFM_MODE_CBC;
	else if (strcmp(chainmode, "ecb") == 0)
		crypto_flags = CRYPTO_TFM_MODE_ECB;
	else {
		ti->error = PFX "Unknown chaining mode";
		goto bad1;
	}

	if (crypto_flags != CRYPTO_TFM_MODE_ECB && !ivmode) {
		ti->error = PFX "This chaining mode requires an IV mechanism";
		goto bad1;
	}

#if defined(CONFIG_OCF_DM_CRYPT)
	/* prepare a new OCF session */
        memset(&cc->cr_dm, 0, sizeof(struct cryptoini));

	if((strcmp(cipher,"aes") == 0) && (strcmp(chainmode, "cbc") == 0))
        	cc->cr_dm.cri_alg  = CRYPTO_AES_CBC;
	else if((strcmp(cipher,"des") == 0) && (strcmp(chainmode, "cbc") == 0))
        	cc->cr_dm.cri_alg  = CRYPTO_DES_CBC;
	else if((strcmp(cipher,"des3_ede") == 0) && (strcmp(chainmode, "cbc") == 0))
        	cc->cr_dm.cri_alg  = CRYPTO_3DES_CBC; 
	else {
		ti->error = PFX "using OCF: unknown cipher or bad chain mode";
		goto bad1;
	}

	strcpy(cc->ocf_alg_name, cipher);
	dmprintk("key size is %d\n",cc->key_size);
        cc->cr_dm.cri_klen = cc->key_size*8;
        cc->cr_dm.cri_key  = cc->key;
        cc->cr_dm.cri_next = NULL;

        if(crypto_newsession(&cc->ocf_cryptoid, &cc->cr_dm, 0)){
		dmprintk("crypt_ctr: crypto_newsession failed\n");
                ti->error = PFX "crypto_newsession failed";
                goto bad2;
        }

#else
	tfm = crypto_alloc_tfm(cipher, crypto_flags);
	if (!tfm) {
		ti->error = PFX "Error allocating crypto tfm";
		goto bad1;
	}
	if (crypto_tfm_alg_type(tfm) != CRYPTO_ALG_TYPE_CIPHER) {
		ti->error = PFX "Expected cipher algorithm";
		goto bad2;
	}

	cc->tfm = tfm;
#endif
	/*
	 * Choose ivmode. Valid modes: "plain", "essiv:<esshash>".
	 * See comments at iv code
	 */

	if (ivmode == NULL)
		cc->iv_gen_ops = NULL;
	else if (strcmp(ivmode, "plain") == 0)
		cc->iv_gen_ops = &crypt_iv_plain_ops;
	else if (strcmp(ivmode, "essiv") == 0)
		cc->iv_gen_ops = &crypt_iv_essiv_ops;
	else {
		ti->error = PFX "Invalid IV mode";
		goto bad2;
	}

#if defined(CONFIG_OCF_DM_CRYPT)
	switch (cc->cr_dm.cri_alg) {
		case CRYPTO_AES_CBC:
			cc->iv_size = 16;
			break;
		default:
			cc->iv_size = 8;
			break;
	}

	if (cc->iv_gen_ops && cc->iv_gen_ops->ctr &&
	    cc->iv_gen_ops->ctr(cc, ti, ivopts) < 0)
		goto bad2;
#else

	if (cc->iv_gen_ops && cc->iv_gen_ops->ctr &&
	    cc->iv_gen_ops->ctr(cc, ti, ivopts) < 0)
		goto bad2;


	if (tfm->crt_cipher.cit_decrypt_iv && tfm->crt_cipher.cit_encrypt_iv)
		/* at least a 64 bit sector number should fit in our buffer */
		cc->iv_size = max(crypto_tfm_alg_ivsize(tfm),
		                  (unsigned int)(sizeof(u64) / sizeof(u8)));
	else {
		cc->iv_size = 0;
		if (cc->iv_gen_ops) {
			DMWARN(PFX "Selected cipher does not support IVs");
			if (cc->iv_gen_ops->dtr)
				cc->iv_gen_ops->dtr(cc);
			cc->iv_gen_ops = NULL;
		}
	}
#endif

	cc->io_pool = mempool_create(MIN_IOS, mempool_alloc_slab,
				     mempool_free_slab, _crypt_io_pool);
	if (!cc->io_pool) {
		ti->error = PFX "Cannot allocate crypt io mempool";
		goto bad3;
	}

	cc->page_pool = mempool_create(MIN_POOL_PAGES, mempool_alloc_page,
				       mempool_free_page, NULL);
	if (!cc->page_pool) {
		ti->error = PFX "Cannot allocate page mempool";
		goto bad4;
	}

#if !defined(CONFIG_OCF_DM_CRYPT)
	if (tfm->crt_cipher.cit_setkey(tfm, cc->key, key_size) < 0) {
		ti->error = PFX "Error setting key";
		goto bad5;
	}
#endif

	if (sscanf(argv[2], SECTOR_FORMAT, &cc->iv_offset) != 1) {
		ti->error = PFX "Invalid iv_offset sector";
		goto bad5;
	}

	if (sscanf(argv[4], SECTOR_FORMAT, &cc->start) != 1) {
		ti->error = PFX "Invalid device sector";
		goto bad5;
	}

	if (dm_get_device(ti, argv[3], cc->start, ti->len,
	                  dm_table_get_mode(ti->table), &cc->dev)) {
		ti->error = PFX "Device lookup failed";
		goto bad5;
	}

	if (ivmode && cc->iv_gen_ops) {
		if (ivopts)
			*(ivopts - 1) = ':';
		cc->iv_mode = kmalloc(strlen(ivmode) + 1, GFP_KERNEL);
		if (!cc->iv_mode) {
			ti->error = PFX "Error kmallocing iv_mode string";
			goto bad5;
		}
		strcpy(cc->iv_mode, ivmode);
	} else
		cc->iv_mode = NULL;

	ti->private = cc;
	return 0;

bad5:
	mempool_destroy(cc->page_pool);
bad4:
	mempool_destroy(cc->io_pool);
bad3:
	if (cc->iv_gen_ops && cc->iv_gen_ops->dtr)
		cc->iv_gen_ops->dtr(cc);
bad2:
#if defined(CONFIG_OCF_DM_CRYPT)
	crypto_freesession(cc->ocf_cryptoid);
#else
	crypto_free_tfm(tfm);
#endif
bad1:
	kfree(cc);
	return -EINVAL;
}

static void crypt_dtr(struct dm_target *ti)
{
	struct crypt_config *cc = (struct crypt_config *) ti->private;

	mempool_destroy(cc->page_pool);
	mempool_destroy(cc->io_pool);

	if (cc->iv_mode)
		kfree(cc->iv_mode);
	if (cc->iv_gen_ops && cc->iv_gen_ops->dtr)
		cc->iv_gen_ops->dtr(cc);
#if defined(CONFIG_OCF_DM_CRYPT)
	crypto_freesession(cc->ocf_cryptoid);
#else
	crypto_free_tfm(cc->tfm);
#endif
	dm_put_device(ti, cc->dev);
	kfree(cc);
}

static int crypt_endio(struct bio *bio, unsigned int done, int error)
{
	struct crypt_io *io = (struct crypt_io *) bio->bi_private;
	struct crypt_config *cc = (struct crypt_config *) io->target->private;

	if (bio_data_dir(bio) == WRITE) {
		/*
		 * free the processed pages, even if
		 * it's only a partially completed write
		 */
		crypt_free_buffer_pages(cc, bio, done);
	}

	if (bio->bi_size)
		return 1;

	bio_put(bio);

	/*
	 * successful reads are decrypted by the worker thread
	 */
	if ((bio_data_dir(bio) == READ)
	    && bio_flagged(bio, BIO_UPTODATE)) {
		kcryptd_queue_io(io);
		return 0;
	}

	dec_pending(io, error);
	return error;
}

static inline struct bio *
crypt_clone(struct crypt_config *cc, struct crypt_io *io, struct bio *bio,
            sector_t sector, unsigned int *bvec_idx,
            struct convert_context *ctx)
{
	struct bio *clone;

	if (bio_data_dir(bio) == WRITE) {
		clone = crypt_alloc_buffer(cc, bio->bi_size,
                                 io->first_clone, bvec_idx);
		if (clone) {
			ctx->bio_out = clone;
#if defined(CONFIG_OCF_DM_CRYPT)
			if (ocf_crypt_convert(cc, ctx, io) < 0) {
#else
			if (crypt_convert(cc, ctx) < 0) {
#endif
				crypt_free_buffer_pages(cc, clone,
				                        clone->bi_size);
				bio_put(clone);
				return NULL;
			}
		}
	} else {
		/*
		 * The block layer might modify the bvec array, so always
		 * copy the required bvecs because we need the original
		 * one in order to decrypt the whole bio data *afterwards*.
		 */
		clone = bio_alloc(GFP_NOIO, bio_segments(bio));
		if (clone) {
			clone->bi_idx = 0;
			clone->bi_vcnt = bio_segments(bio);
			clone->bi_size = bio->bi_size;
			memcpy(clone->bi_io_vec, bio_iovec(bio),
			       sizeof(struct bio_vec) * clone->bi_vcnt);
		}
	}

	if (!clone)
		return NULL;

	clone->bi_private = io;
	clone->bi_end_io = crypt_endio;
	clone->bi_bdev = cc->dev->bdev;
	clone->bi_sector = cc->start + sector;
	clone->bi_rw = bio->bi_rw;

	return clone;
}

static int crypt_map(struct dm_target *ti, struct bio *bio,
		     union map_info *map_context)
{
	struct crypt_config *cc = (struct crypt_config *) ti->private;
	struct crypt_io *io = mempool_alloc(cc->io_pool, GFP_NOIO);
	struct convert_context ctx;
	struct bio *clone;
	unsigned int remaining = bio->bi_size;
	sector_t sector = bio->bi_sector - ti->begin;
	unsigned int bvec_idx = 0;

	io->target = ti;
	io->bio = bio;
	io->first_clone = NULL;
	io->error = 0;
	atomic_set(&io->pending, 1); /* hold a reference */

	if (bio_data_dir(bio) == WRITE)
		crypt_convert_init(cc, &ctx, NULL, bio, sector, 1);

	/*
	 * The allocated buffers can be smaller than the whole bio,
	 * so repeat the whole process until all the data can be handled.
	 */
	while (remaining) {
		clone = crypt_clone(cc, io, bio, sector, &bvec_idx, &ctx);
		if (!clone)
			goto cleanup;

		if (!io->first_clone) {
			/*
			 * hold a reference to the first clone, because it
			 * holds the bio_vec array and that can't be freed
			 * before all other clones are released
			 */
			bio_get(clone);
			io->first_clone = clone;
		}
		atomic_inc(&io->pending);

		remaining -= clone->bi_size;
		sector += bio_sectors(clone);

		generic_make_request(clone);

		/* out of memory -> run queues */
		if (remaining)
			blk_congestion_wait(bio_data_dir(clone), HZ/100);
	}

	/* drop reference, clones could have returned before we reach this */
	dec_pending(io, 0);
	return 0;

cleanup:
	if (io->first_clone) {
		dec_pending(io, -ENOMEM);
		return 0;
	}

	/* if no bio has been dispatched yet, we can directly return the error */
	mempool_free(io, cc->io_pool);
	return -ENOMEM;
}

static int crypt_status(struct dm_target *ti, status_type_t type,
			char *result, unsigned int maxlen)
{
	struct crypt_config *cc = (struct crypt_config *) ti->private;
	const char *cipher;
	const char *chainmode = NULL;
	unsigned int sz = 0;

	switch (type) {
	case STATUSTYPE_INFO:
		result[0] = '\0';
		break;

	case STATUSTYPE_TABLE:

#if  defined(CONFIG_OCF_DM_CRYPT)
		printk("using OCF ");
		cipher = cc->ocf_alg_name;
		chainmode = "cbc";
#else
		cipher = crypto_tfm_alg_name(cc->tfm);

		switch(cc->tfm->crt_cipher.cit_mode) {
		case CRYPTO_TFM_MODE_CBC:
			chainmode = "cbc";
			break;
		case CRYPTO_TFM_MODE_ECB:
			chainmode = "ecb";
			break;
		default:
			BUG();
		}
#endif

		if (cc->iv_mode)
			DMEMIT("%s-%s-%s ", cipher, chainmode, cc->iv_mode);
		else
			DMEMIT("%s-%s ", cipher, chainmode);

		if (cc->key_size > 0) {
			if ((maxlen - sz) < ((cc->key_size << 1) + 1))
				return -ENOMEM;

			crypt_encode_key(result + sz, cc->key, cc->key_size);
			sz += cc->key_size << 1;
		} else {
			if (sz >= maxlen)
				return -ENOMEM;
			result[sz++] = '-';
		}

		DMEMIT(" " SECTOR_FORMAT " %s " SECTOR_FORMAT,
		       cc->iv_offset, cc->dev->name, cc->start);
		break;
	}
	return 0;
}

static struct target_type crypt_target = {
	.name   = "crypt",
	.version= {1, 1, 0},
	.module = THIS_MODULE,
	.ctr    = crypt_ctr,
	.dtr    = crypt_dtr,
	.map    = crypt_map,
	.status = crypt_status,
};

static int __init dm_crypt_init(void)
{
	int r;

	_crypt_io_pool = kmem_cache_create("dm-crypt_io",
	                                   sizeof(struct crypt_io),
	                                   0, 0, NULL, NULL);
	if (!_crypt_io_pool)
		return -ENOMEM;

	_kcryptd_workqueue = create_workqueue("kcryptd");
	if (!_kcryptd_workqueue) {
		r = -ENOMEM;
		DMERR(PFX "couldn't create kcryptd");
		goto bad1;
	}

	r = dm_register_target(&crypt_target);
	if (r < 0) {
		DMERR(PFX "register failed %d", r);
		goto bad2;
	}

#ifdef CONFIG_OCF_DM_CRYPT
	printk("dm_crypt using the OCF package.");
#endif
	return 0;

bad2:
	destroy_workqueue(_kcryptd_workqueue);
bad1:
	kmem_cache_destroy(_crypt_io_pool);
	return r;
}

static void __exit dm_crypt_exit(void)
{
	int r = dm_unregister_target(&crypt_target);

	if (r < 0)
		DMERR(PFX "unregister failed %d", r);

	destroy_workqueue(_kcryptd_workqueue);
	kmem_cache_destroy(_crypt_io_pool);
}

module_init(dm_crypt_init);
module_exit(dm_crypt_exit);

MODULE_AUTHOR("Christophe Saout <christophe@saout.de>");
MODULE_DESCRIPTION(DM_NAME " target for transparent encryption / decryption");
MODULE_LICENSE("GPL");
