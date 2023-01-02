#include <os/task.h>
#include <os/string.h>
#include <os/kernel.h>
#include <os/mm.h>
#include <assert.h>

#define MIN(a,b) ((a>b)?b:a)

extern task_info_t tasks[TASK_MAXNUM];
static char buffer[1024]; // 两页大小, 开摆!!

// mode: 0 from sd to mem;
// 1: return the entry_addr
uint64_t load_task_img(char *taskname, pcb_t* load_pcb/*uintptr_t pgdir*/)
{
    int taskid = 0;
    uint64_t begin_addr, end_addr;
    uint64_t filesz, memsz;
    uint64_t entry_va, cur_kva;
    uint32_t begin_block, begin_offset, block_num;

    for(; taskid != 16; ++taskid)
    {
        if(strcmp(tasks[taskid].name, taskname) == 0) break;
    }
    //debug
    // bios_putchar(taskid + '0');
    // bios_putstr(tasks[taskid].name);
    // bios_putchar('\n');

    begin_block = tasks[taskid].begin_block;
    begin_offset = tasks[taskid].begin_offset;
    begin_addr = (begin_block << 9) + begin_offset;
    end_addr = (tasks[taskid].end_block << 9) + tasks[taskid].end_offset;
    filesz = end_addr - begin_addr;
    memsz = tasks[taskid].memsz;
    entry_va = tasks[taskid].entry_addr;

    for(uint64_t it = 0; it < memsz; it += NORMAL_PAGE_SIZE)
    {
        cur_kva = alloc_page_helper(entry_va + it, load_pcb, UNSWAPABLE);
        if(it < filesz){// 说明还需要搬一页
            int64_t left_byte = filesz - it;
            for(uint32_t block_it = 0; block_it != 8 && left_byte > 0; ++block_it, left_byte -= 512)
            {// 开始遍历sector
                bios_sdread(kva2pa((uintptr_t)buffer), 2, begin_block+block_it);
                memcpy(cur_kva+block_it*512 , buffer + begin_offset, MIN(512, left_byte));
                if(left_byte < 512){
                    memset((cur_kva+block_it*512+left_byte), 0, (NORMAL_PAGE_SIZE -block_it*512-left_byte));
                    break;
                }
            }
            begin_block += 8;
        }else{// 说明不需要在搬一页了
            clear_pgdir(cur_kva);
        }
    }

    return entry_va;
}