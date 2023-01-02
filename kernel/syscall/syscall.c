#include <sys/syscall.h>

long (*syscall[NUM_SYSCALLS])();

void handle_syscall(regs_context_t *regs, uint64_t interrupt, uint64_t cause)
{
    /* TODO: [p2-task3] handle syscall exception */
    /**
     * HINT: call syscall function like syscall[fn](arg0, arg1, arg2),
     * and pay attention to the return value and sepc
     */

    regs->sepc = regs->sepc + 4; // 更新ecall的pc
    // x10是a0
    // uint64_t /*syscall_id,*/ arg0, arg1, arg2, arg3;
    // uint64_t res;
    // // syscall_id = regs->regs[17];
    // arg0 = ;
    // arg1 = ;
    // arg2 = ;
    // arg3 = ;
    regs->regs[10] = syscall[regs->regs[17]](regs->regs[10], regs->regs[11], regs->regs[12], regs->regs[13]);
}

