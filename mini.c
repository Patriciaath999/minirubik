#include <stdio.h>
#include <string.h>

enum { C = 7, P = 5040, O = 729 };
typedef struct {
    unsigned char p[C], o[C];
} state_t;

static state_t turn(state_t s, int f)
{
    static const char map[][15] = {
        "14203561202100",
        "01245630001212",
        "02531460000000",
    };
    state_t t;
    for (int i = 0; i < C; ++i) {
        int j = map[f][i] - '0', v = s.o[j] + map[f][i + C] - '0';
        t.p[i] = s.p[j];
        t.o[i] = v > 2 ? v - 3 : v;
    }
    return t;
}

static unsigned rank_p(state_t s)
{
    unsigned p = 0;
    for (int i = 0; i < C; ++i) {
        unsigned n = 0;
        for (int j = i + 1; j < C; ++j)
            n += s.p[j] < s.p[i];
        p = (i == 1 ? (p << 2) + (p << 1) : i == 2 ? (p << 2) + p :
             i == 3 ? p << 2 : i == 4 ? (p << 1) + p : i == 5 ? p << 1 : p) + n;
    }
    return p;
}

static unsigned rank_o(state_t s)
{
    unsigned o = 0;
    for (int i = 0; i < 6; ++i)
        o += o + o + s.o[i];
    return o;
}

#ifdef PRECOMPUTED

#include <stdint.h>
enum { PERMUTATIONS = P, ORIENTATIONS = O };
#include "tables.h"
#define pd perm_dist
#define od orient_dist
#define pm perm_move
#define om orient_move
#define bfs(x) ((void) 0)
#else

static unsigned char pd[P], od[O];
static unsigned short pm[3][P], om[3][O];

static void bfs(int orient)
{
    static state_t queue[P];
    const state_t solved = {{0, 1, 2, 3, 4, 5, 6}, {0}};
    size_t head = 0, tail = 1;
    queue[0] = solved;
    memset(orient ? od : pd, 255, orient ? O : P);
    (orient ? od : pd)[0] = 0;
    while (head < tail) {
        state_t from = queue[head++];
        unsigned here = orient ? rank_o(from) : rank_p(from);
        for (int f = 0; f < 3; ++f) {
            state_t to = from;
            for (int n = 0; n < 3; ++n) {
                to = turn(to, f);
                if (orient)
                    memcpy(to.p, solved.p, C);
                else
                    memset(to.o, 0, C);
                unsigned there = orient ? rank_o(to) : rank_p(to);
                unsigned char *d = orient ? od : pd;
                if (!n) {
                    if (orient)
                        om[f][here] = there;
                    else
                        pm[f][here] = there;
                }
                if (d[there] == 255) {
                    d[there] = d[here] + 1;
                    queue[tail++] = to;
                }
            }
        }
    }
}
#endif

int main(int argc, char **argv)
{
    state_t s;
    unsigned seen = 0, sum = 0;
    if (argc != 2 || strlen(argv[1]) != 14)
        return 2;
    for (int i = 0; i < C; ++i) {
        unsigned p = (unsigned) (argv[1][i] - '1');
        unsigned o = (unsigned) (argv[1][i + C] - '1');
        if (p >= C || o >= 3 || seen >> p & 1)
            return 2;
        s.p[i] = p;
        s.o[i] = o;
        seen |= 1U << p;
        sum += o;
    }
    while (sum > 2)
        sum -= 3;
    if (sum)
        return 2;

    bfs(0);
    bfs(1);

    unsigned p[12], o[12], f[11], k[11];
    int n = -1, d;
    p[0] = rank_p(s);
    o[0] = rank_o(s);
#define H(i) (pd[p[i]] > od[o[i]] ? pd[p[i]] : od[o[i]])
    for (int bound = H(0); n < 0 && bound <= 11; ++bound) {
        if (!H(0)) {
            n = 0;
            break;
        }
        f[0] = k[0] = d = 0;
        p[1] = pm[0][p[0]];
        o[1] = om[0][o[0]];
        while (d >= 0) {
            if (!H(d + 1)) {
                n = d + 1;
                break;
            }
            if (d + 1 + H(d + 1) <= bound) {
                ++d;
                f[d] = !f[d - 1];
                k[d] = 0;
            } else {
                while (d >= 0 && ++k[d] > 2) {
                    k[d] = 0;
                    f[d] += 1 + (d && f[d] + 1 == f[d - 1]);
                    if (f[d] < 3)
                        break;
                    --d;
                }
                if (d < 0)
                    break;
                if (k[d]) {
                    p[d + 1] = pm[f[d]][p[d + 1]];
                    o[d + 1] = om[f[d]][o[d + 1]];
                    continue;
                }
            }
            p[d + 1] = pm[f[d]][p[d]];
            o[d + 1] = om[f[d]][o[d]];
        }
    }
    if (n < 0)
        return 1;

    state_t t = s;
    for (int i = 0; i < n; ++i)
        for (unsigned q = 0; q <= k[i]; ++q)
            t = turn(t, f[i]);
    if (rank_p(t) || rank_o(t))
        return 1;

    const char *sep = "";
    for (int i = 0; i < n; ++i) {
        printf("%s%c%s", sep, "RBD"[f[i]],
               (const char *[]) {"", "2", "'"}[k[i]]);
        sep = " ";
    }
    return putchar('\n') < 0 || fflush(stdout) || ferror(stdout);
}
