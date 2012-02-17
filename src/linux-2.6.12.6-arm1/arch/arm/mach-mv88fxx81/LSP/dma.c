/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysdev.h>
#include <asm/mach/time.h>
#include <asm/uaccess.h>
#include <linux/proc_fs.h>

#include "mvIdma.h"

#undef DEBUG
//#define DEBUG

#ifdef DEBUG
	#define DPRINTK(s, args...)  printk("MV_DMA: " s, ## args)
#else
	#define DPRINTK(s, args...)
#endif

#undef CPY_USE_DESC
#undef RT_DEBUG
#define RT_DEBUG

#define CPY_IDMA_CTRL_LOW_VALUE      ICCLR_DST_BURST_LIM_128BYTE   \
                                    | ICCLR_SRC_BURST_LIM_128BYTE   \
                                    | ICCLR_INT_MODE_MASK           \
                                    | ICCLR_BLOCK_MODE              \
                                    | ICCLR_CHAN_ENABLE             \
                                    | ICCLR_DESC_MODE_16M

#define CPY_CHAN1	2
#define CPY_CHAN2	3

#define CPY_DMA_TIMEOUT	0x100000

#define MV_MAX_CPY_DMA_DESC 40
typedef struct {
	u32 buf_size;
	u32 desc_buf;
	u32 desc_phys_addr;
	MV_DMA_DESC* idma_desc;
} MV_CPY_DMA;



#define NEXT_CHANNEL(channel) ((CPY_CHAN1 + CPY_CHAN2) - (channel))
#define PREV_CHANNEL(channel) NEXT_CHANNEL(channel)
static MV_U32 current_dma_channel =  CPY_CHAN1;
static  spinlock_t      idma_lock = SPIN_LOCK_UNLOCKED;

#ifdef RT_DEBUG
static int dma_wait_loops = 0;
#endif

#define IDMA_MIN_COPY_CHUNK 128

static unsigned long dma_copy(void *to, const void *from, unsigned long n, unsigned int to_user);



//void * r_temp;
//u32 r_dummy;

static inline u32 page_remainder(u32 virt)
{
	return PAGE_SIZE - (virt & ~PAGE_MASK);
}


static inline int is_kernel_static(u32 virt)
{
	return((virt >= PAGE_OFFSET) && (virt < (unsigned long)high_memory));
}
static inline int is_kernel_space(u32 virt)
{
	return(virt >= TASK_SIZE);
}
/* 
 * map a kernel virtual address or kernel logical address to a phys address
 */

/* the patch-2.6.12-iop3 implementation of physical_address is buggy*/
#if 0
static inline u32 physical_address26(u32 virt)
{
       struct page *page;

       /* kernel static-mapped address */
       if (is_kernel_static((u32) virt)) {
               return __pa((u32) virt);
       }
       page = follow_page(current->mm, (u32) virt, 1);
       if (pfn_valid(page_to_pfn(page))) {
               return ((page_to_pfn(page) << PAGE_SHIFT) |
                       ((u32) virt & (PAGE_SIZE - 1)));
       } else {
               return 0;
       }
}
#endif
static inline u32 page_offset_to_phys(struct page *page, unsigned int offset)
{
        if(pfn_valid(page_to_pfn(page)))
        {
             return ((page_to_pfn(page) << PAGE_SHIFT) | offset);
        }
        else
        {
            BUG();
            return 0;
        }
}
static inline u32 physical_address(u32 virt)
{
        pgd_t *pgd;
        pud_t *pud;
        pmd_t *pmd;
        pte_t *pte;

        /* kernel static-mapped address */
        if(is_kernel_static(virt))
                return __pa((u32) virt);

        //printk(" warning in physical_address: virt address (%x) in not kernel static address\n", virt); 
        if (virt >= TASK_SIZE)
                pgd = pgd_offset_k(virt);
        else
                pgd = pgd_offset_gate(current->mm, virt);
        BUG_ON(pgd_none(*pgd));
        pud = pud_offset(pgd, virt);
        BUG_ON(pud_none(*pud));
        pmd = pmd_offset(pud, virt);
        BUG_ON(pmd_none(*pmd));
        pte = pte_offset_map(pmd, virt);
        BUG_ON(pte_none(*pte));

        return( (pte_val(*pte) & PAGE_MASK) | offset_in_page(virt));
}



