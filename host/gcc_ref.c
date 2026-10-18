/* GCC -O2 reference build for Stage 4.
 * heuristic(), search(), perm_rank(), orient_rank() are copied unchanged from
 * solver.c (PRECOMPUTED build). The harness around them does the same job as
 * rubic.s: parse the inlined 14-char state, solve, replay-check the path with
 * the move tables, print "R B' D2 ..." via Ripes ecalls, exit.
 * No libc: links with -nostdlib, so any __mulsi3/__divsi3 would fail to link.
 */
#include <stdint.h>

enum { CUBIES = 7, PERMUTATIONS = 5040, ORIENTATIONS = 729, MOVES = 9, MAX_DEPTH = 11 };
typedef struct { uint8_t p[CUBIES], o[CUBIES]; } state_t;

static const uint8_t first_move[3] = {0, 3, 6};
static const uint8_t move_face[MOVES] = {0, 0, 0, 1, 1, 1, 2, 2, 2};
static const uint8_t move_turns[MOVES] = {1, 2, 3, 1, 2, 3, 1, 2, 3};
#include "gcc_ref_tables.h"

static const char input_state[15] = "21345671111111";

/* ---- copied from solver.c ---- */
static uint16_t perm_rank(const state_t *state)
{
    uint32_t p = 0;
    for (uint8_t i = 0; i < CUBIES; ++i) {
        uint8_t smaller = 0;
        for (uint8_t j = (uint8_t) (i + 1U); j < CUBIES; ++j)
            if (state->p[j] < state->p[i])
                ++smaller;
        switch (i) {
        case 1: p = (p << 2) + (p << 1); break;
        case 2: p = (p << 2) + p; break;
        case 3: p = p << 2; break;
        case 4: p = (p << 1) + p; break;
        case 5: p = p << 1; break;
        default: break;
        }
        p += smaller;
    }
    return (uint16_t) p;
}

static uint16_t orient_rank(const state_t *state)
{
    uint32_t o = 0;
    for (uint8_t i = 0; i < 6; ++i)
        o = (o << 1) + o + state->o[i];
    return (uint16_t) o;
}

static uint8_t heuristic(uint16_t p, uint16_t o)
{
    return perm_dist[p] > orient_dist[o] ? perm_dist[p] : orient_dist[o];
}

static int search(uint16_t p0, uint16_t o0, uint8_t path[MAX_DEPTH])
{
    uint16_t p[MAX_DEPTH + 1], o[MAX_DEPTH + 1];
    uint8_t face[MAX_DEPTH], turn[MAX_DEPTH];
    p[0] = p0;
    o[0] = o0;
    if (!heuristic(p0, o0))
        return 0;
    for (int bound = heuristic(p0, o0); bound <= MAX_DEPTH; ++bound) {
        int d = 0;
        face[0] = 0;
        turn[0] = 0;
        p[1] = perm_move[0][p[0]];
        o[1] = orient_move[0][o[0]];
        for (;;) {
            int h = heuristic(p[d + 1], o[d + 1]);
            path[d] = (uint8_t) (first_move[face[d]] + turn[d]);
            if (!h)
                return d + 1;
            if (d + 1 + h <= bound) {
                ++d;
                face[d] = face[d - 1] ? 0 : 1;
                turn[d] = 0;
            } else {
                for (;;) {
                    if (++turn[d] < 3) {
                        uint8_t f = face[d];
                        p[d + 1] = perm_move[f][p[d + 1]];
                        o[d + 1] = orient_move[f][o[d + 1]];
                        goto visit;
                    }
                    turn[d] = 0;
                    if (++face[d] == (d ? face[d - 1] : 3))
                        ++face[d];
                    if (face[d] < 3)
                        break;
                    if (!d--)
                        goto next_bound;
                }
            }
            p[d + 1] = perm_move[face[d]][p[d]];
            o[d + 1] = orient_move[face[d]][o[d]];
        visit:;
        }
    next_bound:;
    }
    return -1;
}
/* ---- end of copy ---- */

static void ecall1(int code, int arg)
{
    register int a0 __asm__("a0") = arg;
    register int a7 __asm__("a7") = code;
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
}
static void putch(char c) { ecall1(11, c); }
static void puts_(const char *s) { while (*s) putch(*s++); }

int main(void)
{
    state_t state;
    uint8_t path[MAX_DEPTH];
    for (int i = 0; i < CUBIES; ++i) {
        state.p[i] = (uint8_t) (input_state[i] - '1');
        state.o[i] = (uint8_t) (input_state[i + CUBIES] - '1');
    }
    uint16_t p0 = perm_rank(&state), o0 = orient_rank(&state);
    int n = search(p0, o0, path);
    /* replay check with the move tables, same as rubic.s */
    uint16_t p = p0, o = o0;
    for (int i = 0; i < n; ++i)
        for (uint8_t k = 0; k < move_turns[path[i]]; ++k) {
            p = perm_move[move_face[path[i]]][p];
            o = orient_move[move_face[path[i]]][o];
        }
    if (n < 0 || p || o) {
        puts_("search failed\n");
        return 1;
    }
    for (int i = 0; i < n; ++i) {
        if (i) putch(' ');
        putch("RBD"[move_face[path[i]]]);
        uint8_t t = move_turns[path[i]];
        if (t == 2) putch('2');
        else if (t == 3) putch('\'');
    }
    putch('\n');
    return 0;
}

void _start(void)
{
    main();
    ecall1(10, 0);        /* Ripes exit */
}
