#define main solver_main
#include "solver.c"
#undef main
#include <stdlib.h>
#include <time.h>

int oracle_init(void);
int oracle_distance(uint32_t rank);
void oracle_digits(uint32_t rank, char out[15]);


static unsigned long nodes;
static int counted_search(uint16_t p0, uint16_t o0, uint8_t path[MAX_DEPTH])
{
    uint16_t p[MAX_DEPTH + 1], o[MAX_DEPTH + 1];
    uint8_t face[MAX_DEPTH], turn[MAX_DEPTH];
    p[0] = p0; o[0] = o0;
    if (!heuristic(p0, o0)) return 0;
    for (int bound = heuristic(p0, o0); bound <= MAX_DEPTH; ++bound) {
        int d = 0;
        face[0] = 0; turn[0] = 0;
        p[1] = perm_move[0][p[0]]; o[1] = orient_move[0][o[0]];
        for (;;) {
            nodes++;
            int h = heuristic(p[d + 1], o[d + 1]);
            path[d] = (uint8_t) (first_move[face[d]] + turn[d]);
            if (!h) return d + 1;
            if (d + 1 + h <= bound) { ++d; face[d] = face[d - 1] ? 0 : 1; turn[d] = 0; }
            else {
                for (;;) {
                    if (++turn[d] < 3) { uint8_t f = face[d];
                        p[d + 1] = perm_move[f][p[d + 1]]; o[d + 1] = orient_move[f][o[d + 1]]; goto visit; }
                    turn[d] = 0;
                    if (++face[d] == (d ? face[d - 1] : 3)) ++face[d];
                    if (face[d] < 3) break;
                    if (!d--) goto next_bound;
                }
            }
            p[d + 1] = perm_move[face[d]][p[d]]; o[d + 1] = orient_move[face[d]][o[d]];
        visit:;
        }
    next_bound:;
    }
    return -1;
}

typedef struct { uint32_t rank; unsigned long nodes; } hard_t;
static int by_nodes(const void *a, const void *b)
{
    unsigned long x = ((const hard_t *) a)->nodes, y = ((const hard_t *) b)->nodes;
    return x < y ? 1 : x > y ? -1 : 0;
}

int main(void)
{
    const uint32_t N = (uint32_t) PERMUTATIONS * ORIENTATIONS;
    static uint8_t dist[(uint32_t) PERMUTATIONS * ORIENTATIONS];
    int fail = 0;

    if (!build_tables()) { puts("H2 FAIL: build_tables"); return 1; }
    if (!oracle_init()) { puts("oracle FAIL: original build_table"); return 1; }

    /* H2 */
    int unseen = 0, mp = 0, mo = 0;
    for (int i = 0; i < PERMUTATIONS; i++) {
        if (perm_dist[i] == UNSEEN) unseen++;
        if (perm_dist[i] > mp) mp = perm_dist[i];
        for (int f = 0; f < 3; f++) if (perm_move[f][i] >= PERMUTATIONS) unseen++;
    }
    for (int i = 0; i < ORIENTATIONS; i++) {
        if (orient_dist[i] == UNSEEN) unseen++;
        if (orient_dist[i] > mo) mo = orient_dist[i];
        for (int f = 0; f < 3; f++) if (orient_move[f][i] >= ORIENTATIONS) unseen++;
    }
    int h2 = !unseen && !perm_dist[0] && !orient_dist[0];
    printf("H2: unfilled=%d  perm_dist max=%d solved=%d  orient_dist max=%d solved=%d  -> %s\n",
           unseen, mp, perm_dist[0], mo, orient_dist[0], h2 ? "PASS" : "FAIL");
    fail |= !h2;

    int diameter = 0;
    unsigned count11 = 0;
    for (uint32_t r = 0; r < N; r++) {
        int d = oracle_distance(r);
        if (d < 0) { printf("oracle FAIL at rank %u\n", r); return 1; }
        dist[r] = (uint8_t) d;
        if (d > diameter) diameter = d;
        if (d == 11) count11++;
    }
    printf("oracle: %u states, diameter %d, %u at distance 11\n", N, diameter, count11);

    /* H1: heuristic never exceeds the exact distance. */
    unsigned h1bad = 0;
    for (uint32_t r = 0; r < N; r++)
        if (heuristic((uint16_t) (r / ORIENTATIONS), (uint16_t) (r % ORIENTATIONS)) > dist[r])
            h1bad++;
    printf("H1: %u states where heuristic > exact distance  -> %s\n", h1bad, h1bad ? "FAIL" : "PASS");
    fail |= h1bad != 0;

    /* H3 */
    hard_t *hard = malloc(count11 * sizeof *hard);
    unsigned h3bad = 0, k = 0;
    unsigned long total = 0, worst = 0;
    uint8_t path[MAX_DEPTH];
    clock_t t0 = clock();
    for (uint32_t r = 0; r < N; r++) {
        uint16_t p = (uint16_t) (r / ORIENTATIONS), o = (uint16_t) (r % ORIENTATIONS);
        nodes = 0;
        int n = counted_search(p, o, path);
        total += nodes;
        if (nodes > worst) worst = nodes;
        char digits[15];
        state_t s;
        oracle_digits(r, digits);
        if (n != dist[r] || !parse_state(digits, &s) || !solves(s, path, n)) {
            if (h3bad < 5) printf("  H3 mismatch %s: got %d, exact %d\n", digits, n, dist[r]);
            h3bad++;
        }
        if (dist[r] == 11) { hard[k].rank = r; hard[k].nodes = nodes; k++; }
        if ((r & 0x3FFFF) == 0) { fprintf(stderr, "\r  H3 %3u%%", (unsigned) (100.0 * r / N)); }
    }
    double secs = (double) (clock() - t0) / CLOCKS_PER_SEC;
    fprintf(stderr, "\r  H3 100%%\n");
    printf("H3: %u states, %u wrong, %.1f s, mean %.0f nodes, worst %lu nodes  -> %s\n",
           N, h3bad, secs, (double) total / N, worst, h3bad ? "FAIL" : "PASS");
    fail |= h3bad != 0;

    
    qsort(hard, k, sizeof *hard, by_nodes);
    FILE *out = fopen("d11.txt", "w");
    for (unsigned i = 0; i < k; i++) {
        char digits[15];
        oracle_digits(hard[i].rank, digits);
        fprintf(out, "%s %lu\n", digits, hard[i].nodes);
    }
    fclose(out);
    char a[15], b[15];
    oracle_digits(hard[0].rank, a);
    oracle_digits(hard[k - 1].rank, b);
    printf("distance 11: hardest %s (%lu nodes), easiest %s (%lu nodes), ratio %.1fx -> d11.txt\n",
           a, hard[0].nodes, b, hard[k - 1].nodes, (double) hard[0].nodes / hard[k - 1].nodes);
    free(hard);
    return fail;
}