void
hexdump(char *p, int n)
{
        int i, off;

        for (off = 0; n > 0; off += 16, n -= 16) {
                printk("%s%04x:", off == 0 ? "\n" : "", off);
                i = (n >= 16 ? 16 : n);
                do {
                        printk(" %02x", *p++ & 0xff);
                } while (--i);
                printk("\n");
        }
}


unsigned int wait_for_idma(MV_U32   channel)
{
	u32 timeout = 0;

	/* wait for completion */
        while( mvDmaStateGet(channel) != MV_IDLE )
	{
		DPRINTK(" ctrl low is %x \n", MV_REG_READ(IDMA_CTRL_LOW_REG(channel)));
		//udelay(1);
#ifdef RT_DEBUG
                dma_wait_loops++; 
#endif
		if(timeout++ > CPY_DMA_TIMEOUT)
                {
		    printk("dma_copy: IDMA %d timed out , ctrl low is %x \n",
                    channel, MV_REG_READ(IDMA_CTRL_LOW_REG(channel)));
                    return 1;
                }		
	}
	DPRINTK("IDMA complete in %x cause %x \n", timeout, temp);


#if 0
	/* wait for IDMA to complete */
	temp = MV_REG_READ(IDMA_CAUSE_REG);
	DPRINTK("wait for IDMA chan %d to complete \n", channel);
	while ((temp & (0xf << (8 * channel))) == 0)
	{
		DPRINTK("cause is %x , ctrl low is %x \n", temp, MV_REG_READ(IDMA_CTRL_LOW_REG(channel)));
		//udelay(1);
#ifdef RT_DEBUG
                dma_wait_loops++; 
#endif
		if(timeout++ > CPY_DMA_TIMEOUT)
                {
		    printk("dma_copy: IDMA %d timed out cause %x, ctrl low is %x \n",
                    channel, temp, MV_REG_READ(IDMA_CTRL_LOW_REG(channel)));
                    return 1;
                }
		temp = MV_REG_READ(IDMA_CAUSE_REG);
		
	}
	DPRINTK("IDMA complete in %x cause %x \n", timeout, temp);
	/* check if IDMA finished without errors */
	if((temp & (0x1e << (8 * channel))) != 0)
	{
	    printk("IDMA channel %d finished with error %x \n",channel, temp);
            return 1;
	}
	/* clear the cause reg */
	MV_REG_BYTE_WRITE(IDMA_CAUSE_REG + channel, 0xF0);
#endif

	return 0;

}

static struct proc_dir_entry *dma_proc_entry;
static int dma_to_user = 0;
static int dma_from_user = 0;
#ifdef RT_DEBUG
static int dma_activations = 0;
#endif
static int dma_read_proc(char *, char **, off_t, int, int *, void *);

static int dma_read_proc(char *buf, char **start, off_t offset, int len,
						 int *eof, void *data)
{
	len = 0;

	len += sprintf(buf + len, "Number of DMA copy to user %d copy from user %d \n", dma_to_user, dma_from_user);
#ifdef RT_DEBUG
	len += sprintf(buf + len, "Number of dma activations %d\n", dma_activations);
	len += sprintf(buf + len, "Number of wait for dma loops %d\n", dma_wait_loops);
#endif
        
	return len;
}

#if 0

/*=======================================================================*/
/*  Procedure:  dma_memcpy()                                             */
/*                                                                       */
/*  Description:    DMA-based in-kernel memcpy.                          */
/*                                                                       */
/*  Parameters:  to: destination address                                 */
/*               from: source address                                    */
/*               n: number of bytes to transfer                          */
/*                                                                       */
/*  Returns:     void*: to                                               */
/*                                                                       */
/*  Notes/Assumptions:                                                   */
/*              Assumes that kernel physical memory is contiguous, i.e., */
/*              the physical addresses of contiguous virtual addresses   */
/*              are also contiguous.                                     */
/*              Assumes that kernel memory doesn't get paged.            */
/*              Needs to be able to handle overlapping regions           */
/*              correctly.                                               */
/*                                                                       */
/*=======================================================================*/
void * dma_memcpy(void *to, const void *from, __kernel_size_t n)
{
	DPRINTK("dma_memcpy(0x%x, 0x%x, %lu): entering\n", (u32) to, (u32) from, (unsigned long) n);

	return asm_memcpy(to, from, n);
}
#endif

