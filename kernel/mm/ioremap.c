#include <os/ioremap.h>
#include <os/mm.h>
#include <pgtable.h>
#include <type.h>

// maybe you can map it to IO_ADDR_START ?
static uintptr_t io_base = IO_ADDR_START;

static void ioremap_helper(uint64_t va, uint64_t pa, PTE *pgdir)
{
    va &= VA_MASK;
    uint64_t vpn2 =
        va >> (NORMAL_PAGE_SHIFT + PPN_BITS + PPN_BITS);
    uint64_t vpn1 = (vpn2 << PPN_BITS) ^
                    (va >> (NORMAL_PAGE_SHIFT + PPN_BITS));
    if (pgdir[vpn2] == 0) {
        // alloc a new second-level page directory
        set_pfn(&pgdir[vpn2], GetPFN(kva2pa(allocPage(1))));
        set_attribute(&pgdir[vpn2], _PAGE_PRESENT);
        clear_pgdir(pa2kva(get_pa(pgdir[vpn2])));
    }
    PTE *pmd = (PTE *)pa2kva(get_pa(pgdir[vpn2]));
    set_pfn(&pmd[vpn1], GetPFN(pa));
    set_attribute(
        &pmd[vpn1], _PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE |
                        _PAGE_EXEC | _PAGE_ACCESSED | _PAGE_DIRTY);
}

void *ioremap(unsigned long phys_addr, unsigned long size)
{
    // TODO: [p5-task1] map one specific physical region to virtual address
    void* ret = io_base;
    uint64_t kva;
    io_base += size;
    kva = ret;

    while(kva < io_base)
    {
        ioremap_helper(kva, phys_addr, pa2kva(PGDIR_PA));
        kva += LARGE_PAGE_SIZE;
        phys_addr += LARGE_PAGE_SIZE;
    }
    
    local_flush_tlb_all();
    return ret;
}

void iounmap(void *io_addr)
{
    // TODO: [p5-task1] a very naive iounmap() is OK
    // maybe no one would call this function?
}