#include "userprog/exception.h"
#include <inttypes.h>
#include <stdio.h>
#include "userprog/gdt.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/syscall.h"

/* Number of page faults processed. */
static long long page_fault_count;

static void kill(struct intr_frame *);
static void page_fault(struct intr_frame *);

void exception_init(void)
{

   intr_register_int(3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
   intr_register_int(4, 3, INTR_ON, kill, "#OF Overflow Exception");
   intr_register_int(5, 3, INTR_ON, kill,
                     "#BR BOUND Range Exceeded Exception");

   intr_register_int(0, 0, INTR_ON, kill, "#DE Divide Error");
   intr_register_int(1, 0, INTR_ON, kill, "#DB Debug Exception");
   intr_register_int(6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
   intr_register_int(7, 0, INTR_ON, kill,
                     "#NM Device Not Available Exception");
   intr_register_int(11, 0, INTR_ON, kill, "#NP Segment Not Present");
   intr_register_int(12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
   intr_register_int(13, 0, INTR_ON, kill, "#GP General Protection Exception");
   intr_register_int(16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
   intr_register_int(19, 0, INTR_ON, kill,
                     "#XF SIMD Floating-Point Exception");

   intr_register_int(14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
}

void exception_print_stats(void)
{
   printf(" %lld page faults\n", page_fault_count);
}

static void kill(struct intr_frame *framee
{
   switch (framee->cs)
   {
   case SEL_UCSEG:
      /*  Kill the user process.  */
      printf("%s: dying due to interrupt %#04x (%s).\n",
             thread_name(), framee->vec_no, intr_name(framee->vec_no));
      intr_dump_frame(framee);
      thread_exit();

   case SEL_KCSEG:

      intr_dump_frame(framee);
      PANIC("Kernel bug - unexpected interrupt");

   default:

      printf("Interrupt %#04x (%s) in unknown segment %04x\n",
             framee->vec_no, intr_name(framee->vec_no), framee->cs);
      thread_exit();
   }
}

static void page_fault(struct intr_frame *framee)
{
   bool not_present; /* True: not-present page, false: writing r/o page. */
   bool write;       /* True: access was write, false: access was read. */
   bool user;        /* True: access by user, false: access by kernel. */
   void *fault_addr; /* Fault address. */

   asm("movl %%cr2, %0"
       : "=r"(fault_addr));

   intr_enable();

   /* Count page faults. */
   page_fault_count++;

   if (!not_present)
      exit(-1);

   if (fault_addr == NULL || !is_user_vaddr(fault_addr) || !pagedir_get_page(thread_current()->pagedir, fault_addr))
      exit(-1);

   not_present = (framee->error_code & PF_P) == 0;
   write = (framee->error_code & PF_W) != 0;
   user = (framee->error_code & PF_U) != 0;

   /* implement virtual memory */
   printf("Page fault at %p: %s error %s page in %s context.\n",
          fault_addr,
          not_present ? "not present" : "rights violation",
          write ? "writing" : "reading",
          user ? "user" : "kernel");
   kill(framee);
}