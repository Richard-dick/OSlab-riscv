#include <asm.h>
#include <csr.h>
#include <asm/regs.h>

.macro SAVE_CONTEXT
  /* 切换栈 user-->kernel_context */
  /* 保留32个寄存器 */
  sd      sp, PCB_USER_SP(tp)
  ld      sp, PCB_KERNEL_SP(tp)
  addi    sp, sp, SWITCH_TO_SIZE

  sd      zero, OFFSET_REG_ZERO(sp)
  sd      ra, OFFSET_REG_RA(sp)
  sd      gp, OFFSET_REG_GP(sp)
  sd      tp, OFFSET_REG_TP(sp)
  sd      t0, OFFSET_REG_T0(sp)
  sd      t1, OFFSET_REG_T1(sp)
  sd      t2, OFFSET_REG_T2(sp)
  sd      s0, OFFSET_REG_S0(sp)
  sd      s1, OFFSET_REG_S1(sp)
  sd      a0, OFFSET_REG_A0(sp)
  sd      a1, OFFSET_REG_A1(sp)
  sd      a2, OFFSET_REG_A2(sp)
  sd      a3, OFFSET_REG_A3(sp)
  sd      a4, OFFSET_REG_A4(sp)
  sd      a5, OFFSET_REG_A5(sp)
  sd      a6, OFFSET_REG_A6(sp)
  sd      a7, OFFSET_REG_A7(sp)
  sd      s2, OFFSET_REG_S2(sp)
  sd      s3, OFFSET_REG_S3(sp)
  sd      s4, OFFSET_REG_S4(sp)
  sd      s5, OFFSET_REG_S5(sp)
  sd      s6, OFFSET_REG_S6(sp)
  sd      s7, OFFSET_REG_S7(sp)
  sd      s8, OFFSET_REG_S8(sp)
  sd      s9, OFFSET_REG_S9(sp)
  sd      s10, OFFSET_REG_S10(sp)
  sd      s11, OFFSET_REG_S11(sp)
  sd      t3, OFFSET_REG_T3(sp)
  sd      t4, OFFSET_REG_T4(sp)
  sd      t5, OFFSET_REG_T5(sp)
  sd      t6, OFFSET_REG_T6(sp)
  
  ld t1, PCB_USER_SP(tp)
  sd t1, OFFSET_REG_SP(sp)
  /*
   * Disable user-mode memory access as it should only be set in the
   * actual user copy routines.
   *
   * Disable the FPU to detect illegal usage of floating point in kernel
   * space.
   */

  /* save sstatus, sepc, stval and scause on kernel stack */
  csrr    t0, CSR_SSTATUS
  csrr    t1, CSR_SEPC
  csrr    t2, CSR_STVAL
  csrr    t3, CSR_SCAUSE

  sd      t0, OFFSET_REG_SSTATUS(sp)
  sd      t1, OFFSET_REG_SEPC(sp)
  sd      t2, OFFSET_REG_SBADADDR(sp)
  sd      t3, OFFSET_REG_SCAUSE(sp)

  li t0, SR_SUM | SR_FS
  csrw CSR_SSTATUS, t0

  addi sp, sp, -(SWITCH_TO_SIZE)

.endm

.macro RESTORE_CONTEXT
  /*切换栈，running_stack(abolished)--> kernel_context*/
  ld   sp, PCB_KERNEL_SP(tp)
  addi sp, sp, SWITCH_TO_SIZE
  
  ld t0, OFFSET_REG_SEPC(sp)
  csrw CSR_SEPC, t0
  
  ld t1, OFFSET_REG_SSTATUS(sp)
  csrw CSR_SSTATUS, t1
  
  ld ra, OFFSET_REG_RA(sp)
  ld gp, OFFSET_REG_GP(sp)
  ld tp, OFFSET_REG_TP(sp)
  ld t0, OFFSET_REG_T0(sp)
  ld t1, OFFSET_REG_T1(sp)
  ld t2, OFFSET_REG_T2(sp)
  ld s0, OFFSET_REG_S0(sp)
  ld s1, OFFSET_REG_S1(sp)
  ld a0, OFFSET_REG_A0(sp)
  ld a1, OFFSET_REG_A1(sp)
  ld a2, OFFSET_REG_A2(sp)
  ld a3, OFFSET_REG_A3(sp)
  ld a4, OFFSET_REG_A4(sp)
  ld a5, OFFSET_REG_A5(sp)
  ld a6, OFFSET_REG_A6(sp)
  ld a7, OFFSET_REG_A7(sp)
  ld s2, OFFSET_REG_S2(sp)
  ld s3, OFFSET_REG_S3(sp)
  ld s4, OFFSET_REG_S4(sp)
  ld s5, OFFSET_REG_S5(sp)
  ld s6, OFFSET_REG_S6(sp)
  ld s7, OFFSET_REG_S7(sp)
  ld s8, OFFSET_REG_S8(sp)
  ld s9, OFFSET_REG_S9(sp)
  ld s10, OFFSET_REG_S10(sp)
  ld s11, OFFSET_REG_S11(sp)
  ld t3, OFFSET_REG_T3(sp)
  ld t4, OFFSET_REG_T4(sp)
  ld t5, OFFSET_REG_T5(sp)
  ld t6, OFFSET_REG_T6(sp)

  ld sp, PCB_USER_SP(tp)
