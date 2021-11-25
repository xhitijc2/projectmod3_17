struct thread *
lookup_tid(tid_t threadID)
{
    struct list_elem *elemnt;
    enum intr_level olderLevel;

    olderLevel = intr_disable();
    ASSERT(intr_get_level() == INTR_OFF);
    for (elemnt = list_begin(&all_list); elemnt != list_end(&all_list);
         elemnt = list_next(elemnt))
    {
        struct thread *currthread = list_entry(elemnt, struct thread, allelem);
        if (currthread->threadID == threadID)
        {
            intr_set_level(olderLevel);
            return currthread;
        }
    }
    intr_set_level(olderLevel);

    return 0;
}