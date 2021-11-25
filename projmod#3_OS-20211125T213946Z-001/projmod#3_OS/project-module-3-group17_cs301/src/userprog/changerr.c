static void
start_process(void *filenamearg)
{
    struct intr_frame intrrframe;
    bool res;
    struct thread *parentthread;
    struct thread *current_thread = thread_current();

    pt_suppl_init(&current_thread->pt_suppl);
    lock_init(&current_thread->pt_suppl_lock);

    /* Initialize interrupt frame and load executable. */
    memset(&intrrframe, 0, sizeof intrrframe);
    intrrframe.gs = intrrframe.fs = intrrframe.es = intrrframe.ds = intrrframe.ss = SEL_UDSEG;
    intrrframe.cs = SEL_UCSEG;
    intrrframe.eflags = FLAG_IF | FLAG_MBS;
    res = load((struct args_struct *)filenamearg, &intrrframe.eip, &intrrframe.esp);

    parentthread = lookup_tid(current_thread->parent_tid);
    if (parentthread != NULL)
    {
        parentthread->child_born_status = (res ? 1 : -1);
        sema_up(&parentthread->child_sema);
    }

    if (!res)
    {
        thread_exit();
    }

    palloc_free_page(filenamearg);

    /* Start the user process by simulating a return from an
       interrupt, implemented by intr_exit (in
       threads/intr-stubs.S).  Because intr_exit takes all of its
       arguments on the stack in the form of a `struct intr_frame',
       we just point the stack pointer (%esp) to our stack frame
       and jump to it. */
    asm volatile("movl %0, %%esp; jmp intr_exit"
                 :
                 : "g"(&intrrframe)
                 : "memory");
    NOT_REACHED();
}