#include "frame.h" 
#include "page.h" 
#include "swap.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "filesys/file.h"
#include "filesys/fsaccess.h"

static struct hash frame_hash;
static struct lock frame_hash_lock;
static struct lock frame_fs_lock;



bool compare_frame (const struct hash_elem *element1, const struct hash_elem *element2, void *aux UNUSED)
{
  struct frame_entry *frm1 = hash_entry (element1, struct frame_entry, elem);
  struct frame_entry *frm2 = hash_entry (element2, struct frame_entry, elem);
  return frm1->page < frm2->page;
}
unsigned find_frame (const struct hash_elem *element, void *aux UNUSED)
{
  struct frame_entry *frm = hash_entry (element, struct frame_entry, elem);
  return hash_bytes (&frm->page, sizeof (frm->page));
}
void *vm_frame_alloc (enum palloc_flags pflag, void *thread_vaddr)
{
  void *pg = NULL;

  
  if (pflag & PAL_USER)
    pg = palloc_get_page (pflag);

  if (pg != NULL)
    {
      bool added = frame_hash_add (pg, pflag, pg_round_down(thread_vaddr));
      if (!added)
        PANIC ("Out of memory!");
    }
  else
    {
      struct frame_entry *fe = evict_and_get_frame();
      ASSERT (fe != NULL);
      fe->thread_vaddr = thread_vaddr;
      pg = fe->page;
    }

  return pg;
}




void vm_frame_free (void *pg)
{
  struct hash_elem *element;
  struct frame_entry find;

  find.page = pg;
  /* Remove from list */
  lock_acquire (&frame_hash_lock);
  element = hash_find (&frame_hash, &find.elem);
  hash_delete (&frame_hash, element);
  lock_release (&frame_hash_lock);

  free (hash_entry (element, struct frame_entry, elem));
  palloc_free_page (pg);
}

bool frame_hash_add (void *pg, enum palloc_flags pflag, void *thread_vaddr)
{
  struct frame_entry *frm = malloc (sizeof(struct frame_entry));

  if (frm == NULL)
    return false;

  frm->page = pg;
  frm->owner = thread_current();
  frm->thread_vaddr = thread_vaddr;
  lock_acquire (&frame_hash_lock);

  if (pflag & PAL_USER){
    hash_insert (&frame_hash, &frm->elem);
  }

  lock_release (&frame_hash_lock);

  return true;
}
void vm_frame_alloc_init ()
{
  hash_init (&frame_hash, find_frame, compare_frame, NULL);
  lock_init (&frame_hash_lock);
  lock_init (&frame_fs_lock);
}


/* Call only with lock acquired */
struct frame_entry * select_frame_to_evict()
{
  struct hash_iterator itr;
  struct frame_entry *fe = NULL;
  bool flag = false;

  while (!flag)
  {
    hash_first (&itr, &frame_hash);
    while (hash_next (&itr))
    {
      fe = hash_entry (hash_cur (&itr), struct frame_entry, elem);
      if (pagedir_is_accessed (fe->owner->pagedir, fe->thread_vaddr))
      {
        pagedir_set_accessed (fe->owner->pagedir, fe->thread_vaddr, false);
      }
      else
      {
        flag = true;
        break;
      }
    }
  }
  return fe;
}
struct frame_entry * evict_and_get_frame()
{
  struct thread *thr = thread_current ();

  lock_acquire (&frame_hash_lock);

  struct frame_entry *vict = select_frame_to_evict();
  if (vict == NULL)
    PANIC ("No frame to evict");

  if (!page_out_evicted_frame (vict))
    PANIC ("Can't page out evicted frame");

  vict->owner = thr;
  lock_release (&frame_hash_lock);

  return vict;
}
/* Only call with lock acquired */
bool page_out_evicted_frame (struct frame_entry *fe)
{
  struct pt_suppl_entry *pt = pt_suppl_get (&fe->owner->pt_suppl, fe->thread_vaddr);
  size_t swap_slot_id;

  if (pt == NULL) 
    {// Lazy loaded
      {//dirty -> put in swap memory
        swap_slot_id = swap_out (fe->page);
        if ((int)swap_slot_id == SWAP_ERROR){
          PANIC ("Cannot swap");
          return false;
        }
      }
      pt = malloc (sizeof (struct pt_suppl_entry));
      pt->vaddr = fe->thread_vaddr;
      pt->swap_slot = swap_slot_id;
      pt->file_info = NULL;
      SET_TYPE(pt->status, LAZY);
      SET_PRESENCE (pt->status, SWAPPED);
      hash_insert (&fe->owner->pt_suppl, &pt->elem);
      pagedir_clear_page (fe->owner->pagedir, fe->thread_vaddr);
    }
  else if (IS_MMF (pt->status))
    {
      if(pagedir_is_dirty (fe->owner->pagedir, pt->vaddr))
      {
        lock_acquire (&frame_fs_lock);
        file_write_at (pt->file_info->file, pt->vaddr, 
          pt->file_info->read_bytes, pt->file_info->offset);
        lock_release (&frame_fs_lock);
      }
      SET_TYPE(pt->status, MMF);
      SET_PRESENCE (pt->status, UNLOADED);
      pagedir_clear_page (fe->owner->pagedir, pt->vaddr);
    }
  else
    {
      PANIC ("STATUS of page %p of %d is %d\n", pt->vaddr, fe->owner->tid, pt->status);
    }

  return true;
}
