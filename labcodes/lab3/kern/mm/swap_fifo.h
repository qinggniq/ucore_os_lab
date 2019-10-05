#ifndef __KERN_MM_SWAP_FIFO_H__
#define __KERN_MM_SWAP_FIFO_H__

#include <swap.h>

#define IS_VISITED(pte) ((pte)&PTE_A)
#define IS_WRITED(pte) ((pte)&PTE_D)

extern struct swap_manager swap_manager_fifo;

#endif
