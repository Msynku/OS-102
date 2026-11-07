#include <assert.h>
#include <intr.h>
#include <riscv.h>
#include <sbi.h>
#include <stdio.h>
#include <../sync/sync.h>

extern uintptr_t test_lab3_extra_context(void);

void test_lab3_extra(void) {
    bool enabled;

    intr_disable();
    assert(test_lab3_extra_context() == 0);
    assert((read_csr(sstatus) & SSTATUS_SIE) == 0);

    /* No timer has been armed yet; test restoration with SIE enabled too. */
    intr_enable();
    assert(test_lab3_extra_context() == 0);
    assert((read_csr(sstatus) & SSTATUS_SIE) != 0);
    cprintf("[Extra] 16/32-bit breakpoint and illegal instruction, sp/ra/gp/tp/a0/SIE: PASS\n");

    local_intr_save(enabled);
    assert(enabled);
    assert((read_csr(sstatus) & SSTATUS_SIE) == 0);
    local_intr_restore(enabled);
    assert((read_csr(sstatus) & SSTATUS_SIE) != 0);

    intr_disable();
    local_intr_save(enabled);
    assert(!enabled);
    local_intr_restore(enabled);
    assert((read_csr(sstatus) & SSTATUS_SIE) == 0);
    cprintf("[Extra] local_intr_save/restore preserves enabled and disabled states: PASS\n");
    sbi_shutdown();
    panic("SBI shutdown unexpectedly returned");
}
