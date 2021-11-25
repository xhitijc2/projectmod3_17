#include "userprog/pagedir.h"
#include "threads/malloc.h"
#include "filesys/file.h"
#include "filesys/fsaccess.h"
#include "frame.h"
#include "swap.h"
#include "page.h"
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "filesys/fsaccess.h"

int last_map = 0;

static struct pt_suppl_entry *
pt_suppl_setup_file_info (struct file *file, off_t offset, uint8_t *page_addr, 
uint32_t read_bytes, uint32_t zero_bytes, bool writable, enum pt_status status);



struct pt_suppl_entry * pt_suppl_get_entry_by_addr(const void *vaddr)
{ 
  struct thread *current = thread_current();
  void * page = pg_round_down (vaddr);
  return pt_suppl_get (&current->pt_suppl, page);
}
void pt_suppl_init (struct hash *tbl)
{
  hash_init (tbl, pt_suppl_hash, pt_suppl_less, NULL);
}

int 
pt_suppl_handle_mmap (struct file *fe, void *start_pg)
{
  struct thread *current = thread_current ();
  lock_fs ();
  off_t len = file_length (fe);
  unlock_fs ();
  int left;
  bool error = false;
  if (len == 0)
    return -1;

  last_map ++;

  void * page_address = start_pg; 
  for(int ofs = 0; ofs < len && !error; ofs += PGSIZE)
  {
    if (pt_suppl_get (&current->pt_suppl, page_address + ofs) || 
      pagedir_get_page (current->pagedir, page_address + ofs))
    {
      return -1;
    }

    left = len - ofs;
    if (left >= PGSIZE)
      left = PGSIZE;
    if (!pt_suppl_add_mmf(fe, ofs, page_address, left))
      error = true;

    page_address += PGSIZE;
  }

  return error ? -1 : last_map;
}


bool pt_suppl_handle_page_fault (void * vaddress, struct intr_frame *frm)
{
  ASSERT (vaddress != NULL && vaddress < PHYS_BASE);


  struct pt_suppl_entry *ent = pt_suppl_get_entry_by_addr (vaddress);
  if (ent != NULL)
      return pt_suppl_page_in (ent);
  else
      return pt_suppl_check_and_grow_stack (vaddress, frm->esp);
}

void pt_suppl_handle_unmap (int map_id)
{
  bool removed = false;
  struct pt_suppl_entry entry;
  struct pt_suppl_file_info mmfile;
  struct hash_elem *dele;
  struct pt_suppl_entry *delet;
  struct thread *current = thread_current();
  struct file *file_to_close = NULL;

  while (!removed)
    {
      mmfile.map_id = map_id;
      entry.vaddr = NULL; 
      entry.file_info = &mmfile;
      entry.status = MMF;
      dele = hash_delete (&current->pt_suppl, &entry.elem);

      if (dele != NULL)
      {
        delet = hash_entry (dele, struct pt_suppl_entry, elem);

        if (pagedir_is_dirty (current->pagedir, delet->vaddr))
          pt_suppl_flush_mmf(delet);

        file_to_close = delet->file_info->file;
        pt_suppl_destroy(delet);
      } 
      else
      {
        removed = true;
      }
    }
  if(file_to_close)
  {
    lock_fs ();
    file_close (file_to_close);
    unlock_fs ();
  }
}

void unmap_all()
{
  struct thread *current = thread_current();

  lock_acquire (&current->pt_suppl_lock);
  struct hash_elem *dele;
  struct pt_suppl_entry *delet;
  struct pt_suppl_entry entry;
  struct pt_suppl_file_info mmfile;
  entry.vaddr = NULL; 
  mmfile.map_id = -1; 
  mmfile.owner = current;
  entry.file_info = &mmfile;
  entry.status = MMF;
      
  bool finished = false;
  while (!finished)
    {
      dele = hash_find (&current->pt_suppl, &entry.elem);
      if(dele)
      {
        delet = hash_entry (dele, struct pt_suppl_entry, elem);
        ASSERT (delet->file_info != NULL); 
        pt_suppl_handle_unmap (delet->file_info->map_id);
      }
      else
        finished = true;
    }
  lock_release (&current->pt_suppl_lock);
}



