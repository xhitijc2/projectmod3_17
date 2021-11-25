#include "threads/interrupt.h"
#include "threads/thread.h"
#include "filesys/fsaccess.h"
#include "userprog/process.h"
#include "syscall.h"
#include "devices/shutdown.h"
#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>



static void syscall_handler (struct intr_frame *);
static int wait (pid_t pid);
static bool create (const char *fe, unsigned init_size);
static int read (int fno, void *buff, unsigned len, void *fesp);
static int write (int fno, const void *buff, unsigned len);
static bool remove (const char *fe);
static void seek (int fno, unsigned pos);
static bool chdir (const char *direc);
static void halt (void);
static void exit (int status);
static unsigned tell (int fno);
static pid_t exec (const char *fe);
static bool mkdir (const char *direc);
static bool readdir (int fno, char *name);
static int open (const char *fe);
static void close (int fno);
static int filesize (int fno);
static bool isdir (int fno);
static int mmap (int fno, void *pg);
static void munmap (int m_id);
static int inumber (int fno);

#define CHECK_PTR(esp, wants_to_write) \
{\
  if (!is_valid_address_of_thread (thread_current (), esp, wants_to_write, 0))\
    exit (-1);\
}
#define GET_PARAM(esp,type)\
({\
    CHECK_PTR(esp, false);\
    type ret = *(type*)esp;\
    esp += sizeof(type*);\
    ret;\
})

#define CHECK_PTR_RANGE(start, end, wants_to_write, esp) \
{\
  if (!is_valid_address_range_of_thread (thread_current (), start, end, wants_to_write, esp))\
    exit(-1);\
}

void syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  fsaccess_init();
}

static void syscall_handler (struct intr_frame *frm) 
{
  CHECK_PTR(frm->esp, false);
  
  void *fesp = frm->esp;
  int syscall_id = *(int *)fesp;
  fesp += sizeof(int);

  int fno, m_id, status;
  pid_t pid;
  void *buff;
  const void *buff_const;
  const char *fe, *direc;
  char *name;
  unsigned size, pos, init_size;
  switch (syscall_id)
  {
    case SYS_HALT:
      halt ();
    break;
    case SYS_EXIT:
      status = GET_PARAM(fesp, int);

      exit (status);
    break;
    case SYS_EXEC:
      fe = GET_PARAM(fesp, char *);

      frm->eax = exec (fe);
    break;
    case SYS_WAIT:
      pid = GET_PARAM(fesp, pid_t);

      frm->eax = wait (pid);
    break;
    case SYS_CREATE:
      fe = GET_PARAM(fesp, char *);
      init_size = GET_PARAM(fesp, unsigned);

      frm->eax = create (fe, init_size);
    break;
    case SYS_REMOVE:
      fe = GET_PARAM(fesp, char *);

      frm->eax = remove (fe);
    break;
    case SYS_OPEN:
      fe = GET_PARAM(fesp, char *);

      frm->eax = open (fe);
    break;
    case SYS_FILESIZE:
      fno = GET_PARAM(fesp, int);

      frm->eax = filesize (fno);
    break;
    case SYS_READ:
      fno = GET_PARAM(fesp, int);
      buff = GET_PARAM(fesp, void *);
      size = GET_PARAM(fesp, unsigned);

      frm->eax = read (fno, buff, size, frm->esp); 
    break;
    case SYS_WRITE:
      fno = GET_PARAM(fesp, int);
      buff_const = GET_PARAM(fesp, void *);
      size = GET_PARAM(fesp, unsigned);

      frm->eax = write (fno, buff_const, size);
    break;
    case SYS_SEEK:
      fno = GET_PARAM(fesp, int);
      pos = GET_PARAM(fesp, unsigned);

      seek (fno, pos);
    break;
    case SYS_TELL:
      fno = GET_PARAM(fesp, int);

      frm->eax = tell (fno);
    break;
    case SYS_CLOSE:
      fno = GET_PARAM(fesp, int);

      close (fno);
    break;

    // Assignment 3 and 4
    case SYS_MMAP:
      fno = GET_PARAM(fesp, int);
      buff = GET_PARAM(fesp, void *);

      frm->eax = mmap (fno, buff);
    break;
    case SYS_MUNMAP:
      m_id = GET_PARAM(fesp, int);

      munmap(m_id);
    break;
    case SYS_CHDIR:
      direc = GET_PARAM(fesp, char *);

      frm->eax = chdir (direc);
    break;
    case SYS_MKDIR:
      direc = GET_PARAM(fesp, char *);

      frm->eax = mkdir (direc);
    break;
    case SYS_READDIR:
      fno = GET_PARAM(fesp, int);
      name = GET_PARAM(fesp, char *);

      frm->eax = readdir (fno, name);
    break;
    case SYS_ISDIR:
      fno = GET_PARAM(fesp, int);

      frm->eax = isdir (fno);
    break;
    case SYS_INUMBER:
      fno = GET_PARAM(fesp, int);

      frm->eax = inumber (fno);
    break;
  }
}
static void exit (int status)
{
  thread_exit_with_status (status);
}
static void halt ()
{
  shutdown_power_off ();
}



static int wait (pid_t p_id)
{
  return process_wait (p_id);
}
static pid_t exec (const char *fe)
{
  CHECK_PTR(fe, false);

  struct thread *curren = thread_current ();
  curren->child_born_status = 0;
  tid_t t_id = process_execute (fe);

  if(t_id != TID_ERROR)
    sema_down (&curren->child_sema);
  if (curren->child_born_status == -1)
    return -1;
  else
    return t_id;
}


static bool create (const char *fe, unsigned init_size)
{
  CHECK_PTR(fe, false);
  return create_file(fe, init_size);
}

static bool remove (const char *fe)
{
  CHECK_PTR(fe, false);
  return remove_file_or_dir(fe);
}

static int open (const char *fe)
{
  CHECK_PTR(fe, false);
  return open_file_or_dir(fe);
}

static int filesize (int fno)
{
  return filelength_open_file (fno);
}
static void seek (int fno, unsigned pos)
{
  seek_open_file (fno, pos);
}
static int read (int fno, void *buff, unsigned len, void *fesp)
{
  CHECK_PTR_RANGE(buff, buff + len, true, fesp);
  return read_open_file(fno, buff, len);
}

static int write (int fno, const void *buff, unsigned len)
{
  void *b = (void *)buff;
  CHECK_PTR_RANGE(b, b + len, false, 0);
  return write_open_file (fno, b, len);
}





static int inumber (int fno)
{
  return fd_inode_number (fno);
}

static bool mkdir (const char *direc)
{
  CHECK_PTR(direc, false);

  return create_directory (direc);
}
static void close (int fno)
{
  close_open_file_or_dir (fno);
}
static unsigned tell (int fno)
{
  return tell_open_file (fno);
}
static bool isdir (int fno)
{
  return is_directory (fno);
}
static int mmap (int fno, void *pg)
{
  return memory_map_file (fno, pg);
}

static void munmap (int m_id)
{
  memory_unmap_file (m_id);
}

static bool chdir (const char *direc)
{
  CHECK_PTR(direc, false);

  return change_directory (direc);
}



static bool readdir (int fno, char *n)
{
  CHECK_PTR(n, true);

  return read_directory (fno, n);
}

