#include <os/mm.h>
#include <os/string.h>
#include <os/kernel.h>
#include <assert.h>


// NOTE: A/C-core
static ptr_t kernMemCurr = FREEMEM_KERNEL;
page_info_t page_manager[PAGE_NUM];
LIST_HEAD(free_list);

page_info_t * allocPagetable(pcb_t* pcb_node, page_status_t page_status/*, uint64_t addr*/)
{
    static int page_id = 0;
    page_info_t* ret_page;
    uintptr_t kva;

    // * 先判断是否已满
    if(pcb_node->pagetable.pagenum == pcb_node->pagetable.upbound)
    {// 满则swap, 得到一个可用的kva
        kva = do_swap(pcb_node);
        while(1){// 然后找一张可用的表
            ret_page = &page_manager[page_id++];
            if(ret_page->status == UNUSED){
                ret_page->status = page_status;
                ret_page->kva = kva;
                break;
            }else{
                if(page_id == PAGE_NUM){// 重新开始搜索
                    page_id = 0;
                }
            }
        }
    }else{// 不满则进入正常分配
        // if(!list_is_empty(&free_list)){// 优先找free_list
        //     ret_page = get_first_page(&free_list);// 其中可能有许多杂乱东西, 先clear一下
        //     ret_page->status = page_status;
        //     clear_pgdir(ret_page->kva);
        // }else{
            while(1){
                ret_page = &page_manager[page_id++];
                if(ret_page->status == UNUSED){
                    ret_page->status = page_status;
                    ret_page->kva = allocPage(1);// 否则alloc_page
                    break;
                }else{
                    if(page_id == PAGE_NUM){// 重新开始搜索
                        page_id = 0;
                    }
                }
            }
        // }// 正常分配要增加num
        (pcb_node->pagetable.pagenum)++;
    }

    _list_add_tail(&ret_page->list, &pcb_node->pagetable.pagehead);
    
    return ret_page;
}

void init_pagetable()
{
    for(int i = 0; i != PAGE_NUM; ++i)
    {
        page_manager[i].pte = 0;
        page_manager[i].status = UNUSED;
        page_manager[i].read_member = 0;
        // page_manager[i].kva = allocPage(1);
        page_manager[i].list.prev = NULL;
        page_manager[i].list.next = NULL;
    }
    for(int i = 0; i != NUM_MAX_SHM; ++i)
    {
        shm_page_table[i].valid = 0;
        shm_page_table[i].key = 0;
        shm_page_table[i].num = 0;
        shm_page_table[i].pa = 0;
    }
}

page_info_t* get_first_page(list_head* head)
{
    // if(list_is_empty(head)) return NULL;
    list_head * list = head->next;
    page_info_t* tmp = list_entry(list, page_info_t, list);
    del_queue(list);
    return tmp;
}

ptr_t allocPage(int numPage)
{
    // align PAGE_SIZE
    ptr_t ret = ROUND(kernMemCurr, PAGE_SIZE);
    kernMemCurr = ret + numPage * PAGE_SIZE;
    return ret;
}

// NOTE: Only need for S-core to alloc 2MB large page
#ifdef S_CORE
static ptr_t largePageMemCurr = LARGE_PAGE_FREEMEM;
ptr_t allocLargePage(int numPage)
{
    // align LARGE_PAGE_SIZE
    ptr_t ret = ROUND(largePageMemCurr, LARGE_PAGE_SIZE);
    largePageMemCurr = ret + numPage * LARGE_PAGE_SIZE;
    return ret;    
}
#endif

void freePage(page_info_t* to_free_page)
{
    // TODO [P4-task1] (design you 'freePage' here if you need):
    to_free_page->status = UNUSED;
    del_queue(&to_free_page->list);
    to_free_page->uva = 0;
    _list_add_tail(&to_free_page->list, &free_list);
    assert(to_free_page->kva != 0);
}

