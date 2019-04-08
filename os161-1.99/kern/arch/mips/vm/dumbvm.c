/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "opt-A3.h"
#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>

/*
 * Dumb MIPS-only "VM system" that is intended to only be just barely
 * enough to struggle off the ground.
 */

/* under dumbvm, always have 48k of user stack */
#define DUMBVM_STACKPAGES    12

/*
 * Wrap rma_stealmem in a spinlock.
 */
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

#ifdef OPT_A3

volatile int numofFrames = 0;
volatile unsigned long num_pages = 0;

struct coremap {
    paddr_t paddr;
    bool is_used;
    unsigned long contiguous;
};

struct coremap *coremap;
bool isCoremapDone = false;
int phase = -1;//r


int find_index(unsigned long npages);

#endif


void
vm_bootstrap(void) {
#if OPT_A3
	paddr_t bottomPageAddress, topPageAddress;
    ram_getsize(&bottomPageAddress, &topPageAddress);
    coremap = (struct coremap *) PADDR_TO_KVADDR(bottomPageAddress);
    numofFrames = (topPageAddress - bottomPageAddress) / PAGE_SIZE;
    bottomPageAddress = numofFrames * (sizeof(struct coremap)) + bottomPageAddress;
    paddr_t startAddressing = ROUNDUP(bottomPageAddress, PAGE_SIZE);

    for (int i = 0; i < numofFrames; ++i) {
        coremap[i].is_used = false;
        coremap[i].contiguous = 0;
        coremap[i].paddr = startAddressing;
        startAddressing += PAGE_SIZE;
    }
    isCoremapDone = true;
    phase = 1;
#endif
}


int
find_index(unsigned long npages) {
	int index = -1;
	spinlock_acquire(&stealmem_lock);

	num_pages = 0;

	for (int i = 0; i < numofFrames && num_pages != npages; i++) {
		if (!coremap[i].is_used) {
			if (num_pages == 0) {
				index = i;
			}
			num_pages = num_pages + 1;
		} else {
			//reset
			num_pages = 0;
			index = -1;
		}
	}
	spinlock_release(&stealmem_lock);
	return index;
}