/*=======================================================================*/
/*  Procedure:  dma_copy_to_user()                                       */
/*                                                                       */
/*  Description:    DMA-based copy_to_user.                              */
/*                                                                       */
/*  Parameters:  to: destination address                                 */
/*               from: source address                                    */
/*               n: number of bytes to transfer                          */
/*                                                                       */
/*  Returns:     unsigned long: number of bytes NOT copied               */
/*                                                                       */
/*  Notes/Assumptions:                                                   */
/*              Assumes that kernel physical memory is contiguous, i.e., */
/*              the physical addresses of contiguous virtual addresses   */
/*              are also contiguous.                                     */
/*              Assumes that kernel memory doesn't get paged.            */
/*              Assumes that to/from memory regions cannot overlap       */
/*                                                                       */
/*=======================================================================*/
unsigned long dma_copy_to_user(void *to, const void *from, unsigned long n)
{
    dma_to_user++;
    DPRINTK(KERN_CRIT "dma_copy_to_user(%#10x, 0x%#10x, %lu): entering\n", (u32) to, (u32) from, n);
    
        return  dma_copy(to, from, n, 1);
}

/*=======================================================================*/
/*  Procedure:  dma_copy_from_user()                                     */
/*                                                                       */
/*  Description:    DMA-based copy_from_user.                            */
/*                                                                       */
/*  Parameters:  to: destination address                                 */
/*               from: source address                                    */
/*               n: number of bytes to transfer                          */
/*                                                                       */
/*  Returns:     unsigned long: number of bytes NOT copied               */
/*                                                                       */
/*  Notes/Assumptions:                                                   */
/*              Assumes that kernel virtual memory is contiguous, i.e.,  */
/*              the physical addresses of contiguous virtual addresses   */
/*              are also contiguous.                                     */
/*              Assumes that kernel memory doesn't get paged.            */
/*              Assumes that to/from memory regions cannot overlap       */
/*              XXX this one doesn't quite work right yet                */
/*                                                                       */
/*=======================================================================*/
unsigned long dma_copy_from_user(void *to, const void *from, unsigned long n)
{
	dma_from_user++;
	DPRINTK(KERN_CRIT "dma_copy_from_user(0x%x, 0x%x, %lu): entering\n", (u32) to, (u32) from, n);
	return  dma_copy(to, from, n, 0);
}


/*
 * n must be greater equal than 64.
 */
