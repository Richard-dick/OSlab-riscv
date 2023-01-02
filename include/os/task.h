#ifndef __INCLUDE_TASK_H__
#define __INCLUDE_TASK_H__

#include <type.h>

#define TASK_MEM_BASE    0xffffffc059000000ul
#define TASK_MAXNUM      16
#define TASK_SIZE        0x10000

#define TASK_NUM_LOC     0xffffffc0502001f7ul
// #define TASK_NUM_LOC     0x502000ff
#define TASK_BEGIN_BLOCK 0x0
#define TASK_END_BLOCK   0x2
#define TASK_BEGIN_OFF   0x4
#define TASK_END_OFF     0x8
#define TASK_MEMSZ       0xc
#define TASK_ENTRY       0x10
#define TASK_NAME        0x18
#define TASK_INFO_SIZE   (sizeof(task_info_t))

#define TASK_NAME_LEN    18

/* TODO: [p1-task4] implement your own task_info_t! */
typedef struct {
    uint16_t begin_block;
    uint16_t end_block;
    uint32_t begin_offset;
    uint32_t end_offset;
    uint32_t memsz;
    uint64_t entry_addr;
    char name[TASK_NAME_LEN];
} task_info_t;

extern task_info_t tasks[TASK_MAXNUM];

#endif