void *kmalloc(size_t size)
{
    // TODO [P4-task1] (design you 'kmalloc' here if you need):
}


/* this is used for mapping kernel virtual address into user page table */
void share_pgtable(uintptr_t dest_pgdir, uintptr_t src_pgdir)
{
    // TODO [P4-task1] share_pgtable:
    memcpy((char *)dest_pgdir, (char *)src_pgdir, PAGE_SIZE);
}

/* allocate physical page for `va`, mapping it into `pgdir`,
   return the kernel virtual address for the page
   */
uintptr_t alloc_page_helper(uintptr_t va, pcb_t* map_pcb, page_status_t page_status/*uintptr_t pgdir*/)
{
    // TODO [P4-task1] alloc_page_helper:
    uint64_t vpn2, vpn1, vpn0;
    PTE * first_pgdir, *second_pgdir, *third_pgdir, *pte;
    PTE cur_pte;
    page_info_t* temp_page;
    // 开始顶级页表映射
    vpn2 = GetVPN2(va);
    first_pgdir = (PTE *)(map_pcb->pgdir);
    cur_pte = first_pgdir[vpn2];
    // 检查是否合法
    if(PageValid(cur_pte)){// 是则可以得到下级页表位置
        second_pgdir = pa2kva(get_pa(cur_pte));
    }else{ // 否则需分配并初始化
        temp_page = allocPagetable(map_pcb, UNSWAPABLE);
        second_pgdir = temp_page->kva;
        clear_pgdir(second_pgdir);
        set_pfn(&first_pgdir[vpn2], GetPFN(kva2pa(second_pgdir)));
        set_attribute(&first_pgdir[vpn2], _PAGE_PRESENT | _PAGE_USER);
    }

    // 开始二级页表映射
    vpn1 = GetVPN1(va);
    cur_pte = second_pgdir[vpn1];
    if(PageValid(cur_pte)){
        third_pgdir = pa2kva(get_pa(cur_pte));
    }else{
        temp_page = allocPagetable(map_pcb, UNSWAPABLE);
        third_pgdir = temp_page->kva;
        clear_pgdir(third_pgdir);
        set_pfn(&second_pgdir[vpn1], GetPFN(kva2pa(third_pgdir)));
        set_attribute(&second_pgdir[vpn1], _PAGE_PRESENT | _PAGE_USER);
    }

    // 开始第三级页表映射
    vpn0 = GetVPN0(va);
    cur_pte = third_pgdir[vpn0];
    if(!PageValid(cur_pte)){ // 第三级页表有些不同
        temp_page = allocPagetable(map_pcb, page_status);
        temp_page->uva = va;
        temp_page->pte = &third_pgdir[vpn0];
        pte = temp_page->kva;
        set_pfn(&third_pgdir[vpn0], GetPFN(kva2pa(pte)));
        set_attribute(&third_pgdir[vpn0], _PAGE_ACCESSED | _PAGE_DIRTY | _PAGE_PRESENT | _PAGE_USER | _PAGE_READ | _PAGE_WRITE | _PAGE_EXEC);
        return pte;
    }
    // 返回kva
    return pa2kva(get_pa(cur_pte));
}

