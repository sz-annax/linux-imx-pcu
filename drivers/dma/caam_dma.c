/*
 * caam support for SG DMA
 *
 * Copyright 2016 Freescale Semiconductor, Inc
 * Copyright 2017 NXP
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the names of the above-listed copyright holders nor the
 *       names of any contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "dmaengine.h"

#include "../crypto/caam/regs.h"
#include "../crypto/caam/jr.h"
#include "../crypto/caam/error.h"
#include "../crypto/caam/desc_constr.h"

#define DESC_DMA_MEMCPY_LEN	((CAAM_DESC_BYTES_MAX - DESC_JOB_IO_LEN_MIN) / \
				 CAAM_CMD_SZ)

/*
 * This is max chunk size of a DMA transfer. If a buffer is larger than this
 * value it is internally broken into chunks of max CAAM_DMA_CHUNK_SIZE bytes
 * and for each chunk a DMA transfer request is issued.
 * This value is the largest number on 16 bits that is a multiple of 256 bytes
 * (the largest configurable CAAM DMA burst size).
 */
#define CAAM_DMA_CHUNK_SIZE	65280

struct caam_dma_sh_desc {
	u32 desc[DESC_DMA_MEMCPY_LEN] ____cacheline_aligned;
	dma_addr_t desc_dma;
};

/* caam dma extended descriptor */
struct caam_dma_edesc {
	struct dma_async_tx_descriptor async_tx;
	struct list_head node;
	struct caam_dma_ctx *ctx;
	dma_addr_t src_dma;
	dma_addr_t dst_dma;
	unsigned int src_len;
	unsigned int dst_len;
	u32 jd[] ____cacheline_aligned;
};

/*
 * caam_dma_ctx - per jr/channel context
 * @chan: dma channel used by async_tx API
 * @node: list_head used to attach to the global dma_ctx_list
 * @jrdev: Job Ring device
 * @pending_q: queue of pending (submitted, but not enqueued) jobs
 * @done_not_acked: jobs that have been completed by jr, but maybe not acked
 * @edesc_lock: protects extended descriptor
 */
struct caam_dma_ctx {
	struct dma_chan chan;
	struct list_head node;
	struct device *jrdev;
	struct list_head pending_q;
	struct list_head done_not_acked;
	spinlock_t edesc_lock;
};

static struct dma_device *dma_dev;
static struct caam_dma_sh_desc *dma_sh_desc;
static LIST_HEAD(dma_ctx_list);

static dma_cookie_t caam_dma_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct caam_dma_edesc *edesc = NULL;
	struct caam_dma_ctx *ctx = NULL;
	dma_cookie_t cookie;

	edesc = container_of(tx, struct caam_dma_edesc, async_tx);
	ctx = container_of(tx->chan, struct caam_dma_ctx, chan);

	spin_lock_bh(&ctx->edesc_lock);

	cookie = dma_cookie_assign(tx);
	list_add_tail(&edesc->node, &ctx->pending_q);

	spin_unlock_bh(&ctx->edesc_lock);

	return cookie;
}

static void caam_jr_chan_free_edesc(struct caam_dma_edesc *edesc)
{
	struct caam_dma_ctx *ctx = edesc->ctx;
	struct caam_dma_edesc *_edesc = NULL;

	spin_lock_bh(&ctx->edesc_lock);

	list_add_tail(&edesc->node, &ctx->done_not_acked);
	list_for_each_entry_safe(edesc, _edesc, &ctx->done_not_acked, node) {
		if (async_tx_test_ack(&edesc->async_tx)) {
			list_del(&edesc->node);
			kfree(edesc);
		}
	}

	spin_unlock_bh(&ctx->edesc_lock);
}

static void caam_dma_done(struct device *dev, u32 *hwdesc, u32 err,
			  void *context)
{
	struct caam_dma_edesc *edesc = context;
	struct caam_dma_ctx *ctx = edesc->ctx;
	dma_async_tx_callback callback;
	void *callback_param;

	if (err)
		caam_jr_strstatus(ctx->jrdev, err);

	dma_run_dependencies(&edesc->async_tx);

	spin_lock_bh(&ctx->edesc_lock);
	dma_cookie_complete(&edesc->async_tx);
	spin_unlock_bh(&ctx->edesc_lock);

	callback = edesc->async_tx.callback;
	callback_param = edesc->async_tx.callback_param;

	dma_descriptor_unmap(&edesc->async_tx);

	caam_jr_chan_free_edesc(edesc);

	if (callback)
		callback(callback_param);
}

