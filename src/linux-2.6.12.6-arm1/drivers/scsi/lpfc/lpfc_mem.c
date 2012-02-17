/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Enterprise Fibre Channel Host Bus Adapters.                     *
 * Refer to the README file included with this package for         *
 * driver version and adapter support.                             *
 * Copyright (C) 2004 Emulex Corporation.                          *
 * www.emulex.com                                                  *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of the GNU General Public License     *
 * as published by the Free Software Foundation; either version 2  *
 * of the License, or (at your option) any later version.          *
 *                                                                 *
 * This program is distributed in the hope that it will be useful, *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of  *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the   *
 * GNU General Public License for more details, a copy of which    *
 * can be found in the file COPYING included with this package.    *
 *******************************************************************/

/*
 * $Id: lpfc_mem.c,v 1.1.1.1 2009/08/19 08:02:27 jack Exp $
 */

#include <linux/mempool.h>
#include <linux/pci.h>
#include <linux/interrupt.h>

#include "lpfc_hw.h"
#include "lpfc_sli.h"
#include "lpfc_disc.h"
#include "lpfc_scsi.h"
#include "lpfc.h"
#include "lpfc_crtn.h"

#define LPFC_MBUF_POOL_SIZE     64      /* max elements in MBUF safety pool */
#define LPFC_MEM_POOL_SIZE      64      /* max elem in non-DMA safety pool */

static void *
lpfc_pool_kmalloc(unsigned int gfp_flags, void *data)
{
	return kmalloc((unsigned long)data, gfp_flags);
}

static void
lpfc_pool_kfree(void *obj, void *data)
{
	kfree(obj);
}

int
lpfc_mem_alloc(struct lpfc_hba * phba)
{
	struct lpfc_dma_pool *pool = &phba->lpfc_mbuf_safety_pool;
	int i;

	phba->lpfc_scsi_dma_buf_pool = pci_pool_create("lpfc_scsi_dma_buf_pool",
				phba->pcidev, phba->cfg_sg_dma_buf_size, 8, 0);
	if (!phba->lpfc_scsi_dma_buf_pool)
		goto fail;

	phba->lpfc_mbuf_pool = pci_pool_create("lpfc_mbuf_pool", phba->pcidev,
							LPFC_BPL_SIZE, 8,0);
	if (!phba->lpfc_mbuf_pool)
		goto fail_free_dma_buf_pool;

	pool->elements = kmalloc(sizeof(struct lpfc_dmabuf) *
					 LPFC_MBUF_POOL_SIZE, GFP_KERNEL);
	pool->max_count = 0;
	pool->current_count = 0;
	for ( i = 0; i < LPFC_MBUF_POOL_SIZE; i++) {
		pool->elements[i].virt = pci_pool_alloc(phba->lpfc_mbuf_pool,
				       GFP_KERNEL, &pool->elements[i].phys);
		if (!pool->elements[i].virt)
			goto fail_free_mbuf_pool;
		pool->max_count++;
		pool->current_count++;
	}

	phba->mbox_mem_pool = mempool_create(LPFC_MEM_POOL_SIZE,
				lpfc_pool_kmalloc, lpfc_pool_kfree,
				(void *)(unsigned long)sizeof(LPFC_MBOXQ_t));
	if (!phba->mbox_mem_pool)
		goto fail_free_mbuf_pool;

	phba->nlp_mem_pool = mempool_create(LPFC_MEM_POOL_SIZE,
			lpfc_pool_kmalloc, lpfc_pool_kfree,
			(void *)(unsigned long)sizeof(struct lpfc_nodelist));
	if (!phba->nlp_mem_pool)
		goto fail_free_mbox_pool;

	return 0;

 fail_free_mbox_pool:
	mempool_destroy(phba->mbox_mem_pool);
 fail_free_mbuf_pool:
	while (--i)
		pci_pool_free(phba->lpfc_mbuf_pool, pool->elements[i].virt,
						 pool->elements[i].phys);
	kfree(pool->elements);
	pci_pool_destroy(phba->lpfc_mbuf_pool);
 fail_free_dma_buf_pool:
	pci_pool_destroy(phba->lpfc_scsi_dma_buf_pool);
 fail:
	return -ENOMEM;
}

void
lpfc_mem_free(struct lpfc_hba * phba)
{
	struct lpfc_sli *psli = &phba->sli;
	struct lpfc_dma_pool *pool = &phba->lpfc_mbuf_safety_pool;
	LPFC_MBOXQ_t *mbox, *next_mbox;
	struct lpfc_dmabuf   *mp;
	int i;

	list_for_each_entry_safe(mbox, next_mbox, &psli->mboxq, list) {
		mp = (struct lpfc_dmabuf *) (mbox->context1);
		if (mp) {
			lpfc_mbuf_free(phba, mp->virt, mp->phys);
			kfree(mp);
		}
		list_del(&mbox->list);
		mempool_free(mbox, phba->mbox_mem_pool);
	}

	psli->sli_flag &= ~LPFC_SLI_MBOX_ACTIVE;
	if (psli->mbox_active) {
		mbox = psli->mbox_active;
		mp = (struct lpfc_dmabuf *) (mbox->context1);
		if (mp) {
			lpfc_mbuf_free(phba, mp->virt, mp->phys);
			kfree(mp);
		}
		mempool_free(mbox, phba->mbox_mem_pool);
		psli->mbox_active = NULL;
	}

	for (i = 0; i < pool->current_count; i++)
		pci_pool_free(phba->lpfc_mbuf_pool, pool->elements[i].virt,
						 pool->elements[i].phys);
	kfree(pool->elements);
	mempool_destroy(phba->nlp_mem_pool);
	mempool_destroy(phba->mbox_mem_pool);

	pci_pool_destroy(phba->lpfc_scsi_dma_buf_pool);
	pci_pool_destroy(phba->lpfc_mbuf_pool);
}

void *
lpfc_mbuf_alloc(struct lpfc_hba *phba, int mem_flags, dma_addr_t *handle)
{
	struct lpfc_dma_pool *pool = &phba->lpfc_mbuf_safety_pool;
	void *ret;

	ret = pci_pool_alloc(phba->lpfc_mbuf_pool, GFP_KERNEL, handle);

	if (!ret && ( mem_flags & MEM_PRI) && pool->current_count) {
		pool->current_count--;
		ret = pool->elements[pool->current_count].virt;
		*handle = pool->elements[pool->current_count].phys;
	}
	return ret;
}

void
lpfc_mbuf_free(struct lpfc_hba * phba, void *virt, dma_addr_t dma)
{
	struct lpfc_dma_pool *pool = &phba->lpfc_mbuf_safety_pool;

	if (pool->current_count < pool->max_count) {
		pool->elements[pool->current_count].virt = virt;
		pool->elements[pool->current_count].phys = dma;
		pool->current_count++;
	} else {
		pci_pool_free(phba->lpfc_mbuf_pool, virt, dma);
	}
	return;
}