static unsigned long dma_copy(void *to, const void *from, unsigned long n, unsigned int to_user)
{
	u32 chunk,i;
	u32 k_chunk = 0;
	u32 u_chunk = 0;
	u32 phys_from, phys_to;
	
        unsigned long flags;
	u32 unaligned_to;
	u32 index = 0;
        u32 temp;

        unsigned long uaddr, kaddr, base_uaddr;
        int     res;
        int     nr_pages = 0;
        struct page **pages = NULL;
        unsigned char uaddr_kernel_space = 0;
        unsigned char uaddr_kernel_static = 0;
        unsigned char kaddr_kernel_static = 0;
	DPRINTK("dma_copy: entering\n");


	/* 
      	 * The unaligned is taken care seperatly since the dst might be part of a cache line that is changed 
	 * by other process -> we must not invalidate this cache lines and we can't also flush it, since other 
	 * process (or the exception handler) might fetch the cache line before we copied it. 
	 */

	/*
	 * Ok, start addr is not cache line-aligned, so we need to make it so.
	 */
	unaligned_to = (u32)to & 31;
	if(unaligned_to)
	{
		DPRINTK("Fixing up starting address %d bytes\n", 32 - unaligned_to);

		if(to_user)
			__arch_copy_to_user(to, from, 32 - unaligned_to);
		else
			__arch_copy_from_user(to, from, 32 - unaligned_to);

		temp = (u32)to + (32 - unaligned_to);
		to = (void *)temp;
		temp = (u32)from + (32 - unaligned_to);
		from = (void *)temp;

                /*it's ok, n supposed to be greater than 32 bytes at this point*/
		n -= (32 - unaligned_to);
	}

	/*
	 * Ok, we're aligned at the top, now let's check the end
	 * of the buffer and align that. After this we should have
	 * a block that is a multiple of cache line size.
	 */
	unaligned_to = ((u32)to + n) & 31;
	if(unaligned_to)
	{	
		u32 tmp_to = (u32)to + (n - unaligned_to);
		u32 tmp_from = (u32)from + (n - unaligned_to);
		DPRINTK("Fixing ending alignment %d bytes\n", unaligned_to);

		if(to_user)
			__arch_copy_to_user((void *)tmp_to, (void *)tmp_from, unaligned_to);
		else
			__arch_copy_from_user((void *)tmp_to, (void *)tmp_from, unaligned_to);

                /*it's ok, n supposed to be greater than 32 bytes at this point*/
		n -= unaligned_to;
	}

        if(to_user)
        {
            uaddr = (unsigned long)to;  
            kaddr = (unsigned long)from;
        }
        else
        {
             uaddr = (unsigned long)from;
             kaddr = (unsigned long)to;
        }
        if(is_kernel_static(kaddr))
        {
            kaddr_kernel_static = 1;
            k_chunk = n;
        }

        if(is_kernel_static(uaddr))
        {
            uaddr_kernel_static = 1;
            uaddr_kernel_space = 1;
            u_chunk = n;
        }
        else if(is_kernel_space(uaddr))
        {
            uaddr_kernel_space = 1;
        }
        else
        {
            nr_pages = ((uaddr & ~PAGE_MASK) + n + ~PAGE_MASK) >> PAGE_SHIFT;
    
            /* User attempted Overflow! */
            if ((uaddr + n) < uaddr)
            {
                printk("DMA_COPY: user buffer overflow detected. address 0x%lx  len 0x%lx\n", 
                        uaddr, n);
                goto error;
            }
            if ((pages = kmalloc(nr_pages * sizeof(*pages), GFP_KERNEL)) == NULL)
            {
                printk("DMA_COPY: failed to allocate buffer to pages. nr_pages %d\n", nr_pages);
                goto error;
            }   
            /* Try to fault in all of the necessary pages */
            down_read(&current->mm->mmap_sem);
            /* the get_user_pages_no_flush is a clone of get_user_pages() but it doesn't call flush_dcache_page */
            res = get_user_pages_no_flush(
                    current,
                    current->mm,
                    uaddr,
                    nr_pages,
                    to_user == 1,// 1 -> write,
                    0, /* don't force */
                    pages,
                    NULL);

            up_read(&current->mm->mmap_sem);
            DPRINTK(" map user pages: nr_pages %d, res %d , uaddr %lx, to_user %d\n",
                     nr_pages, res, uaddr, to_user);
            /* Errors and no page mapped should return here */
            if (res < nr_pages)
            {
                printk("error: map_user_pages retuned error. uaddr %lx res %d, nr_pages %d\n",
                        uaddr, res, nr_pages);
                printk(" is_kerenl_static(from) %d, is_kernel_static(to) %d\n",
                        is_kernel_static((u32)from) , is_kernel_static((u32)to));
                if (res > 0)
                {
                    for (i=0; i < res; i++)
                        page_cache_release(pages[i]);
                }
                goto free_pages;
            }
        }    

        base_uaddr = uaddr;
        
        DPRINTK("kaddr is static %d, uadd is kernel %d uaddr is kernel static %d\n",
                kaddr_kernel_static, uaddr_kernel_space, uaddr_kernel_static);

        spin_lock_irqsave(&idma_lock, flags);
        i = 0;
	while(n > 0)
	{
	    if(k_chunk == 0)
	    {
                /* virtual address */
	        k_chunk = page_remainder((u32)kaddr);
		DPRINTK("kaddr reminder %d \n",k_chunk);
	    }

	    if(u_chunk == 0)
	    {
                u_chunk = page_remainder((u32)uaddr);
                DPRINTK("uaddr reminder %d \n", u_chunk);
            }
        
            chunk = ((u_chunk < k_chunk) ? u_chunk : k_chunk);
            if(n < chunk)
	    {
		chunk = n;
	    }

	    if(chunk == 0)
	    {
	    	break;
	    }

	    DPRINTK("choose chunk %d \n",chunk);
	    /*
	     *  Prepare the IDMA.
	     */
            if (chunk < IDMA_MIN_COPY_CHUNK)
            {
        	DPRINTK(" chunk %d too small , use memcpy \n",chunk);
        	if(to_user)
	       	    __arch_copy_to_user((void *)to, (void *)from, chunk);
	        else
		    __arch_copy_from_user((void *)to, (void *)from, chunk);
            }
            else
            {
                /* 
	 	 * Ensure that the cache is clean:
	 	 *      - from range must be cleaned
        	 *      - to range must be invalidated
	         */
	        mvOsCacheFlush(NULL, (void *)from, chunk);
	        mvOsCacheInvalidate(NULL, (void *)to, chunk);
                if(uaddr_kernel_space == 0)
                {
                    i = uaddr - (base_uaddr & PAGE_MASK);
                    i >>= PAGE_SHIFT;
                    
                    if (to_user)
                    {
                        phys_from = physical_address((u32)from);
                        phys_to = page_offset_to_phys(pages[i], uaddr & ~PAGE_MASK);

                    }
                    else
                    {
                        phys_to = physical_address((u32)to);
                        phys_from = page_offset_to_phys(pages[i], uaddr & ~PAGE_MASK);
                    }
                    
                }
                else
                {
                    phys_from = physical_address((u32)from);
                    phys_to = physical_address((u32)to);
                }
               	if(index > 1)
		{
		    if(wait_for_idma(current_dma_channel))
                    {
		        BUG(); 
                        goto unlock_dma;
                    }
                }
		    /* Start DMA */
                    DPRINTK(" activate DMA: channel %d from %x to %x len %x\n",
                            current_dma_channel, phys_from, phys_to, chunk);
		    mvDmaTransfer(current_dma_channel, phys_from, phys_to, chunk, 0);
                    current_dma_channel = NEXT_CHANNEL(current_dma_channel); 
#ifdef RT_DEBUG
                    dma_activations++;
#endif
		    index++;
                }

		/* go to next chunk */
		from += chunk;
		to += chunk;
                kaddr += chunk;
                uaddr += chunk;
		n -= chunk;
		u_chunk -= chunk;
		k_chunk -= chunk;		
	}
        
        if (index > 1)
        {
	    if(wait_for_idma(current_dma_channel))
            {
	        BUG(); 
            }
        }

        if (index > 0)
        {
            if(wait_for_idma(PREV_CHANNEL(current_dma_channel)))
            {
	        BUG();
            }
        }

unlock_dma:    
        spin_unlock_irqrestore(&idma_lock, flags);
        
        DPRINTK(" release user pages: nr_pages = %d\n", nr_pages);
        for (i = 0; i < nr_pages; i++)
        {
            if (to_user && !PageReserved(pages[i]))
               SetPageDirty(pages[i]);
                    /* FIXME: cache flush missing for rw==READ
                    * FIXME: call the correct reference counting function
                    */
            page_cache_release(pages[i]);
        }
free_pages:        
        if (pages) kfree(pages);
        
        DPRINTK("dma_copy(0x%x, 0x%x, %lu): exiting\n", (u32) to,
                (u32) from, n);
       
error:
        if(n != 0)
        {
       	    if(to_user)
                return __arch_copy_to_user((void *)to, (void *)from, n);
	            else
                return __arch_copy_from_user((void *)to, (void *)from, n);
        }
        return 0;
}


