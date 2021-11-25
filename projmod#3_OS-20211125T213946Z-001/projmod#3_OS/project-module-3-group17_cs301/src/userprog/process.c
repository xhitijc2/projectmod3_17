#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/fsaccess.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "devices/timer.h"
#include "vm/frame.h"
#include "vm/page.h"

static thread_func start_process NO_RETURN;
static bool load(struct args_struct *file_name_args, void (**eip)(void), void **esp);
tid_t process_execute(const char *file_name)
{
  char *only_fn;
  char *fn_copy;
  char *savepointer;
  struct thread *currenthread = thread_current();
  struct thread *chld;
  tid_t threadID;
  struct args_struct *argument_names;

  /* Make a copy of file_name.
     Otherwise there's a race between the caller and load(). */
  argument_names = palloc_get_page(0);
  if (argument_names == NULL)
    return TID_ERROR;

  fn_copy = malloc(sizeof(char) * (strlen(file_name) + 1));
  if (fn_copy == NULL)
  {
    palloc_free_page(argument_names);
    return TID_ERROR;
  }

  strlcpy(fn_copy, file_name, strlen(file_name) + 1);

  /* All arguments. */
  strlcpy(argument_names->file_args, fn_copy, strlen(fn_copy) + 1);
  only_fn = strtok_r(fn_copy, " ", &savepointer);
  /* Only filename. */
  strlcpy(argument_names->file_name, only_fn, strlen(only_fn) + 1);

  /* Create a new thread to execute file_name. */
  threadID = thread_create(only_fn, PRI_DEFAULT,
                           start_process, argument_names);
  if (threadID == TID_ERROR)
  {
    palloc_free_page(argument_names);
  }
  else
  {
    chld = lookup_tid(threadID);
    list_push_back(&currenthread->children_list, &chld->children_elem);
  }
  free(fn_copy);

  return threadID;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process(void *filename_arguments)
{
  struct intr_frame intrFrame;
  bool result;
  struct thread *parentThread;
  struct thread *currthread = thread_current();

  pt_suppl_init(&currthread->pt_suppl);
  lock_init(&currthread->pt_suppl_lock);

  /* Initialize interrupt frame and load executable. */
  memset(&intrFrame, 0, sizeof intrFrame);
  intrFrame.gs = intrFrame.fs = intrFrame.es = intrFrame.ds = intrFrame.ss = SEL_UDSEG;
  intrFrame.cs = SEL_UCSEG;
  intrFrame.eflags = FLAG_IF | FLAG_MBS;
  result = load((struct args_struct *)filename_arguments, &intrFrame.eip, &intrFrame.esp);

  parentThread = lookup_tid(currthread->parent_tid);
  if (parentThread != NULL)
  {
    parentThread->child_born_status = (result ? 1 : -1);
    sema_up(&parentThread->child_sema);
  }

  if (!result)
  {
    thread_exit();
  }

  palloc_free_page(filename_arguments);

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit */
  asm volatile("movl %0, %%esp; jmp intr_exit"
               :
               : "g"(&intrFrame)
               : "memory");
  NOT_REACHED();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.
   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int process_wait(tid_t childThreadID)
{
  struct thread *currentThread = thread_current();
  struct thread *chld = NULL;
  struct list_elem *chhild_elemen;
  struct list *list_of_children = &currentThread->children_list;
  bool result = false;
  int exitstat;

  if (!list_empty(list_of_children))
  {
    for (chhild_elemen = list_front(list_of_children); chhild_elemen != list_end(list_of_children) && !result;
         chhild_elemen = list_next(chhild_elemen))
    {

      chld = list_entry(chhild_elemen, struct thread, children_elem);

      if (chld->tid == childThreadID)
        result = true;
    }
  }

  if (chld == NULL || chld->waited)
    return -1;
  chld->waited = true;

  sema_down(&chld->exit_sema);
  /* Save the exit status. */
  exitstat = chld->exit_status;
  printf("%s: exit(%d)\n", chld->name, exitstat);
  list_remove(&chld->children_elem);
  sema_up(&chld->exit_status_read_sema);

  return exitstat;
}

/* Free the current process's resources. */
void process_exit(void)
{
  struct thread *currentThread = thread_current();
  struct thread *chld;
  struct list *listOfChildren = &currentThread->children_list;
  struct list_elem *childelement;
  uint32_t *currpagedirectory;

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  currpagedirectory = currentThread->pagedir;
  if (currpagedirectory != NULL)
  {

    currentThread->pagedir = NULL;
    pagedir_activate(NULL);
    pagedir_destroy(currpagedirectory);
  }

  if (currentThread->run_file != NULL)
  {
    /* Automatically allows writes again. */
    file_close(currentThread->run_file);
  }

  pt_suppl_free(&currentThread->pt_suppl);

  /* Children don't need to wait on the semaphore after we exit. */
  if (!list_empty(listOfChildren))
  {
    for (childelement = list_front(listOfChildren); childelement != list_end(listOfChildren);
         childelement = list_next(childelement))
    {

      chld = list_entry(childelement, struct thread, children_elem);
      sema_up(&chld->exit_status_read_sema);
    }
  }

  sema_up(&currentThread->exit_sema);
  /* Wait that the parent process has actually read the exit status */
  sema_down(&currentThread->exit_status_read_sema);
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void process_activate(void)
{
  struct thread *t = thread_current();

  /* Activate thread's page tables. */
  pagedir_activate(t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32 /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32 /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32 /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16 /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
{
  unsigned char e_ident[16];
  Elf32_Half e_type;
  Elf32_Half e_machine;
  Elf32_Word e_version;
  Elf32_Addr e_entry;
  Elf32_Off e_phoff;
  Elf32_Off e_shoff;
  Elf32_Word e_flags;
  Elf32_Half e_ehsize;
  Elf32_Half e_phentsize;
  Elf32_Half e_phnum;
  Elf32_Half e_shentsize;
  Elf32_Half e_shnum;
  Elf32_Half e_shstrndx;
};

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
{
  Elf32_Word p_type;
  Elf32_Off p_offset;
  Elf32_Addr p_vaddr;
  Elf32_Addr p_paddr;
  Elf32_Word p_filesz;
  Elf32_Word p_memsz;
  Elf32_Word p_flags;
  Elf32_Word p_align;
};

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL 0           /* Ignore. */
#define PT_LOAD 1           /* Loadable segment. */
#define PT_DYNAMIC 2        /* Dynamic linking info. */
#define PT_INTERP 3         /* Name of dynamic loader. */
#define PT_NOTE 4           /* Auxiliary info. */
#define PT_SHLIB 5          /* Reserved. */
#define PT_PHDR 6           /* Program header table. */
#define PT_STACK 0x6474e551 /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1 /* Executable. */
#define PF_W 2 /* Writable. */
#define PF_R 4 /* Readable. */

static bool setup_stack(void **esp, char *file_name_args);
static bool validate_segment(const struct Elf32_Phdr *, struct file *);
static bool load_segment(struct file *file, off_t ofs, uint8_t *upage,
                         uint32_t read_bytes, uint32_t zero_bytes,
                         bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool load(struct args_struct *filenameargument, void (**eip)(void), void **esp)
{
  struct thread *currentThread = thread_current();
  struct Elf32_Ehdr EHDR;
  struct file *currentFile = NULL;
  off_t fileoffset;

  bool res = false;

  if (filenameargument == NULL)
    return res;

  /* Allocate and activate page directory. */
  currentThread->pagedir = pagedir_create();
  if (currentThread->pagedir == NULL)
    goto done;
  process_activate();

  /* Open executable currentFile. */
  lock_fs();
  currentFile = filesys_open(filenameargument->file_name);
  if (currentFile == NULL)
  {
    printf("load: %s: open failed\n", filenameargument->file_name);
    file_close(currentFile);
    unlock_fs();

    goto done;
  }
  else
  {
    currentThread->run_file = currentFile;
    file_deny_write(currentFile);
    unlock_fs();
  }

  /* Read and verify executable header. */
  if (file_read(currentFile, &EHDR, sizeof EHDR) != sizeof EHDR || memcmp(EHDR.e_ident, "\177ELF\1\1\1", 7) || EHDR.e_type != 2 || EHDR.e_machine != 3 || EHDR.e_version != 1 || EHDR.e_phentsize != sizeof(struct Elf32_Phdr) || EHDR.e_phnum > 1024)
  {
    printf("load: %s: error loading executable\n", filenameargument->file_name);
    goto done;
  }

  /* Read program headers. */
  fileoffset = EHDR.e_phoff;

  int tt = EHDR.e_phnum;
  while (tt--)
  {
    struct Elf32_Phdr phdr;

    if (fileoffset < 0 || fileoffset > file_length(currentFile))
      goto done;
    file_seek(currentFile, fileoffset);

    if (file_read(currentFile, &phdr, sizeof phdr) != sizeof phdr)
      goto done;
    fileoffset += sizeof phdr;
    switch (phdr.p_type)
    {
    case PT_NULL:
    case PT_NOTE:
    case PT_PHDR:
    case PT_STACK:
    default:
      /* Ignore this segment. */
      break;
    case PT_DYNAMIC:
    case PT_INTERP:
    case PT_SHLIB:
      goto done;
    case PT_LOAD:
      if (validate_segment(&phdr, currentFile))
      {
        bool writable = (phdr.p_flags & PF_W) != 0;
        uint32_t file_page = phdr.p_offset & ~PGMASK;
        uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
        uint32_t page_offset = phdr.p_vaddr & PGMASK;
        uint32_t read_bytes, zero_bytes;
        if (phdr.p_filesz > 0)
        {
          /* Normal segment.
             Read initial part from disk and zero the rest. */
          read_bytes = page_offset + phdr.p_filesz;
          zero_bytes = (ROUND_UP(page_offset + phdr.p_memsz, PGSIZE) - read_bytes);
        }
        else
        {
          /* Entirely zero.
             Don' read anything from disk. */
          read_bytes = 0;
          zero_bytes = ROUND_UP(page_offset + phdr.p_memsz, PGSIZE);
        }
        if (!load_segment(currentFile, file_page, (void *)mem_page,
                          read_bytes, zero_bytes, writable))
          goto done;
      }
      else
        goto done;
      break;
    }
  }

  /* Set up stack. */
  if (!setup_stack(esp, filenameargument->file_args))
    goto done;

  /* Start address. */
  *eip = (void (*)(void))EHDR.e_entry;

  res = true;

done:
  return res;
}

/* load() helpers. */

static bool install_page(void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment(const struct Elf32_Phdr *phdr, struct file *file)
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
    return false;

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off)file_length(file))
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz)
    return false;

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;

  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr((void *)phdr->p_vaddr))
    return false;
  if (!is_user_vaddr((void *)(phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:
        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.
        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.
   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment(struct file *file, off_t ofs, uint8_t *userPage,
             uint32_t bytesRead, uint32_t zero_bytes, bool isWritable)
{
  ASSERT(bytesRead + zero_bytes > 0);
  ASSERT((bytesRead + zero_bytes) % PGSIZE == 0);
  ASSERT(pg_ofs(userPage) == 0);
  ASSERT(ofs % PGSIZE == 0);

  while (bytesRead > 0 || zero_bytes > 0)
  {
    /* Calculate how to fill this page.
       We will read bytesreadfrompage bytes from FILE
       and zero the final pagezerobytes bytes. */
    size_t bytesreadfrompage = bytesRead < PGSIZE ? bytesRead : PGSIZE;
    size_t pagezerobytes = PGSIZE - bytesreadfrompage;

    bool success = pt_suppl_add_lazy(file, ofs, userPage, bytesreadfrompage,
                                     pagezerobytes, isWritable);
    if (!success)
      return false;

    /* Advance. */
    bytesRead -= bytesreadfrompage;
    zero_bytes -= pagezerobytes;
    ofs += bytesreadfrompage;
    userPage += PGSIZE;
  }
  return true;
}
// struct argument_addr
// {
//     struct list_elem list_elem;
//     uint32_t addr;
// };
// void push_argument_(void **esp, const char *args, struct list *list)
// {
//     int siz = strlen(args) + 1;
//     *esp -= siz;

//     struct argument_addr *addr = malloc(sizeof(struct argument_addr));
//     memcpy(*esp, args, siz);

//     addr->addr = *esp;
//     list_push_back(list, &addr->list_elem);
// }
// void push_arguments(void **esp, const char *args)
// {
//     struct list l;
//     list_init(&l);

//     *esp = PHYS_BASE;
//     uint32_t arg_num = 1;
//     const char *arg = thread_name();
//     push_argument_(esp, arg, &l);
//     char *token, *save_p;
//     for (token = strtok_r(args, " ", &save_p);
//          token != NULL;
//          token = strtok_r(NULL, " ", &save_p))
//     {
//         arg_num += 1;
//         push_argument_(esp, token, &l);
//     }
//     int ans = PHYS_BASE - *esp;
//     *esp = *esp - (4 - ans % 4) - 4;
//     *esp -= 4;
//     *(uint32_t *)*esp = (uint32_t)NULL;
//     while (!list_empty(&l))
//     {
//         struct argument_addr *addr =
//             list_entry(list_pop_back(&l), struct argument_addr, list_elem);
//         *esp -= 4;
//         *(uint32_t *)*esp = addr->addr;
//     }

//     *esp -= 4;
//     *(uint32_t *)*esp = (uint32_t *)(*esp + 4);

//     *esp -= 4;
//     *(uint32_t *)*esp = arg_num;

//     *esp -= 4;
//     *(uint32_t *)*esp = 0x0;
// }
/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack(void **esp, char *argumentfilenames)
{
  if (esp == NULL || argumentfilenames == NULL)
    return false;

  uint8_t *kernelPage;
  bool result = false;
  uint8_t *startt = ((uint8_t *)PHYS_BASE) - PGSIZE;
  kernelPage = vm_frame_alloc(PAL_USER | PAL_ZERO, startt);
  if (kernelPage != NULL)
  {
    result = install_page(startt, kernelPage, true);
    if (result)
      *esp = PHYS_BASE;
    else
    {
      vm_frame_free(kernelPage);
      return false;
    }
  }
  else
    return false;

  char *savepoitner;
  char *tkn;
  char *argumentoffilenamescopy;
  int argumentcount = 0, j = 0;

  argumentoffilenamescopy = malloc((strlen(argumentfilenames) + 1) * sizeof(char));

  if (argumentoffilenamescopy == NULL)
    return false;

  strlcpy(argumentoffilenamescopy, argumentfilenames, strlen(argumentfilenames) + 1);

  for ((tkn = strtok_r(argumentoffilenamescopy, " ", &savepoitner)); tkn != NULL && argumentcount < ARG_MAX;
       tkn = strtok_r(NULL, " ", &savepoitner))
  {
    argumentcount++;
  }

  char **argvector = malloc((argumentcount + 1) * sizeof(char *));

  if (argvector == NULL)
    return false;
  /* Load arguments*/
  for (tkn = strtok_r(argumentfilenames, " ", &savepoitner); tkn != NULL;
       tkn = strtok_r(NULL, " ", &savepoitner))
  {
    *esp -= strlen(tkn) + 1;
    if (*esp > PHYS_BASE - PGSIZE)
      memcpy(*esp, tkn, strlen(tkn) + 1);

    argvector[j] = (char *)*esp;
    j++;
  }
  argvector[j] = NULL;

  /* Word alignment */
  while ((int)*esp % 4 != 0)
  {
    *esp -= sizeof(char);

    uint8_t zero = 0;
    if (*esp > PHYS_BASE - PGSIZE)
      memcpy(*esp, &zero, sizeof(char));
  }

  /* argvector pointers (must load in reversed order) */
  for (int j = argumentcount; j >= 0; j--)
  {
    *esp -= sizeof(char *);
    if (*esp > PHYS_BASE - PGSIZE)
      memcpy(*esp, &argvector[j], sizeof(char *));
  }

  /* Load pointer to argvector */
  void *espb4 = *esp;
  *esp -= sizeof(char **);
  if (*esp > PHYS_BASE - PGSIZE)
    memcpy(*esp, &espb4, sizeof(char **));

  *esp -= sizeof(int);
  if (*esp > PHYS_BASE - PGSIZE)
    memcpy(*esp, &argumentcount, sizeof(int));
  *esp -= sizeof(uint32_t);
  uint32_t ret = 0;
  if (*esp > PHYS_BASE - PGSIZE)
    memcpy(*esp, &ret, sizeof(uint32_t));

  free(argvector);
  free(argumentoffilenamescopy);

  return result;
}
/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page(void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page(t->pagedir, upage) == NULL && pagedir_set_page(t->pagedir, upage, kpage, writable));
}