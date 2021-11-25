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