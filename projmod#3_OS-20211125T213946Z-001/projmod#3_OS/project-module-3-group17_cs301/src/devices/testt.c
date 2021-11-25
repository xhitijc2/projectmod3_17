void timer_calibrate(void)
{
    unsigned higherBit;
    unsigned testerBit;

    ASSERT(intr_get_level() == INTR_ON);
    printf("Calibrating timer...  ");

    loops_per_tick = 1u << 10;
    while (!too_many_loops(loops_per_tick << 1))
    {
        loops_per_tick <<= 1;
        ASSERT(loops_per_tick != 0);
    }

    higherBit = loops_per_tick;
    for (testerBit = higherBit >> 1; testerBit != higherBit >> 10; testerBit >>= 1)
        if (!too_many_loops(higherBit | testerBit))
            loops_per_tick |= testerBit;

    printf("%'" PRIu64 " loops/s.\n", (uint64_t)loops_per_tick * TIMER_FREQ);
}
