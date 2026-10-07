#define main orig_main
#include "solver_orig.c"
#undef main

static uint8_t *table;

int oracle_init(void)
{
    uint8_t diameter;
    table = build_table(&diameter);
    return table && diameter == 11;
}


int oracle_distance(uint32_t rank)
{
    state_t s;
    int d = 0;
    unrank_state(rank, &s);
    while (rank) {
        s = apply_move(s, table[rank]);
        rank = rank_state(&s);
        if (++d > 11)
            return -1;
    }
    return d;
}

void oracle_digits(uint32_t rank, char out[15])
{
    state_t s;
    unrank_state(rank, &s);
    for (int i = 0; i < 7; i++) {
        out[i] = (char) ('1' + s.p[i]);
        out[i + 7] = (char) ('1' + s.o[i]);
    }
    out[14] = 0;
}