bool pt_suppl_add (struct hash *tbl, struct pt_suppl_entry *entry)
{
  ASSERT (tbl != NULL && entry != NULL);
  hash_insert (tbl, &entry->elem);
  return true;
}

void pt_suppl_destroy(struct pt_suppl_entry *entry)
{
  ASSERT (entry != NULL);
  
  if(entry->file_info != NULL)
    free (entry->file_info);
  free (entry);
}

bool pt_suppl_add_mmf (struct file *fe, off_t ofs, uint8_t *page_addr, 
uint32_t read_bytes)
{
  struct pt_suppl_entry * entry = pt_suppl_setup_file_info (fe, ofs, page_addr, 
                                  read_bytes, PGSIZE - read_bytes, true, MMF_UNLOADED);

  bool passed = pt_suppl_add (&thread_current ()->pt_suppl, entry);
  ASSERT (passed);
  return entry != NULL;
}
struct pt_suppl_entry * pt_suppl_get (struct hash *tbl, void *pg)
{
  struct pt_suppl_entry entry;
  entry.vaddr = pg;
  struct hash_elem *element = hash_find (tbl, &entry.elem);

  if(element == NULL)
    return NULL;
  else
    return hash_entry (element, struct pt_suppl_entry, elem);
}



void pt_suppl_flush_mmf (struct pt_suppl_entry *entry)
{
  if(entry->status == MMF_PRESENT || 
     entry->status == MMF_SWAPPED)
    {
      struct pt_suppl_file_info *mmfile = entry->file_info;
      ASSERT (mmfile != NULL);

      file_seek (mmfile->file, mmfile->offset);
      file_write (mmfile->file, entry->vaddr, mmfile->read_bytes);
    }
}

static struct pt_suppl_entry *pt_suppl_setup_file_info (struct file *fe, off_t ofs, uint8_t *page_addr, uint32_t rb, uint32_t zb, bool writ, enum pt_status status)
{
  struct pt_suppl_entry * entry = calloc (1, sizeof (struct pt_suppl_entry));

  if(entry == NULL)
    return NULL;

  struct pt_suppl_file_info * inf = calloc (1, sizeof (struct pt_suppl_file_info));
  if(inf == NULL)
  {
    free(entry);
    return NULL;
  } 

  entry->vaddr = page_addr;
  entry->status = status;
  entry->file_info = inf;
  inf->file = fe;
  inf->owner = thread_current();
  inf->offset = ofs;
  inf->map_id = last_map;
  inf->read_bytes = rb;
  inf->zero_bytes = zb;
  inf->writable = writ;

  return entry;
}
bool pt_suppl_add_lazy (struct file *fe, off_t ofs, uint8_t *page_addr, uint32_t rb, uint32_t zb, bool writ)
{
  struct pt_suppl_entry * entry = pt_suppl_setup_file_info (fe, ofs, page_addr, 
                                  rb, zb, writ, LAZY_UNLOADED);

  struct thread *current = thread_current ();
  lock_acquire (&current->pt_suppl_lock);
  bool passed = pt_suppl_add (&current->pt_suppl, entry);
  lock_release (&current->pt_suppl_lock);
  ASSERT (passed);
  return entry != NULL;
}


