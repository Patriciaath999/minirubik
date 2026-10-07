#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    CUBIES = 7,
    PERMUTATIONS = 5040,
    ORIENTATIONS = 729,
    MOVES = 9,
    MAX_DEPTH = 11
};

typedef struct {
    uint8_t p[CUBIES], o[CUBIES];
} state_t;

/*@ predicate valid_state(state_t *state) =
      (\forall integer i; 0 <= i < CUBIES ==>
         state->p[i] < CUBIES && state->o[i] < 3) &&
      (\forall integer i, j; 0 <= i < j < CUBIES ==>
         state->p[i] != state->p[j]) &&
      (state->o[0] + state->o[1] + state->o[2] + state->o[3] +
       state->o[4] + state->o[5] + state->o[6]) % 3 == 0;
 */

static const char *const move_names[MOVES] = {"R",  "R2", "R'", "B", "B2",
                                              "B'", "D",  "D2", "D'"};
static const uint8_t inverse_move[MOVES] = {2, 1, 0, 5, 4, 3, 8, 7, 6};
static const uint8_t move_face[MOVES] = {0, 0, 0, 1, 1, 1, 2, 2, 2};
static const uint8_t move_turns[MOVES] = {1, 2, 3, 1, 2, 3, 1, 2, 3};
static const uint8_t first_move[3] = {0, 3, 6}; /* face * 3 */
/* Each destination takes a cubie from source[face][destination]. */
static const uint8_t source[3][CUBIES] = {
    {1, 4, 2, 0, 3, 5, 6},
    {0, 1, 2, 4, 5, 6, 3},
    {0, 2, 5, 3, 1, 4, 6},
};
static const uint8_t twist[3][CUBIES] = {
    {1, 2, 0, 2, 1, 0, 0},
    {0, 0, 0, 1, 2, 1, 2},
    {0, 0, 0, 0, 0, 0, 0},
};

/* The three quarter-turns preserve the fixed front-upper-left corner. */
/*@ requires face < 3;
    assigns \nothing;
    ensures \forall integer i; 0 <= i < CUBIES ==>
              \result.p[i] == state.p[source[face][i]];
    ensures \forall integer i; 0 <= i < CUBIES ==>
              \result.o[i] == (state.o[source[face][i]] + twist[face][i]) % 3;
 */
static state_t quarter_turn(state_t state, uint8_t face)
{
    state_t result;
    /*@ loop invariant 0 <= i <= CUBIES;
        loop invariant \forall integer j; 0 <= j < i ==>
          result.p[j] == state.p[source[face][j]];
        loop invariant \forall integer j; 0 <= j < i ==>
          result.o[j] == (state.o[source[face][j]] + twist[face][j]) % 3;
        loop assigns i, result.p[0..6], result.o[0..6];
        loop variant CUBIES - i;
    */
    for (uint8_t i = 0; i < CUBIES; ++i) {
        uint8_t from = source[face][i];
        result.p[i] = state.p[from];
        uint8_t sum = (uint8_t) (state.o[from] + twist[face][i]); 
        result.o[i] = (uint8_t) (sum >= 3U ? sum - 3U : sum); 
    }
    return result;
}

static state_t apply_move(state_t state, uint8_t move)
{
    uint8_t turns = move_turns[move];
    for (uint8_t i = 0; i < turns; ++i)
        state = quarter_turn(state, move_face[move]);
    return state;
}

/*@ requires \valid_read(state);
    requires \forall integer i; 0 <= i < CUBIES ==>
      0 <= state->p[i] < CUBIES;
    requires \forall integer i, j; 0 <= i < j < CUBIES ==>
      state->p[i] != state->p[j];
    assigns \nothing;
    ensures \result < PERMUTATIONS;
 */
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