static void caam_dma_memcpy_init_job_desc(struct caam_dma_edesc *edesc)
{
	u32 *jd = edesc->jd;
	u32 *sh_desc = dma_sh_desc->desc;
	dma_addr_t desc_dma = dma_sh_desc->desc_dma;

	/* init the job descriptor */
	init_job_desc_shared(jd, desc_dma, desc_len(sh_desc), HDR_REVERSE);

	/* set SEQIN PTR */
	append_seq_in_ptr(jd, edesc->src_dma, edesc->src_len, 0);

	/* set SEQOUT PTR */
	append_seq_out_ptr(jd, edesc->dst_dma, edesc->dst_len, 0);

	print_hex_dump_debug("caam dma desc@" __stringify(__LINE__) ": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, jd, desc_bytes(jd), 1);
}

static struct dma_async_tx_descriptor *
caam_dma_prep_memcpy(struct dma_chan *chan, dma_addr_t dst, dma_addr_t src,
		     size_t len, unsigned long flags)
{
	struct caam_dma_edesc *edesc;
	struct caam_dma_ctx *ctx = container_of(chan, struct caam_dma_ctx,
						chan);

	edesc = kzalloc(sizeof(*edesc) + DESC_JOB_IO_LEN, GFP_DMA | GFP_NOWAIT);
	if (!edesc)
		return ERR_PTR(-ENOMEM);

	dma_async_tx_descriptor_init(&edesc->async_tx, chan);
	edesc->async_tx.tx_submit = caam_dma_tx_submit;
	edesc->async_tx.flags = flags;
	edesc->async_tx.cookie = -EBUSY;

	edesc->src_dma = src;
	edesc->src_len = len;
	edesc->dst_dma = dst;
	edesc->dst_len = len;
	edesc->ctx = ctx;

	caam_dma_memcpy_init_job_desc(edesc);

	return &edesc->async_tx;
}

/* This function can be called in an interrupt context */
static void caam_dma_issue_pending(struct dma_chan *chan)
{
	struct caam_dma_ctx *ctx = container_of(chan, struct caam_dma_ctx,
						chan);
	struct caam_dma_edesc *edesc, *_edesc;

	spin_lock_bh(&ctx->edesc_lock);
	list_for_each_entry_safe(edesc, _edesc, &ctx->pending_q, node) {
		int ret = caam_jr_enqueue(ctx->jrdev, edesc->jd,
					  caam_dma_done, edesc);
		if (ret != -EINPROGRESS)
			break;
		list_del(&edesc->node);
	}
	spin_unlock_bh(&ctx->edesc_lock);
}

static void caam_dma_free_chan_resources(struct dma_chan *chan)
{
	struct caam_dma_ctx *ctx = container_of(chan, struct caam_dma_ctx,
						chan);
	struct caam_dma_edesc *edesc, *_edesc;

	spin_lock_bh(&ctx->edesc_lock);
	list_for_each_entry_safe(edesc, _edesc, &ctx->pending_q, node) {
		list_del(&edesc->node);
		kfree(edesc);
	}
	list_for_each_entry_safe(edesc, _edesc, &ctx->done_not_acked, node) {
		list_del(&edesc->node);
		kfree(edesc);
	}
	spin_unlock_bh(&ctx->edesc_lock);
}

static int caam_dma_jr_chan_bind(void)
{
	struct device *jrdev;
	struct caam_dma_ctx *ctx;
	int bonds = 0;
	int i;

	for (i = 0; i < caam_jr_driver_probed(); i++) {
		jrdev = caam_jridx_alloc(i);
		if (IS_ERR(jrdev)) {
			pr_err("job ring device %d allocation failed\n", i);
			continue;
		}

		ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
		if (!ctx) {
			caam_jr_free(jrdev);
			continue;
		}

		ctx->chan.device = dma_dev;
		ctx->chan.private = ctx;

		ctx->jrdev = jrdev;

		INIT_LIST_HEAD(&ctx->pending_q);
		INIT_LIST_HEAD(&ctx->done_not_acked);
		INIT_LIST_HEAD(&ctx->node);
		spin_lock_init(&ctx->edesc_lock);

		dma_cookie_init(&ctx->chan);

		/* add the context of this channel to the context list */
		list_add_tail(&ctx->node, &dma_ctx_list);

		/* add this channel to the device chan list */
		list_add_tail(&ctx->chan.device_node, &dma_dev->channels);

		bonds++;
	}

	return bonds;
}

static inline void caam_jr_dma_free(struct dma_chan *chan)
{
	struct caam_dma_ctx *ctx = container_of(chan, struct caam_dma_ctx,
						chan);

	list_del(&ctx->node);
	list_del(&chan->device_node);
	caam_jr_free(ctx->jrdev);
	kfree(ctx);
}

static void set_caam_dma_desc(u32 *desc)
{
	u32 *jmp_cmd;

	/* dma shared descriptor */
	init_sh_desc(desc, HDR_SHARE_NEVER | (1 << HDR_START_IDX_SHIFT));

	/* REG1 = CAAM_DMA_CHUNK_SIZE */
	append_math_add_imm_u32(desc, REG1, ZERO, IMM, CAAM_DMA_CHUNK_SIZE);

	/* REG0 = SEQINLEN - CAAM_DMA_CHUNK_SIZE */
	append_math_sub_imm_u32(desc, REG0, SEQINLEN, IMM, CAAM_DMA_CHUNK_SIZE);

	/*
	 * if (REG0 > 0)
	 *	jmp to LABEL1
	 */
	jmp_cmd = append_jump(desc, JUMP_TEST_INVALL | JUMP_COND_MATH_N |
			      JUMP_COND_MATH_Z);

	/* REG1 = SEQINLEN */
	append_math_sub(desc, REG1, SEQINLEN, ZERO, CAAM_CMD_SZ);

	/* LABEL1 */
	set_jump_tgt_here(desc, jmp_cmd);

	/* VARSEQINLEN = REG1 */
	append_math_add(desc, VARSEQINLEN, REG1, ZERO, CAAM_CMD_SZ);

	/* VARSEQOUTLEN = REG1 */
	append_math_add(desc, VARSEQOUTLEN, REG1, ZERO, CAAM_CMD_SZ);

	/* do FIFO STORE */
	append_seq_fifo_store(desc, 0, FIFOST_TYPE_METADATA | LDST_VLF);

	/* do FIFO LOAD */
	append_seq_fifo_load(desc, 0, FIFOLD_CLASS_CLASS1 |
			     FIFOLD_TYPE_IFIFO | LDST_VLF);

	/*
	 * if (REG0 > 0)
	 *	jmp 0xF8 (after shared desc header)
	 */
	append_jump(desc, JUMP_TEST_INVALL | JUMP_COND_MATH_N |
		    JUMP_COND_MATH_Z | 0xF8);

	print_hex_dump_debug("caam dma shdesc@" __stringify(__LINE__) ": ",
			     DUMP_PREFIX_ADDRESS, 16, 4, desc, desc_bytes(desc),
			     1);
}

static int caam_dma_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device *ctrldev = dev->parent;
	struct dma_chan *chan, *_chan;
	u32 *sh_desc;
	int err = -ENOMEM;
	int bonds;

	if (!caam_jr_driver_probed()) {
		dev_info(dev, "Defer probing after JR driver probing\n");
		return -EPROBE_DEFER;
	}

	dma_dev = kzalloc(sizeof(*dma_dev), GFP_KERNEL);
	if (!dma_dev)
		return -ENOMEM;

	dma_sh_desc = kzalloc(sizeof(*dma_sh_desc), GFP_KERNEL | GFP_DMA);
	if (!dma_sh_desc)
		goto desc_err;

	sh_desc = dma_sh_desc->desc;
	set_caam_dma_desc(sh_desc);
	dma_sh_desc->desc_dma = dma_map_single(ctrldev, sh_desc,
					       desc_bytes(sh_desc),
					       DMA_TO_DEVICE);
	if (dma_mapping_error(ctrldev, dma_sh_desc->desc_dma)) {
		dev_err(dev, "unable to map dma descriptor\n");
		goto map_err;
	}

	INIT_LIST_HEAD(&dma_dev->channels);

	bonds = caam_dma_jr_chan_bind();
	if (!bonds) {
		err = -ENODEV;
		goto jr_bind_err;
	}

	dma_dev->dev = dev;
	dma_dev->residue_granularity = DMA_RESIDUE_GRANULARITY_DESCRIPTOR;
	dma_cap_set(DMA_MEMCPY, dma_dev->cap_mask);
	dma_cap_set(DMA_PRIVATE, dma_dev->cap_mask);
	dma_dev->device_tx_status = dma_cookie_status;
	dma_dev->device_issue_pending = caam_dma_issue_pending;
	dma_dev->device_prep_dma_memcpy = caam_dma_prep_memcpy;
	dma_dev->device_free_chan_resources = caam_dma_free_chan_resources;

	err = dma_async_device_register(dma_dev);
	if (err) {
		dev_err(dev, "Failed to register CAAM DMA engine\n");
		goto jr_bind_err;
	}

	dev_info(dev, "caam dma support with %d job rings\n", bonds);

	return err;

jr_bind_err:
	list_for_each_entry_safe(chan, _chan, &dma_dev->channels, device_node)
		caam_jr_dma_free(chan);

	dma_unmap_single(ctrldev, dma_sh_desc->desc_dma, desc_bytes(sh_desc),
			 DMA_TO_DEVICE);
map_err:
	kfree(dma_sh_desc);
desc_err:
	kfree(dma_dev);
	return err;
}

static void caam_dma_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device *ctrldev = dev->parent;
	struct caam_dma_ctx *ctx, *_ctx;

	dma_async_device_unregister(dma_dev);

	list_for_each_entry_safe(ctx, _ctx, &dma_ctx_list, node) {
		list_del(&ctx->node);
		caam_jr_free(ctx->jrdev);
		kfree(ctx);
	}

	dma_unmap_single(ctrldev, dma_sh_desc->desc_dma,
			 desc_bytes(dma_sh_desc->desc), DMA_TO_DEVICE);

	kfree(dma_sh_desc);
	kfree(dma_dev);

	dev_info(dev, "caam dma support disabled\n");
}

static struct platform_driver caam_dma_driver = {
	.driver = {
		.name = "caam-dma",
	},
	.probe  = caam_dma_probe,
	.remove = caam_dma_remove,
};
module_platform_driver(caam_dma_driver);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("NXP CAAM support for DMA engine");
MODULE_AUTHOR("NXP Semiconductors");
MODULE_ALIAS("platform:caam-dma");