void build_page_helper(uintptr_t va, pcb_t* map_pcb, page_status_t page_status, uintptr_t pa)
{
    // TODO [P4-task1] alloc_page_helper:
    uint64_t vpn2, vpn1, vpn0;
    PTE * first_pgdir, *second_pgdir, *third_pgdir;
    PTE cur_pte;
    page_info_t* temp_page;
    // 开始顶级页表映射
    vpn2 = GetVPN2(va);
    first_pgdir = (PTE *)(map_pcb->pgdir);
    cur_pte = first_pgdir[vpn2];
    // 检查是否合法
    if(PageValid(cur_pte)){// 是则可以得到下级页表位置
        second_pgdir = pa2kva(get_pa(cur_pte));
    }else{ // 否则需分配并初始化
        temp_page = allocPagetable(map_pcb, UNSWAPABLE);
        second_pgdir = temp_page->kva;
        clear_pgdir(second_pgdir);
        set_pfn(&first_pgdir[vpn2], GetPFN(kva2pa(second_pgdir)));
        set_attribute(&first_pgdir[vpn2], _PAGE_PRESENT | _PAGE_USER);
    }

    // 开始二级页表映射
    vpn1 = GetVPN1(va);
    cur_pte = second_pgdir[vpn1];
    if(PageValid(cur_pte)){
        third_pgdir = pa2kva(get_pa(cur_pte));
    }else{
        temp_page = allocPagetable(map_pcb, UNSWAPABLE);
        third_pgdir = temp_page->kva;
        clear_pgdir(third_pgdir);
        set_pfn(&second_pgdir[vpn1], GetPFN(kva2pa(third_pgdir)));
        set_attribute(&second_pgdir[vpn1], _PAGE_PRESENT | _PAGE_USER);
    }

    // 开始第三级页表映射
    vpn0 = GetVPN0(va);
    cur_pte = third_pgdir[vpn0];
    if(!PageValid(cur_pte)){ // 第三级页表有些不同
        // temp_page = allocPagetable(map_pcb, page_status);
        // temp_page->uva = va;
        // pte = temp_page->kva;
        set_pfn(&third_pgdir[vpn0], GetPFN(pa));
        set_attribute(&third_pgdir[vpn0], _PAGE_ACCESSED | _PAGE_DIRTY | _PAGE_PRESENT | _PAGE_USER | _PAGE_READ | _PAGE_WRITE | _PAGE_EXEC);
    }
    // 返回kva
    return;
}

uintptr_t shm_page_get(int key)
{
    // TODO [P4-task4] shm_page_get:
    pcb_t* current_running = core_running[get_current_cpu_id()];
    page_info_t* tmp;
    int index;
    for(index = 0; index != NUM_MAX_SHM; ++index)
    {
        if(shm_page_table[index].key == key){// 如果key相等, 则看是否valid, 是valid则break
            if(shm_page_table[index].valid) break;
            else continue;
        }
    }
    if(index == NUM_MAX_SHM){// 说明, 以key的方式, 没有找到, 尝试找一个unvalid
        for(index = 0; index != NUM_MAX_SHM; ++index)
        {
            if(!shm_page_table[index].valid){// 如果不合法, 则可以分配
                shm_page_table[index].key = key;
                // shm_page_table[index].valid = 1;
                break;
            }
        }
    }
    if(index == NUM_MAX_SHM) assert(0);// ? 还找不到, 开摆

    if(shm_page_table[index].valid == 0){// 第一次来, 需要初始化
        tmp = allocPagetable(current_running, UNSWAPABLE);
        uintptr_t pa = kva2pa(tmp->kva);
        shm_page_table[index].pa = pa;
        current_running->shm_page_num++;
        uintptr_t vaddr = USER_STACK_ADDR - (current_running->shm_page_num + 1) * PAGE_SIZE;
        build_page_helper(vaddr, current_running, UNSWAPABLE, pa);
        local_flush_tlb_all();
        clear_pgdir(pa2kva(pa));
        shm_page_table[index].num++;
        shm_page_table[index].valid = 1;
        return vaddr;
    }
    else{
        current_running->shm_page_num++;
        uintptr_t vaddr = USER_STACK_ADDR - (current_running->shm_page_num + 1) * PAGE_SIZE;
        uintptr_t pa = shm_page_table[index].pa;
        build_page_helper(vaddr, current_running, UNSWAPABLE, pa);
        local_flush_tlb_all();
        shm_page_table[index].num++;
        return vaddr;
    }
}