.endm

ENTRY(enable_preempt)
  not t0, x0
  csrs CSR_SIE, t0
  jr ra
ENDPROC(enable_preempt)

ENTRY(disable_preempt)
  csrw CSR_SIE, zero
  jr ra
ENDPROC(disable_preempt)

ENTRY(enable_interrupt)
  li t0, SR_SIE
  csrs CSR_SSTATUS, t0
  jr ra
ENDPROC(enable_interrupt)

ENTRY(disable_interrupt)
  li t0, SR_SIE
  csrc CSR_SSTATUS, t0
  jr ra
ENDPROC(disable_interrupt)

// NOTE: the address of previous pcb in a0
// NOTE: the address of next pcb in a1
ENTRY(switch_to)
  ld t0,  PCB_KERNEL_SP(a0)
  sd sp,  SWITCH_TO_SP(t0)
  sd ra,  SWITCH_TO_RA(t0)
  sd s0,  SWITCH_TO_S0(t0)
  sd s1,  SWITCH_TO_S1(t0)
  sd s2,  SWITCH_TO_S2(t0)
  sd s3,  SWITCH_TO_S3(t0)
  sd s4,  SWITCH_TO_S4(t0)
  sd s5,  SWITCH_TO_S5(t0)
  sd s6,  SWITCH_TO_S6(t0)
  sd s7,  SWITCH_TO_S7(t0)
  sd s8,  SWITCH_TO_S8(t0)
  sd s9,  SWITCH_TO_S9(t0)
  sd s10, SWITCH_TO_S10(t0)
  sd s11, SWITCH_TO_S11(t0)

  /*接下来进入a1，nextpcb，调整tp*/
  add tp, a1, zero

  ld t0,  PCB_KERNEL_SP(tp)
  ld sp,  SWITCH_TO_SP(t0)
  ld ra,  SWITCH_TO_RA(t0)
  ld s0,  SWITCH_TO_S0(t0)
  ld s1,  SWITCH_TO_S1(t0)
  ld s2,  SWITCH_TO_S2(t0)
  ld s3,  SWITCH_TO_S3(t0)
  ld s4,  SWITCH_TO_S4(t0)
  ld s5,  SWITCH_TO_S5(t0)
  ld s6,  SWITCH_TO_S6(t0)
  ld s7,  SWITCH_TO_S7(t0)
  ld s8,  SWITCH_TO_S8(t0)
  ld s9,  SWITCH_TO_S9(t0)
  ld s10, SWITCH_TO_S10(t0)
  ld s11, SWITCH_TO_S11(t0)
  
  /* 接下来转成用户栈进行操作，ra内本来就是ret——exception */
  jr ra
ENDPROC(switch_to)

ENTRY(ret_from_exception)
  /* RESTORE里处理了栈的问题，全部都回来了 */
  call unlock_kernel
  RESTORE_CONTEXT
  sret
ENDPROC(ret_from_exception)

ENTRY(exception_handler_entry)

  SAVE_CONTEXT
  call lock_kernel
  
  ld   a0, PCB_KERNEL_SP(tp)
  addi a0, a0, SWITCH_TO_SIZE
  /*addi a0, a0, OFFSET_SIZE  传入上下文，而不是栈指针，tmd就是传入栈指针!! */
  csrr a1, CSR_STVAL
  csrr a2, CSR_SCAUSE
  call interrupt_helper

  la ra, ret_from_exception
  jr ra
ENDPROC(exception_handler_entry)
