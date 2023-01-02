#include <asm.h>

.section ".entry_function","ax"
ENTRY(_start)
    /* setup C runtime environment for the user program */
    la t0,__bss_start
    la t1,__BSS_END__

loop_clear_BSS:
    beq   t0, t1, BSS_over
    sb    zero,(t0)
    addi  t0, t0, 1
    j     loop_clear_BSS

BSS_over: /* enter main */
    jal main


    /* exit when program return! */
    call sys_exit

// while(1) loop, unreachable here
loop:
    wfi
    j loop

END(_start)