static int valid(const state_t *state)
{
    uint8_t sum = 0;
    /*@ loop invariant 0 <= i <= CUBIES;
        loop invariant sum <= 2 * i;
        loop invariant sum == (i > 0 ? state->o[0] : 0) +
          (i > 1 ? state->o[1] : 0) + (i > 2 ? state->o[2] : 0) +
          (i > 3 ? state->o[3] : 0) + (i > 4 ? state->o[4] : 0) +
          (i > 5 ? state->o[5] : 0) + (i > 6 ? state->o[6] : 0);
        loop invariant \forall integer j; 0 <= j < i ==>
          state->p[j] < CUBIES && state->o[j] < 3;
        loop invariant \forall integer j, k; 0 <= j < k < i ==>
          state->p[j] != state->p[k];
        loop assigns i, sum;
        loop variant CUBIES - i;
    */
    for (uint8_t i = 0; i < CUBIES; ++i) {
        if (state->p[i] >= CUBIES || state->o[i] >= 3)
            return 0;
        /*@ loop invariant 0 <= j <= i;
            loop invariant \forall integer k; 0 <= k < j ==>
              state->p[k] != state->p[i];
            loop assigns j;
            loop variant i - j;
        */
        for (uint8_t j = 0; j < i; ++j)
            if (state->p[j] == state->p[i])
                return 0;
        sum = (uint8_t) (sum + state->o[i]);
    }
    while (sum >= 3U)
        sum = (uint8_t) (sum - 3U); 
    return sum == 0;
}


#define UNSEEN UINT8_MAX
#ifdef PRECOMPUTED
#include "tables.h"
static int build_tables(void) { return 1; }
#else
static uint8_t perm_dist[PERMUTATIONS], orient_dist[ORIENTATIONS];
static uint16_t perm_move[3][PERMUTATIONS], orient_move[3][ORIENTATIONS];

static int build_tables(void)
{
    static state_t queue[PERMUTATIONS];
    const state_t solved = {{0, 1, 2, 3, 4, 5, 6}, {0}};
    uint16_t head, tail;

    memset(perm_dist, UNSEEN, sizeof perm_dist);
    queue[0] = solved;
    perm_dist[0] = 0;
    for (head = 0, tail = 1; head < tail; ++head) {
        uint16_t here = perm_rank(&queue[head]);
        for (uint8_t face = 0; face < 3; ++face) {
            state_t next = queue[head];
            
            for (uint8_t turn = 0; turn < 3; ++turn) {
                next = quarter_turn(next, face);
                memset(next.o, 0, sizeof next.o);
                uint16_t there = perm_rank(&next);
                if (turn == 0)
                    perm_move[face][here] = there;
                if (perm_dist[there] == UNSEEN) {
                    perm_dist[there] = (uint8_t) (perm_dist[here] + 1U);
                    queue[tail++] = next;
                }
            }
        }
    }
    if (tail != PERMUTATIONS)
        return 0;

    memset(orient_dist, UNSEEN, sizeof orient_dist);
    queue[0] = solved;
    orient_dist[0] = 0;
    for (head = 0, tail = 1; head < tail; ++head) {
        uint16_t here = orient_rank(&queue[head]);
        for (uint8_t face = 0; face < 3; ++face) {
            state_t next = queue[head];
           
            for (uint8_t turn = 0; turn < 3; ++turn) {
                next = quarter_turn(next, face);
                memcpy(next.p, solved.p, sizeof next.p); 
                uint16_t there = orient_rank(&next);
                if (turn == 0)
                    orient_move[face][here] = there;
                if (orient_dist[there] == UNSEEN) {
                    orient_dist[there] = (uint8_t) (orient_dist[here] + 1U);
                    queue[tail++] = next;
                }
            }
        }
    }
    return tail == ORIENTATIONS;
}

static void emit_row(const void *data, int count, int wide)
{
    printf("{");
    for (int i = 0; i < count; ++i)
        printf("%s%u", i ? (i & 15 ? "," : ",\n ") : "\n ",
               wide ? ((const uint16_t *) data)[i]
                    : ((const uint8_t *) data)[i]);
    printf("}");
}

static void emit_tables(void)
{
    
    printf("static const uint8_t perm_dist[PERMUTATIONS] = ");
    emit_row(perm_dist, PERMUTATIONS, 0);
    printf(";\nstatic const uint8_t orient_dist[ORIENTATIONS] = ");
    emit_row(orient_dist, ORIENTATIONS, 0);
    printf(";\nstatic const uint16_t perm_move[3][PERMUTATIONS] = {");
    for (int f = 0; f < 3; ++f, printf(f < 3 ? ",\n" : ""))
        emit_row(perm_move[f], PERMUTATIONS, 1);
    printf("};\nstatic const uint16_t orient_move[3][ORIENTATIONS] = {");
    for (int f = 0; f < 3; ++f, printf(f < 3 ? ",\n" : ""))
        emit_row(orient_move[f], ORIENTATIONS, 1);
    printf("};\n");
}
#endif

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

