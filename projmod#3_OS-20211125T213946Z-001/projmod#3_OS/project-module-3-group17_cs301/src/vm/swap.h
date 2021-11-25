#ifndef _SWAP_H
#define _SWAP_H 
#define SWAP_ERROR -1

void swap_init (void);
void swap_free(size_t slot);
void swap_in (size_t slot, void* page);
size_t swap_out (const void *page);

#define SECTORS_PER_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)

#endif
