/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 */

#include <asm.h>
#include <csr.h>

/*.section ".entry_function","ax"*/
ENTRY(_start)
  /* Mask all interrupts */
  csrw CSR_SIE, zero
  csrw CSR_SIP, zero

  /* 写入SUM位 */
  li t0, SR_SUM
  csrs CSR_SSTATUS, t0

  call get_current_cpu_id
  bnez a0, slave_core

master_core:
  la t0,__bss_start
  la t1,__BSS_END__
  
loop_clear_BSS:
  beq   t0, t1, setup_kernel_C_stack
  sw    zero,(t0)
  addi  t0, t0, 4
  j     loop_clear_BSS

setup_kernel_C_stack:
  la tp, pid0_pcb_master
  ld sp, pid0_stack_master
  jal   main

slave_core:
  la tp, pid0_pcb_slave
  ld sp, pid0_stack_slave
  jal   main

loop:
  /*wfi*/
  j loop

END(_start)
