#include <asm/biosdef.h>
#include <csr.h>

// os size location (os_size could be sectors [p1-task3] or bytes [p1-task4])
.equ os_size_loc, 0x502001fc

// kernel address (move kernel to here, and jmp here to start kernel)
.equ kernel, 0x50202000

// BIOS function entry (jump here to use BIOS APIs)
.equ bios_func_entry, 0x50150000

.text
.global main

main:
	// fence on all memory and I/O
	fence

	bnez a0, secondary

	// TODO: [p1-task1] call BIOS to print string "It's bootblock!"
	li a0,	BIOS_PUTSTR
	la a1,	msg
	li a2,	0
	li a3,	0
	li a4,	0
	li a5,	0
	call bios_func_entry

// 初始化
	la 		s1, 	kernel	// mem_addr
	la 		s2,		os_size_loc
	lh		s2,		(s2)	// s2存储block数
	li 		s3, 	1		// 每次开始的block_id
	li 		s4,		64		// 每次递增的sector
	li 		s5, 	32768	// 每次递增的地址
	ble 	s2,		s4,		remain // 若block数< 64, 则直接进入remain处理逻辑

sd_read:
	// TODO: [p1-task2] call BIOS to read kernel in SD card
	li 		a0, 	BIOS_SDREAD		//调用名
	add 	a1, 	s1, zero
	add 	s1, 	s1, s5 		// 递增地址
	add 	a2,		s4, zero		
	sub 	s2, 	s2, s4 			// 每次放入64, 并减去64
	add 	a3, 	s3, zero
	add		s3, 	s3, s4 			// 确定sd卡sector开头, 并加上64作为下一次开始
	li		a4,		0
	li		a5,		0
	call bios_func_entry			// 在call之前, s1,s2,s3都已经得到更新
	ble 	s2,		s4,		remain	// 此时, s2 < 64则进入remain

remain:
	li 		a0, 	BIOS_SDREAD		// 调用名, 每次额外写入
	add 	a1,     s1, zero 		// 将调整好的地址写入a1
	add 	a2, 	s2, zero		// 将剩下不到64个的sector写入a2
	add		a3,		s3, zero		// 将剩下的sector的开头写入a3
	li		a4,		0
	li		a5,		0
	call bios_func_entry

	// TODO: [p1-task2] jump to kernel to start UCAS-OS
	la	a1,	kernel
	jr 		a1

secondary:
	/* TODO [P3-task3]: 
	 * 1. Mask all interrupts
	 * 2. let stvec pointer to kernel_main
	 * 3. enable software interrupt for ipi
	 */
	
	csrw CSR_SIE, zero
	csrw CSR_SIP, zero

	lui  t0, %hi(kernel)
	addi t0, t0, %lo(kernel)
	
	csrw CSR_STVEC, t0

	li   t0, SIE_SSIE
  	csrw CSR_SIE, t0

	li   t0, SR_SIE  
  	csrs CSR_SSTATUS, t0

wait_for_wakeup:
	wfi
	j wait_for_wakeup


// while(1) --> stop here
stop:
	j stop
	nop

.data

msg: .string "It's a bootloader...\n\r"