static
paddr_t
getppages(unsigned long npages) {
	paddr_t addr;
#if OPT_A3

	if (isCoremapDone) {
        int entry_point = find_index(npages);
        DEBUG(DB_VM, "getppages(): entry_point = %d\n", entry_point);

        if (entry_point == -1) {
            panic("getppages(): Cannot find desired memory block\n");
        }

        addr = coremap[entry_point].paddr;
        coremap[entry_point].contiguous = num_pages;

        unsigned long end_point = entry_point + num_pages;
        // mark them as in use
        for (unsigned int i = (unsigned int) entry_point; i < end_point; i++) {
            coremap[i].is_used = true;
        }

    } else {
        DEBUG(DB_VM, "getppages(): calling ram_stealmem()\n");
        addr = ram_stealmem(npages);
    }
    DEBUG(DB_VM, "getppages(): paddr is %d\n", addr);

    phase = 2;
    return addr;

#else
	spinlock_acquire(&stealmem_lock);
	paddr = ram_stealmem(npages);
	spinlock_release(&stealmem_lock);

	return paddr;
#endif
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t
alloc_kpages(int npages) {
	paddr_t pa;
	pa = getppages(npages);
	if (pa == 0) {
		return 0;
	}
	return PADDR_TO_KVADDR(pa);
}


void
free_kpages(vaddr_t addr) {
#if OPT_A3

	if (isCoremapDone && addr == 0) {
        return;
    }
    spinlock_acquire(&stealmem_lock);
    // physical
    paddr_t free_paddr = addr - MIPS_KSEG0;
    DEBUG(DB_VM, "free_kpages(): physical paddr = %d\n", free_paddr);


    for (int i = 0; i < numofFrames; i++) {
        if (coremap[i].paddr == addr) {
            coremap[i].is_used = false;
            if (coremap[i].contiguous == 0) {
                break;
            }
        }
    }
    DEBUG(DB_VM, "free_kpages(): freed paddr\n");


    for (int j = 0; j < numofFrames; j++) {
        if (coremap[j].paddr == free_paddr) {
            unsigned long end = coremap[j].contiguous;

            coremap[j].contiguous = 0;

            for (unsigned int k = 0; k < end; k++) {
                coremap[j + k].is_used = false;
            }
            break;
        }
    }
    DEBUG(DB_VM, "free_kpages(): freed all\n");

    spinlock_release(&stealmem_lock);
    phase = 3;
#else
	(void)paddr;
#endif
}

void
vm_tlbshootdown_all(void) {
	panic("dumbvm tried to do tlb shootdown?!\n");
}

void
vm_tlbshootdown(const struct tlbshootdown *ts) {
	(void) ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
}

////////////////////////////////////////////////////////////////
/*
vm fault uses information from an addrspace structure to determine a
page-to-frame mapping to load into the TLB
 – there is a separate addrspace structure for each process
 – each addrspace structure describes where its process’s pages are stored
in physical memory
 – an addrspace structure does the same job as a page table, but the
addrspace structure is simpler because OS/161 places all pages of each
segment contiguously in physical memory
 */
int
vm_fault(int faulttype, vaddr_t faultaddress) {
	DEBUG(DB_VM, "vm_fault(): startAddressing\n");

	vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
	paddr_t paddr;
	int i;
	uint32_t ehi, elo;
	struct addrspace *as;
	int spl;

	faultaddress &= PAGE_FRAME;

	DEBUG(DB_VM, "vm_fault(): fault: 0x%x\n", faultaddress);

	switch (faulttype) {
		case VM_FAULT_READONLY:
			/* We always create pages read-write, so we can't get this */
#if OPT_A3
			return EINVAL;
#else
			panic("dumbvm: got VM_FAULT_READONLY\n");
#endif
		case VM_FAULT_READ:
		case VM_FAULT_WRITE:
			break;
		default:
			return EINVAL;
	}

	if (curproc == NULL) {
		/*
         * No process. This is probably a kernel fault early
         * in boot. Return EFAULT so as to panic instead of
         * getting into an infinite faulting loop.
         */
		return EFAULT;
	}

	as = curproc_getas();
	if (as == NULL) {
		/*
         * No address space set up. This is probably also a
         * kernel fault early in boot.
         */
		return EFAULT;
	}

	KASSERT(as->as_vbase1 != 0);
	KASSERT(as->as_pbase1 != 0);
	KASSERT(as->as_npages1 != 0);
	KASSERT(as->as_vbase2 != 0);
	KASSERT(as->as_pbase2 != 0);
	KASSERT(as->as_npages2 != 0);
	KASSERT(as->as_stackpbase != 0);
	KASSERT((as->as_vbase1 & PAGE_FRAME) == as->as_vbase1);
	KASSERT((as->as_pbase1 & PAGE_FRAME) == as->as_pbase1);
	KASSERT((as->as_vbase2 & PAGE_FRAME) == as->as_vbase2);
	KASSERT((as->as_pbase2 & PAGE_FRAME) == as->as_pbase2);
	KASSERT((as->as_stackpbase & PAGE_FRAME) == as->as_stackpbase);


	vbase1 = as->as_vbase1;
	vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
	vbase2 = as->as_vbase2;
	vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
	stackbase = USERSTACK - DUMBVM_STACKPAGES * PAGE_SIZE;
	stacktop = USERSTACK;


#if OPT_A3
	phase = 4;
    bool valid = false;

    if (faultaddress >= vbase1 && faultaddress < vtop1) {
        paddr = as->as_pbase1 + (faultaddress - vbase1);
    } else if (faultaddress >= vbase2 && faultaddress < vtop2) {
        paddr = as->as_pbase2 + (faultaddress - vbase2);
    } else if (faultaddress >= stackbase && faultaddress < stacktop) {
        paddr = as->as_stackpbase + (faultaddress - stackbase);
    } else {
        return EFAULT;
    }
    DEBUG(DB_VM, "vm_fault(): set paddr\n");

#else

	if (faultaddress >= vbase1 && faultaddress < vtop1) {
		paddr = (faultaddress - vbase1) + as->as_pbase1;
	}
	else if (faultaddress >= vbase2 && faultaddress < vtop2) {
		paddr = (faultaddress - vbase2) + as->as_pbase2;
	}
	else if (faultaddress >= stackbase && faultaddress < stacktop) {
		paddr = (faultaddress - stackbase) + as->as_stackpbase;
	}
	else {
		return EFAULT;
	}
#endif


	/* make sure it's page-aligned */
	KASSERT((paddr & PAGE_FRAME) == paddr);

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i = 0; i < NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			continue;
		}
		ehi = faultaddress;
		elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
		DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);

#if OPT_A3
		valid = (faultaddress >= vbase1 && faultaddress < vtop1);
        if (valid && as->elf_loaded) {
            elo &= ~TLBLO_DIRTY;
            DEBUG(DB_VM, "vm_fault(): inside TLB loop: elo &= ~TLBLO_DIRTY\n");
        }
#endif
		DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);

		tlb_write(ehi, elo, i);
		splx(spl);
		return 0;
	}// end of for TLB
#if OPT_A3
	ehi = faultaddress;
    elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
    valid = (faultaddress >= vbase1 && faultaddress < vtop1);

    if (valid && as->elf_loaded) {
        elo &= ~TLBLO_DIRTY;
    }
    DEBUG(DB_VM, "vm_fault(): outside TLB loop; elo &= ~TLBLO_DIRTY\n");

    tlb_random(ehi, elo);
    splx(spl);
    return 0;

