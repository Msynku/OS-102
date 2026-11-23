#ifndef __SBI_H__
#define __SBI_H__

#include <defs.h>

/* NOTE: This is a TEMPORARY local-verification shim.
 * The original lab code uses the LEGACY SBI v0.1 extension IDs (putchar=0x01,
 * set_timer=0x00, ...), which were REMOVED in OpenSBI >= 0.9 / 1.x. The grader's
 * environment uses an older OpenSBI with legacy SBI, so the ORIGINAL sbi.h
 * (backed up at libs/sbi.h.orig) must be restored for the final submission.
 * This shim maps the four needed calls to the SBI v0.2+ (aka "new") extensions
 * so the kernel can be verified on modern OpenSBI (v1.5.1 / SBI v2.0).
 */

/* SBI v0.2+ extension IDs (EIDs) */
#define SBI_EXT_BASE   0x10
#define SBI_EXT_TIME   0x54494D45   /* "TIME" */
#define SBI_EXT_DBCN   0x4442434E   /* "DBCN" - debug console */
#define SBI_EXT_SRST   0x53525354   /* "SRST" - system reset */

/* New SBI calling convention: a7=EID, a6=FID, a0..a5=args, ret in a0/a1 */
#define SBI_CALL_NEW(eid, fid, arg0, arg1, arg2) ({				\
	register uintptr_t a0 asm ("a0") = (uintptr_t)(arg0);		\
	register uintptr_t a1 asm ("a1") = (uintptr_t)(arg1);		\
	register uintptr_t a2 asm ("a2") = (uintptr_t)(arg2);		\
	register uintptr_t a6 asm ("a6") = (uintptr_t)(fid);		\
	register uintptr_t a7 asm ("a7") = (uintptr_t)(eid);		\
	asm volatile ("ecall"						\
		      : "+r" (a0)					\
		      : "r" (a1), "r" (a2), "r" (a6), "r" (a7)		\
		      : "memory");					\
	a0;								\
})

static inline void sbi_console_putchar(int ch)
{
	/* DBCN console_write_byte(byte): FID = 2 */
	SBI_CALL_NEW(SBI_EXT_DBCN, 2, (uintptr_t)((unsigned char)ch), 0, 0);
}

static inline int sbi_console_getchar(void)
{
	/* DBCN console_read(num_bytes, base_lo, base_hi): FID = 1 */
	unsigned char c = 0;
	SBI_CALL_NEW(SBI_EXT_DBCN, 1, 1, (uintptr_t)&c, 0);
	return c;
}

static inline void sbi_set_timer(uint64_t stime_value)
{
	/* TIME set_timer(stime_value): FID = 0 */
	SBI_CALL_NEW(SBI_EXT_TIME, 0, stime_value, 0, 0);
}

static inline void sbi_shutdown(void)
{
	/* SRST reset(reset_type=0 shutdown, reason=0): FID = 0 */
	SBI_CALL_NEW(SBI_EXT_SRST, 0, 0, 0, 0);
}

static inline void sbi_clear_ipi(void)
{
}

static inline void sbi_send_ipi(const unsigned long *hart_mask)
{
}

static inline void sbi_remote_fence_i(const unsigned long *hart_mask)
{
}

static inline void sbi_remote_sfence_vma(const unsigned long *hart_mask,
					 unsigned long start,
					 unsigned long size)
{
}

static inline void sbi_remote_sfence_vma_asid(const unsigned long *hart_mask,
					      unsigned long start,
					      unsigned long size,
					      unsigned long asid)
{
}

#endif /* !__SBI_H__ */