void shm_page_dt(uintptr_t addr)
{
    // TODO [P4-task4] shm_page_dt:
    pcb_t* current_running = core_running[get_current_cpu_id()];
    PTE *pte = pte_checker(addr, current_running->pgdir);
    uintptr_t pa = ((*pte) >> 10) << 12;
    int i;
    for(i = 0; i < NUM_MAX_SHM; i++){
        if(shm_page_table[i].pa != pa){
            break;
        }
    }
    if(i != NUM_MAX_SHM){
        shm_page_table[i].num--;
    }
    else{
        return 0;
    }
    
    *pte = 0;
    local_flush_tlb_all();
    if(shm_page_table[i].num == 0){
        shm_page_table[i].valid = 0;
    }
}


// * 从虚拟地址得到表项, 如果有一层valid失误, 则返回0; 否则返回合理的pte
// * 额外增加SD卡检查功能, 当最后一项pte的valid为0时, 再检测一下是否是在SD卡内
PTE* pte_checker(uintptr_t va, uintptr_t pgdir)
{
    uint64_t vpn2, vpn1, vpn0;
    PTE * first_pgdir, *second_pgdir, *third_pgdir, *pte;
    PTE cur_pte;

    vpn2 = GetVPN2(va);
    first_pgdir = (PTE *)pgdir;
    cur_pte = first_pgdir[vpn2];
    if(PageValid(cur_pte)){
        second_pgdir = pa2kva(get_pa(cur_pte));
    }else{ 
        return 0;
    }

    vpn1 = GetVPN1(va);
    cur_pte = second_pgdir[vpn1];
    if(PageValid(cur_pte)){
        third_pgdir = pa2kva(get_pa(cur_pte));
    }else{
        return 0;
    }

    vpn0 = GetVPN0(va);
    cur_pte = third_pgdir[vpn0];
    if(PageValid(cur_pte)){
        return &third_pgdir[vpn0];
    }else{ 
        if(inSD(cur_pte)) return &third_pgdir[vpn0];
        else return 0; // 说明既不在SD卡里, 也没有映射过
    }
}

uint32_t get_sector()
{
    static uint32_t sector = 2048;
    uint32_t ret = sector;
    sector += 8;
    return ret;
}

PTE* get_pte(page_info_t* page, pcb_t* pcb_node)
{ // ! 通过虚拟用户地址, 得到表项, 为换入SD卡做准备
    uint64_t vpn2, vpn1, vpn0;
    PTE *pgdir, cur_pte;
    uint64_t va = page->uva;

    vpn2 = GetVPN2(va);
    pgdir = (PTE *)(pcb_node->pgdir);
    cur_pte = pgdir[vpn2];
    // 检查是否合法
    if(PageValid(cur_pte)){// 是则可以得到下级页表位置
        pgdir = pa2kva(get_pa(cur_pte));
    }else{
        assert(0);
    }

    vpn1 = GetVPN1(va);
    cur_pte = pgdir[vpn1];
    if(PageValid(cur_pte)){
        pgdir = pa2kva(get_pa(cur_pte));
    }else{
        assert(0);
    }

    vpn0 = GetVPN0(va);
    cur_pte = pgdir[vpn0];
    if(PageValid(cur_pte)){
        return &pgdir[vpn0];
    }else{
        assert(0);
    }
}

uint64_t swap_to_sd(page_info_t* to_swap_page, pcb_t* pcb_node)
{
    uint32_t sector_id = get_sector();
    uint64_t kva = to_swap_page->kva;
    PTE* pte = get_pte(to_swap_page, pcb_node);
    // 置位
    set_attribute(pte, _PAGE_INSD);
    clean_attribute(pte, _PAGE_PRESENT);    
    // 写入
    sd_write(get_pa(*pte), 8, sector_id);
    clear_pgdir(pa2kva(get_pa(*pte)));
    // 修改
    set_pfn(pte, sector_id);
    to_swap_page->pte = pte;
    to_swap_page->status = NOT_IN_MEM;
    to_swap_page->kva = 0;
    return kva;
}

