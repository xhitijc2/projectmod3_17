#include "devices/block.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "filesys/fsaccess.h"
#include <stddef.h>
#include <inttypes.h>
#include "swap.h"
#include <bitmap.h>
#include <stdbool.h>



struct block  *swap_device;
static struct bitmap *swap_bm;
struct lock swap_lock;



void swap_in (size_t slot, void* pg)
{
  lock_acquire (&swap_lock);
  size_t swap_addr_base = slot * SECTORS_PER_PAGE;
  for (size_t iter = 0; iter < SECTORS_PER_PAGE; iter++)
    {
      block_sector_t from = swap_addr_base + iter;
      void* tooo = pg + iter * BLOCK_SECTOR_SIZE;
      lock_fs ();
      block_read (swap_device, from, tooo);
      unlock_fs ();
    }

  swap_free (slot);
  lock_release (&swap_lock);
}

void swap_init ()
{
  lock_init(&swap_lock);
  swap_device = block_get_role (BLOCK_SWAP);
  ASSERT (swap_device != NULL);
  size_t bms = block_size(swap_device) / SECTORS_PER_PAGE;
  swap_bm = bitmap_create(bms);
  ASSERT (swap_bm != NULL);

  bitmap_set_all(swap_bm, true);
} 

void swap_free(size_t st)
{
  bitmap_flip (swap_bm, st);
}
  

size_t swap_out (const void* pg)
{
  lock_acquire (&swap_lock);
  size_t s = bitmap_scan_and_flip (swap_bm, 0, 1, true);

  if (s != BITMAP_ERROR)
    {
      size_t swap_addr = s * SECTORS_PER_PAGE;
      lock_fs ();
      for (size_t iter = 0; iter < SECTORS_PER_PAGE; iter++)
        {
          const void* from = pg + iter * BLOCK_SECTOR_SIZE;
          block_sector_t tooo = swap_addr + iter;
          block_write (swap_device, tooo, from);
        }
      unlock_fs ();
      lock_release (&swap_lock);
      return s;  
    }
  else
    {
      lock_release (&swap_lock);
      return SWAP_ERROR;
    }
}