/*@ requires valid_read_string(input);
    requires \valid(state);
    assigns state->p[0..6], state->o[0..6];
    ensures \result != 0 ==> input[14] == '\0';
    ensures \result != 0 ==> \forall integer i; 0 <= i < CUBIES ==>
      state->p[i] < CUBIES && state->o[i] < 3;
    ensures \result != 0 ==> \forall integer i, j; 0 <= i < j < CUBIES ==>
      state->p[i] != state->p[j];
    ensures \result != 0 ==>
      (state->o[0] + state->o[1] + state->o[2] + state->o[3] +
       state->o[4] + state->o[5] + state->o[6]) % 3 == 0;
    ensures \result != 0 ==> \forall integer i; 0 <= i < CUBIES ==>
      state->p[i] == input[i] - '1';
    ensures \result != 0 ==> \forall integer i; 0 <= i < CUBIES ==>
      state->o[i] == input[i + CUBIES] - '1';
 */
static int parse_state(const char *input, state_t *state)
{
    /*@ loop invariant 0 <= i <= 14;
        loop invariant i <= strlen(input);
        loop invariant i <= 7 ==> \initialized(&state->p[0..i-1]);
        loop invariant i >= 7 ==> \initialized(&state->p[0..6]);
        loop invariant i >= 7 ==> \initialized(&state->o[0..i-8]);
        loop invariant \forall integer j; 0 <= j < i && j < CUBIES ==>
          state->p[j] == input[j] - '1';
        loop invariant \forall integer j; 0 <= j < i - CUBIES ==>
          state->o[j] == input[j + CUBIES] - '1';
        loop assigns i, state->p[0..6], state->o[0..6];
        loop variant 14 - i;
     */
    for (int i = 0; i < 14; ++i) {
        int limit = i < 7 ? 7 : 3;
        if (input[i] < '1' || input[i] > '0' + limit)
            return 0;
        (i < 7 ? state->p : state->o)[i < 7 ? i : i - 7] = (uint8_t) (input[i] - '1');
    }
    return input[14] == '\0' && valid(state);
}

/* stdout is fully buffered off a terminal, so a write error surfaces at the
 * flush, not at the printf that queued the bytes. Every exit path that has
 * produced output goes through here.
 */
static int output_failed(void)
{
    return fflush(stdout) != 0 || ferror(stdout);
}

static int self_test(void)
{
    const state_t solved = {{0, 1, 2, 3, 4, 5, 6}, {0}};
    state_t state;
    for (uint8_t move = 0; move < MOVES; ++move) {
        state = solved;
        state = apply_move(state, move);
        state = apply_move(state, inverse_move[move]);
        if (memcmp(&solved, &state, sizeof solved))
            return 0;
    }
    return 1;
}



int main(int argc, char **argv)
{
    state_t state;
    uint8_t path[MAX_DEPTH];
    if (argc == 2 && !strcmp(argv[1], "--self-test")) {
        if (!self_test()) {
            fputs("self-test failed\n", stderr);
            return 1;
        }
        if (!build_tables()) {
            fputs("could not build pattern tables\n", stderr);
            return 1;
        }
        puts("5040 permutations; 729 orientations");
        return output_failed();
    }
#ifndef PRECOMPUTED
    if (argc == 2 && !strcmp(argv[1], "--emit-tables")) {
        if (!build_tables()) {
            fputs("could not build pattern tables\n", stderr);
            return 1;
        }
        emit_tables();
        return output_failed();
    }
#endif
    if (argc != 2 || !parse_state(argv[1], &state)) {

        fprintf(stderr, "usage: %s PPPPPPPOOOOOOO\n",
                argc > 0 && argv[0] ? argv[0] : "solver");
        return 2;
    }
    if (!build_tables()) {
        fputs("could not build pattern tables\n", stderr);
        return 1;
    }
    int n = search(perm_rank(&state), orient_rank(&state), path);
    if (n < 0) {
    fputs("search failed\n", stderr);
    return 1;
    }
    for (int i = 0; i < n; ++i)
        printf("%s%s", i ? " " : "", move_names[path[i]]);
    putchar('\n');
    return output_failed();
}