#else
	kprintf("dumbvm: Ran out of TLB entries - cannot handle page fault\n");
	splx(spl);
	return EFAULT;
#endif

}


struct addrspace *
as_create(void) {
	struct addrspace *as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}

#if OPT_A3
	as->elf_loaded = false;
    phase = 5;
#endif
	as->as_vbase1 = 0;
	as->as_pbase1 = 0;
	as->as_npages1 = 0;
	as->as_vbase2 = 0;
	as->as_pbase2 = 0;
	as->as_npages2 = 0;
	as->as_stackpbase = 0;
	DEBUG(DB_VM, "as_create(): as created.\n");

	return as;
}


void
as_destroy(struct addrspace *as) {
#if OPT_A3
	phase = 6;
    free_kpages(PADDR_TO_KVADDR(as->as_pbase2));
    free_kpages(PADDR_TO_KVADDR(as->as_pbase1));
    free_kpages(PADDR_TO_KVADDR(as->as_stackpbase));
    DEBUG(DB_VM, "as_destroy(): as destoried.\n");
#endif
	kfree(as);
}

void
as_activate(void) {
	int i, spl;
	struct addrspace *as;

	as = curproc_getas();
#ifdef UW
	/* Kernel threads don't have an address spaces to activate */
#endif
	if (as == NULL) {
		return;
	}

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

void
as_deactivate(void) {
	/* nothing */
}

int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
				 int readable, int writeable, int executable) {
	size_t npages;

	/* Align the region. First, the base... */
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;

	npages = sz / PAGE_SIZE;

	/* We don't use these - all pages are read-write */
	(void)readable;
	(void)writeable;
	(void)executable;

	if (as->as_vbase1 == 0) {
		as->as_vbase1 = vaddr;
		as->as_npages1 = npages;
		return 0;
	}

	if (as->as_vbase2 == 0) {
		as->as_vbase2 = vaddr;
		as->as_npages2 = npages;
		return 0;
	}

	/*
     * Support for more than two regions is not available.
     */
	kprintf("dumbvm: Warning: too many regions\n");
	return EUNIMP;
}

static
void
as_zero_region(paddr_t paddr, unsigned npages) {
	bzero((void *) PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}

int
as_prepare_load(struct addrspace *as) {

	KASSERT(as->as_pbase1 == 0);
	KASSERT(as->as_pbase2 == 0);
	KASSERT(as->as_stackpbase == 0);

	as->as_pbase1 = getppages(as->as_npages1);
	if (as->as_pbase1 == 0) {
		return ENOMEM;
	}

	as->as_pbase2 = getppages(as->as_npages2);
	if (as->as_pbase2 == 0) {
		return ENOMEM;
	}

	as->as_stackpbase = getppages(DUMBVM_STACKPAGES);
	if (as->as_stackpbase == 0) {
		return ENOMEM;
	}

	as_zero_region(as->as_pbase1, as->as_npages1);
	as_zero_region(as->as_pbase2, as->as_npages2);
	as_zero_region(as->as_stackpbase, DUMBVM_STACKPAGES);


	return 0;
}

int
as_complete_load(struct addrspace *as) {
#if OPT_A3
	phase = 7;
    as->elf_loaded = true;
#else
	(void)as;
#endif
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr) {

	KASSERT(as->as_stackpbase != 0);

	*stackptr = USERSTACK;
	return 0;
}

int
as_copy(struct addrspace *old, struct addrspace **ret) {
	struct addrspace *new;

	new = as_create();
	if (new == NULL) {
		return ENOMEM;
	}

	new->as_vbase1 = old->as_vbase1;
	new->as_npages1 = old->as_npages1;
	new->as_vbase2 = old->as_vbase2;
	new->as_npages2 = old->as_npages2;

	/* (Mis)use as_prepare_load to allocate some physical memory. */
	if (as_prepare_load(new)) {
		as_destroy(new);
		return ENOMEM;
	}


	KASSERT(new->as_pbase1 != 0);
	KASSERT(new->as_pbase2 != 0);
	KASSERT(new->as_stackpbase != 0);

	memmove((void *) PADDR_TO_KVADDR(new->as_pbase1),
			(const void *) PADDR_TO_KVADDR(old->as_pbase1),
			old->as_npages1 * PAGE_SIZE);

	memmove((void *) PADDR_TO_KVADDR(new->as_pbase2),
			(const void *) PADDR_TO_KVADDR(old->as_pbase2),
			old->as_npages2 * PAGE_SIZE);

	memmove((void *) PADDR_TO_KVADDR(new->as_stackpbase),
			(const void *) PADDR_TO_KVADDR(old->as_stackpbase),
			DUMBVM_STACKPAGES * PAGE_SIZE);


	*ret = new;
	return 0;
}