int mv_dma_init(void)
{
	printk(KERN_INFO "use IDMA acceleration in copy to/from user buffers. used channels %d and %d \n",
                CPY_CHAN1, CPY_CHAN2);

        MV_REG_WRITE(IDMA_BYTE_COUNT_REG(CPY_CHAN1), 0);
        MV_REG_WRITE(IDMA_CURR_DESC_PTR_REG(CPY_CHAN1), 0);
        MV_REG_WRITE(IDMA_CTRL_HIGH_REG(CPY_CHAN1), ICCHR_ENDIAN_LITTLE | ICCHR_DESC_BYTE_SWAP_EN);
        MV_REG_WRITE(IDMA_CTRL_LOW_REG(CPY_CHAN1), CPY_IDMA_CTRL_LOW_VALUE);

        MV_REG_WRITE(IDMA_BYTE_COUNT_REG(CPY_CHAN2), 0);
        MV_REG_WRITE(IDMA_CURR_DESC_PTR_REG(CPY_CHAN2), 0);
        MV_REG_WRITE(IDMA_CTRL_HIGH_REG(CPY_CHAN2), ICCHR_ENDIAN_LITTLE | ICCHR_DESC_BYTE_SWAP_EN);
        MV_REG_WRITE(IDMA_CTRL_LOW_REG(CPY_CHAN2), CPY_IDMA_CTRL_LOW_VALUE);

        current_dma_channel = CPY_CHAN1;
	dma_proc_entry = create_proc_entry("dma_copy", S_IFREG | S_IRUGO, 0);
	dma_proc_entry->read_proc = dma_read_proc;
	dma_proc_entry->write_proc = NULL;
	dma_proc_entry->nlink = 1;

	printk(KERN_INFO "Done. \n");
	return 0;
}

void mv_dma_exit(void)
{
}

module_init(mv_dma_init);
module_exit(mv_dma_exit);
MODULE_LICENSE(GPL);

EXPORT_SYMBOL(dma_copy_to_user);
EXPORT_SYMBOL(dma_copy_from_user);			