uint64_t do_swap(pcb_t* pcb_node)
{// * 分配时就会触发
    page_info_t* to_swap_page = get_swap_page(&pcb_node->pagetable.pagehead); 
    uint64_t kva = swap_to_sd(to_swap_page, pcb_node);
    return kva;
}

void swap_to_mem(PTE* pte, uint64_t va, uint64_t kva)
{// * 只有缺页时会触发
    uint32_t sector_id = get_pfn(*pte);
    pcb_t* current_running = core_running[get_current_cpu_id()];
    list_head *pagehead = &current_running->pagetable.pagehead;
    list_node_t *page_list = pagehead->next;
    page_info_t* tmp;
    while(page_list != pagehead){
        tmp = list_entry(page_list, page_info_t, list);
        if(tmp->pte == pte){
            break;
        }
        page_list = page_list->next;
    }
    assert(tmp->status == NOT_IN_MEM);
    tmp->status = SWAPABLE;
    tmp->uva = va;
    assert(tmp->kva == 0);
    tmp->kva = kva;
    sd_read(kva2pa(tmp->kva), 8, sector_id);
    clean_attribute(pte, _PAGE_INSD);
    set_attribute(pte, _PAGE_PRESENT); 
    set_pfn(pte, kva2pa(tmp->kva) >> NORMAL_PAGE_SHIFT);

    return;
}

page_info_t * get_swap_page(list_head *head)
{
    page_info_t* swap_page;
    list_node_t* page_list = head->next;
    while (page_list != head)
    {
        swap_page = list_entry(page_list, page_info_t, list);
        if(swap_page->status == SWAPABLE){
            return swap_page;
        }else{
            page_list = page_list->next;
        }
    }
    assert(0);
}

uint64_t do_snapshot(uintptr_t va)
{
    // static num = 2;
    pcb_t* current_running = core_running[get_current_cpu_id()];
    PTE* pte = pte_checker(va, current_running->pgdir);
    // PTE* new_pte;
    list_head *pagehead = &current_running->pagetable.pagehead;
    list_node_t *page_list = pagehead->next;
    page_info_t* tmp;
    uintptr_t new_va, kva = pa2kva(get_pa(*pte));
    new_va = va;
    while(page_list != pagehead){
        tmp = list_entry(page_list, page_info_t, list);
        if(tmp->kva == kva){
            break;
        }
        page_list = page_list->next;
    }

    for(int i=0; ; ++i)
    {// 线性探查到一个有效的new_va;
        new_va += PAGE_SIZE;
        // new_pte = pte_checker(new_va, current_running->pgdir);
        if(pte_checker(new_va, current_running->pgdir) == 0) break;// 找到了
    }

    if(tmp->status != COPY_ON_WRITE){
        // 初始化
        clean_attribute(pte, _PAGE_WRITE);
        tmp->status = COPY_ON_WRITE;
        tmp->read_member = 1;
        tmp->read_member++;
        // ++num;
        build_page_helper(new_va, current_running, COPY_ON_WRITE, kva2pa(tmp->kva));
        pte = pte_checker(new_va, current_running->pgdir);
        clean_attribute(pte, _PAGE_WRITE);// 清理write位
    }else{// 则是另一份映射
        tmp->read_member++;
        // ++num;
        // new_va = va + (num - 1)*PAGE_SIZE;
        build_page_helper(new_va, current_running, COPY_ON_WRITE, kva2pa(tmp->kva));
        pte = pte_checker(new_va, current_running->pgdir);
        clean_attribute(pte, _PAGE_WRITE);// 清理write位
    }
    

    local_flush_tlb_all();
    return new_va;
}

uint64_t do_getpa(uintptr_t va)
{
    pcb_t* current_running = core_running[get_current_cpu_id()];
    PTE* pte = pte_checker(va, current_running->pgdir);
    uintptr_t pa = get_pa(*pte);
    pa |= (va & 0xfff);
    return pa;
}