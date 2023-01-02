#ifndef __INCLUDE_LOADER_H__
#define __INCLUDE_LOADER_H__

#include <type.h>
#include <os/sched.h>

// #define LOAD_FROM_SD    0
// #define LOAD_GET_ADDR   1

uint64_t load_task_img(/*int taskid*/char *taskname, pcb_t*/*uintptr_t pgdir*/);

#endif