#ifndef __KERN_MM_BEST_FIT_PMM_H__
#define __KERN_MM_BEST_FIT_PMM_H__

#include <pmm.h>

/* free_area_t is defined in memlayout.h (included via pmm.h). */
extern free_area_t free_area;

/* The pmm code accesses the free-list head and the free-page counter as
 * plain global names; here we map them onto the single free_area instance. */
#define free_list (free_area.free_list)
#define nr_free   (free_area.nr_free)

/* default_pmm_manager (best-fit) is implemented in default_pmm.c */
extern const struct pmm_manager default_pmm_manager;

#endif /* !__KERN_MM_BEST_FIT_PMM_H__ */
