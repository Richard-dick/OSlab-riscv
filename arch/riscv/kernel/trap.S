#include <asm.h>
#include <csr.h>

ENTRY(setup_exception)

  /* TODO: save exception_handler_entry into STVEC */
  la t0, exception_handler_entry
  csrw stvec, t0

  /* TODO: enable interrupts globally */
  li t0, (SR_SIE & ~SR_SPP)
  csrw sstatus, t0

  li t0, SIE_STIE | SIE_SSIE
  csrw sie, t0

  jr ra
ENDPROC(setup_exception)

ENTRY(setup_eie)
  csrr t0, sie
  ori  t0, t0, SIE_SEIE
  csrw sie, t0

  jr ra
ENDPROC(setup_eie)