bool pt_suppl_page_in (struct pt_suppl_entry *entry)
{
  uint8_t *frm = vm_frame_alloc (PAL_USER, entry->vaddr);
  if (frm == NULL) return false;

  if (IS_SWAPPED (entry->status))
    {
      bool is_writ = entry->file_info == NULL || entry->file_info->writable;
      bool pgdir = pagedir_set_page (thread_current ()->pagedir,entry->vaddr, frm, is_writ);

      if (!pgdir)
      {
        vm_frame_free (frm);
        return false;
      }
      swap_in (entry->swap_slot, entry->vaddr);

      if(GET_TYPE(entry->status) == LAZY)
        {
          ASSERT (hash_delete (&thread_current ()->pt_suppl, &entry->elem) != NULL);
          pt_suppl_destroy(entry);
        }
      else  
        SET_PRESENCE(entry->status, PRESENT);

      return true;
    }
  else if (IS_UNLOADED (entry->status))
    {
      struct pt_suppl_file_info *inf = entry->file_info;
      ASSERT (inf != NULL);

      bool read = false, pgdir = false;
      file_seek (inf->file, inf->offset);
      if(inf->read_bytes > 0)
      {
        read = file_read (inf->file, frm, inf->read_bytes);
        memset (frm + inf->read_bytes, 0, inf->zero_bytes);
      }
      else
      {
        read = true;
        memset (frm, 0, inf->zero_bytes);
      }
      if (read)
        pgdir = pagedir_set_page (thread_current ()->pagedir,entry->vaddr, frm, inf->writable);

      if(pgdir)
        {
          if(GET_TYPE(entry->status) == LAZY)
          {
            ASSERT (hash_delete (&thread_current ()->pt_suppl, &entry->elem) != NULL);
            pt_suppl_destroy(entry);
          }
          else  
            SET_PRESENCE(entry->status, PRESENT);

          return true;
        }
      else
        {
          vm_frame_free (frm);
          return false;
        }
    }
  else PANIC ("Trying to page-in already loaded page");
}
void pt_suppl_grow_stack (const void *front)
{
  void *pg = vm_frame_alloc(PAL_USER | PAL_ZERO, pg_round_down (front));
  if (pg != NULL)
  {
    bool passed = pagedir_set_page 
          (thread_current ()->pagedir, pg_round_down (front), pg, true);

    if(!passed)
      vm_frame_free (pg);
  }
}

bool pt_suppl_check_and_grow_stack (const void *vaddress, const void *esp)
{
  void * pg = pg_round_down (vaddress);
  bool is_stack_growth = vaddress >= esp-32;
  is_stack_growth &= PHYS_BASE - pg <= MAX_STACK;

  if(is_stack_growth)
    pt_suppl_grow_stack(vaddress);

  return is_stack_growth;
}




unsigned pt_suppl_hash (const struct hash_elem *helem, void *auxil)
{
  if(auxil != helem)
    return hash_bytes (&auxil, sizeof (void*));
  else
  {
    struct pt_suppl_entry *pentry = hash_entry (helem, struct pt_suppl_entry, elem);
    return hash_bytes (&pentry->vaddr, sizeof (void*));
  }
}

static void pt_suppl_free_entry (struct hash_elem *helem, void *aux UNUSED)
{
  struct pt_suppl_entry *entry;
  entry = hash_entry (helem, struct pt_suppl_entry, elem);
  if (entry->status == MMF_SWAPPED)
    {
      
      free (entry->file_info);
    }
  free (entry);
}

void pt_suppl_free (struct hash *tbl) 
{
  hash_destroy (tbl, pt_suppl_free_entry);
}
bool pt_suppl_less (const struct hash_elem *helem1, const struct hash_elem *helem2,void *aux UNUSED)
{
  struct pt_suppl_entry *e1,*e2;

  e1 = hash_entry (helem1, struct pt_suppl_entry, elem);
  e2 = hash_entry (helem2, struct pt_suppl_entry, elem);

  if(e1->vaddr == NULL || e2->vaddr == NULL)
  {
    ASSERT(e1->file_info != NULL || e2->file_info != NULL);
    
    if(GET_TYPE(e1->status) != GET_TYPE(e2->status))
      return true; 

    if(e1->file_info->map_id < 0 || e2->file_info->map_id < 0)
      return e1->file_info->owner < e2->file_info->owner; 
    else
      return e1->file_info->map_id < e2->file_info->map_id; 
  }
  return e1->vaddr < e2->vaddr;
}
