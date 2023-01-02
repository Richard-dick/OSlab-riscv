#include <asm.h>

.section ".entry_function","ax"
ENTRY(_start)

    /* TODO: [p1-task3] setup C runtime environment for the user program */
    la t0,__bss_start
    la t1,__BSS_END__
loop_clear_BSS:
    beq   t0, t1, BSS_over
    sb    zero,(t0)
    addi  t0, t0, 1
    j     loop_clear_BSS


    /* TODO: [p1-task3] enter main function */
BSS_over:
    jal main


    /* TODO: [p1-task3] finish task and return to the kernel */
    /* NOTE: You need to replace this with new mechanism in p3-task2! */

    /* TODO: [P3] call sys_exit */
    call sys_exit

// while(1) loop, unreachable here
loop:
    wfi
    j loop

END(_start)
