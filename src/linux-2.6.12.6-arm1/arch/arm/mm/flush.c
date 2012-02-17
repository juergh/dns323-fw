/*
 *  linux/arch/arm/mm/flush.c
 *
 *  Copyright (C) 1995-2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/shm.h>

#include <asm/cacheflush.h>
#include <asm/system.h>
#include <asm/tlbflush.h>

#ifdef CONFIG_CPU_CACHE_VIPT
#define CACHE_COLOUR(vaddr)	(((vaddr) & (SHMLBA - 1)) >> PAGE_SHIFT)
#define ALIAS_FLUSH_START	0xffff4000

#define TOP_PTE(x)	pte_offset_kernel(top_pmd, x)

static void flush_pfn_alias(unsigned long pfn, unsigned long vaddr, unsigned long flags)
{
	unsigned long to = ALIAS_FLUSH_START + (CACHE_COLOUR(vaddr) << PAGE_SHIFT);

	set_pte(TOP_PTE(to), pfn_pte(pfn, PAGE_KERNEL));
	flush_tlb_kernel_page(to);

	__cpuc_flush_user_range(to, to + PAGE_SIZE, flags);
}
#else
#define flush_pfn_alias(pfn,vaddr,flags)	do { } while (0)
#endif

static void __flush_dcache_page(struct address_space *mapping, struct page *page)
{
	struct mm_struct *mm = current->active_mm;
	struct vm_area_struct *mpnt;
	struct prio_tree_iter iter;
	pgoff_t pgoff;

	/*
	 * Writeback any data associated with the kernel mapping of this
	 * page.  This ensures that data in the physical page is mutually
	 * coherent with the kernels mapping.
	 */
	__cpuc_flush_dcache_page(page_address(page));

	/*
	 * If there's no mapping pointer here, then this page isn't
	 * visible to userspace yet, so there are no cache lines
	 * associated with any other aliases.
	 */
	if (!mapping)
		return;

	/*
	 * This is a page cache page.  If we have a VIPT cache, we
	 * only need to do one flush - which would be at the relevant
	 * userspace colour, which is congruent with page->index.
	 *
	 * FIXME: should we set the VM_EXEC bit for flush_pfn_alias()?
	 */
	if (cache_is_vipt()) {
		if (cache_is_vipt_aliasing())
			flush_pfn_alias(page_to_pfn(page),
					page->index << PAGE_CACHE_SHIFT, 0);
		return;
	}

	/*
	 * There are possible user space mappings of this page:
	 * - VIVT cache: we need to also write back and invalidate all user
	 *   data in the current VM view associated with this page.
	 * - aliasing VIPT: we only need to find one mapping of this page.
	 */
	pgoff = page->index << (PAGE_CACHE_SHIFT - PAGE_SHIFT);

	flush_dcache_mmap_lock(mapping);
	vma_prio_tree_foreach(mpnt, &iter, &mapping->i_mmap, pgoff, pgoff) {
		unsigned long offset;

		/*
		 * If this VMA is not in our MM, we can ignore it.
		 */
		if (mpnt->vm_mm != mm)
			continue;
		if (!(mpnt->vm_flags & VM_MAYSHARE))
			continue;
		offset = (pgoff - mpnt->vm_pgoff) << PAGE_SHIFT;
		flush_cache_page(mpnt, mpnt->vm_start + offset, page_to_pfn(page));
	}
	flush_dcache_mmap_unlock(mapping);
}

/*
 * Ensure cache coherency between kernel mapping and userspace mapping
 * of this page.
 *
 * We have three cases to consider:
 *  - VIPT non-aliasing cache: fully coherent so nothing required.
 *  - VIVT: fully aliasing, so we need to handle every alias in our
 *          current VM view.
 *  - VIPT aliasing: need to handle one alias in our current VM view.
 *
 * If we need to handle aliasing:
 *  If the page only exists in the page cache and there are no user
 *  space mappings, we can be lazy and remember that we may have dirty
 *  kernel cache lines for later.  Otherwise, we assume we have
 *  aliasing mappings.
 */
void flush_dcache_page(struct page *page)
{
	struct address_space *mapping = page_mapping(page);

	if (mapping && !mapping_mapped(mapping))
		set_bit(PG_dcache_dirty, &page->flags);
	else
		__flush_dcache_page(mapping, page);
}
EXPORT_SYMBOL(flush_dcache_page);

void flush_cache_mm(struct mm_struct *mm)
{
	if ((cache_is_vivt() && current->active_mm == mm)
	    || cache_is_vipt_aliasing())
		__cpuc_flush_user_all();
}

void flush_cache_range(struct vm_area_struct *vma, unsigned long start, unsigned long end)
{
	if (current->active_mm != vma->vm_mm)
		return;

	if (cache_is_vivt()) {
		__cpuc_flush_user_range(start & PAGE_MASK, PAGE_ALIGN(end),
					vma->vm_flags);
		return;
	}

	if (cache_is_vipt_aliasing()) {
		unsigned long addr;
		start &= PAGE_MASK;
		end = PAGE_ALIGN(end);
		for (addr = start; addr < end; addr += PAGE_SIZE) {
			pgd_t *pgd;
			pmd_t *pmd;
			pte_t *pte, entry;

			pgd = pgd_offset(vma->vm_mm, addr);
			if (pgd_none(*pgd) || pgd_bad(*pgd))
				continue;

			pmd = pmd_offset(pgd, addr);
			if (pmd_none(*pmd) || pmd_bad(*pmd))
				continue;

			pte = pte_offset_map(pmd, addr);
			entry = *pte;
			if (!pte_present(entry))
				continue;

			__cpuc_flush_user_range(addr, addr + PAGE_SIZE,
						vma->vm_flags);
		}
	}
}

void flush_cache_page(struct vm_area_struct *vma, unsigned long user_addr, unsigned long pfn)
{
	if (cache_is_vivt()) {
		if (current->active_mm == vma->vm_mm) {
			unsigned long addr = user_addr & PAGE_MASK;
			__cpuc_flush_user_range(addr, addr + PAGE_SIZE, vma->vm_flags);
		}
		return;
	}

	if (cache_is_vipt_aliasing())
		flush_pfn_alias(pfn, user_addr, vma->vm_flags);
	else if (vma->vm_flags & VM_EXEC) {
		/* We only flush the kernel range since the cache is VIPT
		 * non-aliasing. This is needed for ptrace to work properly
		 */
		unsigned long addr = (unsigned long) page_address(pfn_to_page(pfn));
		flush_icache_range(addr, addr + PAGE_SIZE);
	}
}
