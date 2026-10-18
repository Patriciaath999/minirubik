/* gates_asm.c -- host gates H1, H2, H3 for the RV32I build (rubic.elf).
 *
 * Unlike gates.c, which checks the C solver, this checks the shipped binary:
 *   - the distance and move tables are read out of rubic.elf by symbol,
 *     i.e. exactly the bytes the assembly links against;
 *   - H3 executes rubic.elf's own instructions in an RV32I ISS for every
 *     one of the 3,674,160 states, with input_state patched per run.
 * Ground truth is an independent BFS on the cubie model (source/twist from
 * solver.c), sharing no table with the program under test.
 *
 * Build: gcc -O2 -o gates_asm gates_asm.c
 * Run:   ./gates_asm rubic.elf [workers] [stride-start]
 *        (workers = 0 runs H1/H2 only)
 *        prints H1/H2/H3 PASS/FAIL, writes d11_asm.txt (distance-11 states,
 *        hardest first, with retired-instruction counts).
 */
#include <elf.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define NP 5040u
#define NO 729u
#define N (NP * NO)

/* ---------------- independent cubie model (from solver.c) ---------------- */
static const uint8_t SRC[3][7] = {{1, 4, 2, 0, 3, 5, 6}, {0, 1, 2, 4, 5, 6, 3}, {0, 2, 5, 3, 1, 4, 6}};
static const uint8_t TW[3][7] = {{1, 2, 0, 2, 1, 0, 0}, {0, 0, 0, 1, 2, 1, 2}, {0, 0, 0, 0, 0, 0, 0}};
typedef struct { uint8_t p[7], o[7]; } cube_t;

static cube_t turn(cube_t c, int f)
{
    cube_t r;
    for (int i = 0; i < 7; i++) {
        r.p[i] = c.p[SRC[f][i]];
        r.o[i] = (uint8_t) ((c.o[SRC[f][i]] + TW[f][i]) % 3);
    }
    return r;
}
static uint32_t rank_p(const uint8_t *p)
{
    uint32_t r = 0;
    for (int i = 0; i < 7; i++) {
        int s = 0;
        for (int j = i + 1; j < 7; j++) s += p[j] < p[i];
        r = r * (7 - i) + s;
    }
    return r;
}
static uint32_t rank_o(const uint8_t *o)
{
    uint32_t r = 0;
    for (int i = 0; i < 6; i++) r = r * 3 + o[i];
    return r;
}
static uint32_t rank(cube_t c) { return rank_p(c.p) * NO + rank_o(c.o); }
static cube_t unrank(uint32_t r)
{
    cube_t c;
    uint32_t p = r / NO, o = r % NO, f = 720, sum = 0;
    uint8_t av[7] = {0, 1, 2, 3, 4, 5, 6};
    for (int i = 0; i < 7; i++) {
        uint32_t q = p / f;
        p %= f;
        c.p[i] = av[q];
        for (int j = q; j + 1 < 7 - i; j++) av[j] = av[j + 1];
        if (i < 6) f /= 6 - i;
    }
    for (int i = 5; i >= 0; i--) { c.o[i] = o % 3; sum += c.o[i]; o /= 3; }
    c.o[6] = (3 - sum % 3) % 3;
    return c;
}

/* ---------------- ELF loading and symbols ---------------- */
#define MEMSZ (1u << 20)
#define DBASE 0x10000000u
static uint8_t text[MEMSZ], data[MEMSZ], data0[MEMSZ];
static uint32_t data_hi;          /* bytes of .data to restore per run */
static uint32_t entry;
static uint8_t *elf;
static Elf32_Ehdr *eh;

static void load_elf(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    elf = malloc(n);
    if (fread(elf, 1, n, f) != (size_t) n) exit(2);
    fclose(f);
    eh = (Elf32_Ehdr *) elf;
    if (eh->e_machine != EM_RISCV || eh->e_ident[EI_CLASS] != ELFCLASS32) {
        fprintf(stderr, "not an RV32 ELF\n");
        exit(2);
    }
    Elf32_Phdr *ph = (Elf32_Phdr *) (elf + eh->e_phoff);
    for (int i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD) continue;
        uint32_t va = ph[i].p_vaddr;
        if (va < MEMSZ) memcpy(text + va, elf + ph[i].p_offset, ph[i].p_filesz);
        else if (va >= DBASE) {
            memcpy(data + va - DBASE, elf + ph[i].p_offset, ph[i].p_filesz);
            if (va - DBASE + ph[i].p_memsz > data_hi) data_hi = va - DBASE + ph[i].p_memsz;
        }
    }
    memcpy(data0, data, data_hi);
    entry = eh->e_entry;
}
static uint32_t sym(const char *name)
{
    Elf32_Shdr *sh = (Elf32_Shdr *) (elf + eh->e_shoff);
    for (int i = 0; i < eh->e_shnum; i++) {
        if (sh[i].sh_type != SHT_SYMTAB) continue;
        Elf32_Sym *s = (Elf32_Sym *) (elf + sh[i].sh_offset);
        const char *str = (const char *) (elf + sh[sh[i].sh_link].sh_offset);
        for (uint32_t k = 0; k < sh[i].sh_size / sizeof *s; k++)
            if (!strcmp(str + s[k].st_name, name)) return s[k].st_value;
    }
    fprintf(stderr, "symbol %s not found\n", name);
    exit(2);
}
static uint8_t *at(uint32_t a)
{
    if (a < MEMSZ) return &text[a];
    if (a >= DBASE && a - DBASE < MEMSZ) return &data[a - DBASE];
    return NULL;
}

/* ---------------- RV32I ISS (aborts on anything outside RV32I) ---------------- */
#define SX(v, b) ((int32_t) ((uint32_t) (v) << (32 - (b))) >> (32 - (b)))
static char out[256];
static int outn;

/* returns 0 on clean exit (a7=10), nonzero on any fault */
static int run(uint64_t *retired, uint64_t limit)
{
    uint32_t x[32] = {0}, pc = entry;
    uint64_t n = 0;
    outn = 0;
    for (;;) {
        if (++n > limit) return 9;
        uint8_t *ip = at(pc);
        if (!ip || (pc & 3) || pc >= MEMSZ) return 8;
        uint32_t in;
        memcpy(&in, ip, 4);
        uint32_t op = in & 0x7f, rd = (in >> 7) & 31, f3 = (in >> 12) & 7,
                 r1 = x[(in >> 15) & 31], r2 = x[(in >> 20) & 31], f7 = in >> 25,
                 npc = pc + 4, v = 0, a;
        int32_t ii = SX(in >> 20, 12);
        int wr = 1;
        uint8_t *m;
        switch (op) {
        case 0x37: v = in & 0xfffff000u; break;
        case 0x17: v = pc + (in & 0xfffff000u); break;
        case 0x6f:
            v = npc;
            npc = pc + SX(((in >> 31) << 20) | (((in >> 12) & 0xff) << 12) |
                          (((in >> 20) & 1) << 11) | (((in >> 21) & 0x3ff) << 1), 21);
            break;
        case 0x67: if (f3) return 7; v = npc; npc = (r1 + ii) & ~1u; break;
        case 0x63: {
            int32_t b = SX(((in >> 31) << 12) | (((in >> 7) & 1) << 11) |
                           (((in >> 25) & 0x3f) << 5) | (((in >> 8) & 0xf) << 1), 13);
            int t;
            switch (f3) {
            case 0: t = r1 == r2; break;
            case 1: t = r1 != r2; break;
            case 4: t = (int32_t) r1 < (int32_t) r2; break;
            case 5: t = (int32_t) r1 >= (int32_t) r2; break;
            case 6: t = r1 < r2; break;
            case 7: t = r1 >= r2; break;
            default: return 7;
            }
            if (t) npc = pc + b;
            wr = 0;
            break;
        }
        case 0x03: {
            a = r1 + ii;
            int sz = f3 & 3 ? (f3 & 3) * 2 : 1;
            if (f3 == 3 || f3 > 5 || (a & (sz - 1)) || !(m = at(a))) return 6;
            if (f3 == 0) v = SX(m[0], 8);
            else if (f3 == 4) v = m[0];
            else if (f3 == 1) v = SX(m[0] | m[1] << 8, 16);
            else if (f3 == 5) v = m[0] | m[1] << 8;
            else memcpy(&v, m, 4);
            break;
        }
        case 0x23: {
            a = r1 + SX((f7 << 5) | rd, 12);
            int sz = 1 << f3;
            if (f3 > 2 || (a & (sz - 1)) || a < DBASE || !(m = at(a))) return 6;
            memcpy(m, &r2, sz);
            wr = 0;
            break;
        }
        case 0x13:
            switch (f3) {
            case 0: v = r1 + ii; break;
            case 2: v = (int32_t) r1 < ii; break;
            case 3: v = r1 < (uint32_t) ii; break;
            case 4: v = r1 ^ ii; break;
            case 6: v = r1 | ii; break;
            case 7: v = r1 & ii; break;
            case 1: if (f7) return 7; v = r1 << (ii & 31); break;
            case 5:
                if (f7 == 0x20) v = (uint32_t) ((int32_t) r1 >> (ii & 31));
                else if (!f7) v = r1 >> (ii & 31);
                else return 7;
                break;
            }
            break;
        case 0x33:
            if (f7 == 1) return 5;              /* M extension */
            if (f7 && f7 != 0x20) return 7;
            switch (f3 | (f7 == 0x20) << 3) {
            case 0: v = r1 + r2; break;
            case 8: v = r1 - r2; break;
            case 1: v = r1 << (r2 & 31); break;
            case 2: v = (int32_t) r1 < (int32_t) r2; break;
            case 3: v = r1 < r2; break;
            case 4: v = r1 ^ r2; break;
            case 5: v = r1 >> (r2 & 31); break;
            case 13: v = (uint32_t) ((int32_t) r1 >> (r2 & 31)); break;
            case 6: v = r1 | r2; break;
            case 7: v = r1 & r2; break;
            default: return 7;
            }
            break;
        case 0x73:
            if (in != 0x00000073) return 7;
            wr = 0;
            if (x[17] == 10) { *retired = n; out[outn] = 0; return 0; }
            if (x[17] == 11) { if (outn < 255) out[outn++] = (char) x[10]; }
            else if (x[17] == 4) {
                for (a = x[10]; (m = at(a)) && *m; a++) if (outn < 255) out[outn++] = (char) *m;
            } else return 4;
            break;
        default: return 7;
        }
        if (wr && rd) x[rd] = v;
        pc = npc;
    }
}

/* apply printed solution on the cubie model; returns move count or -1 */
static int replay(cube_t c, const char *s, cube_t *end)
{
    int n = 0;
    while (*s) {
        if (*s == ' ' || *s == '\n') { s++; continue; }
        const char *fs = strchr("RBD", *s);
        if (!fs || !*s) return -1;
        int f = (int) (fs - "RBD"), k = 1;
        s++;
        if (*s == '2') { k = 2; s++; }
        else if (*s == '\'') { k = 3; s++; }
        if (*s && *s != ' ' && *s != '\n') return -1;
        while (k--) c = turn(c, f);
        n++;
    }
    *end = c;
    return n;
}

static int8_t dist[N];

static void bfs_full(void)
{
    uint32_t *q = malloc(N * sizeof *q), h = 0, t = 0;
    memset(dist, -1, sizeof dist);
    dist[0] = 0;
    q[t++] = 0;
    while (h < t) {
        uint32_t r = q[h++];
        cube_t c = unrank(r);
        for (int f = 0; f < 3; f++) {
            cube_t d = c;
            for (int k = 0; k < 3; k++) {
                d = turn(d, f);
                uint32_t s = rank(d);
                if (dist[s] < 0) { dist[s] = (int8_t) (dist[r] + 1); q[t++] = s; }
            }
        }
    }
    free(q);
    if (t != N) { fprintf(stderr, "BFS reached %u of %u states\n", t, N); exit(2); }
}

typedef struct { uint32_t rank; uint32_t retired; } rec_t;
static int by_ret(const void *a, const void *b)
{
    uint32_t x = ((const rec_t *) a)->retired, y = ((const rec_t *) b)->retired;
    return x < y ? 1 : x > y ? -1 : 0;
}
static void digits(uint32_t r, char s[15])
{
    cube_t c = unrank(r);
    for (int i = 0; i < 7; i++) { s[i] = '1' + c.p[i]; s[i + 7] = '1' + c.o[i]; }
    s[14] = 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s rubic.elf [workers]\n", argv[0]); return 2; }
    int workers = argc > 2 ? atoi(argv[2]) : (int) sysconf(_SC_NPROCESSORS_ONLN);
    load_elf(argv[1]);
    uint32_t a_in = sym("input_state"), a_pm = sym("perm_move"), a_om = sym("orient_move"),
             a_pd = sym("perm_dist"), a_od = sym("orient_dist");
    int fail = 0;

    clock_t t0 = clock();
    bfs_full();
    int hist[16] = {0}, diam = 0;
    for (uint32_t r = 0; r < N; r++) { hist[dist[r]]++; if (dist[r] > diam) diam = dist[r]; }
    printf("truth: BFS on cubie model, %u states, diameter %d, %d at distance 11 (%.1f s)\n",
           N, diam, hist[11], (double) (clock() - t0) / CLOCKS_PER_SEC);

    /* ---- H2: tables inside rubic.elf, against the cubie model ---- */
    const uint8_t *PD = at(a_pd), *OD = at(a_od);
    uint16_t PM[3][NP], OM[3][NO];
    memcpy(PM, at(a_pm), sizeof PM);
    memcpy(OM, at(a_om), sizeof OM);
    unsigned bad = 0;
    cube_t id;
    for (int i = 0; i < 7; i++) { id.p[i] = i; id.o[i] = 0; }
    /* move tables: every entry in range and equal to the cubie model */
    for (uint32_t r = 0; r < NP; r++) {
        cube_t c = unrank(r * NO);
        for (int f = 0; f < 3; f++) bad += PM[f][r] != rank_p(turn(c, f).p);
    }
    for (uint32_t r = 0; r < NO; r++) {
        cube_t c = unrank(r);
        for (int f = 0; f < 3; f++) bad += OM[f][r] != rank_o(turn(c, f).o);
    }
    /* distance tables: equal to exact BFS on each projection */
    static int8_t pd[NP], od[NO];
    memset(pd, 127, sizeof pd);
    memset(od, 127, sizeof od);
    for (uint32_t r = 0; r < N; r++) {
        if (dist[r] < pd[r / NO]) pd[r / NO] = dist[r];
        if (dist[r] < od[r % NO]) od[r % NO] = dist[r];
    }
    int mp = 0, mo = 0;
    for (uint32_t i = 0; i < NP; i++) { bad += PD[i] != pd[i]; if (PD[i] > mp) mp = PD[i]; }
    for (uint32_t i = 0; i < NO; i++) { bad += OD[i] != od[i]; if (OD[i] > mo) mo = OD[i]; }
    int h2 = !bad && !PD[0] && !OD[0] && mp == 7 && mo == 6;
    printf("H2: tables read from %s; %u mismatches vs cubie model; perm_dist max=%d solved=%d, "
           "orient_dist max=%d solved=%d  -> %s\n",
           argv[1], bad, mp, PD[0], mo, OD[0], h2 ? "PASS" : "FAIL");
    fail |= !h2;

    /* ---- H1: h(s) = max(perm_dist, orient_dist) <= d(s) for all states ---- */
    unsigned h1bad = 0, tight = 0;
    for (uint32_t r = 0; r < N; r++) {
        int h = PD[r / NO] > OD[r % NO] ? PD[r / NO] : OD[r % NO];
        if (h > dist[r]) h1bad++;
        tight += h == dist[r];
    }
    printf("H1: %u of %u states with h > d (h == d on %.1f%%)  -> %s\n", h1bad, N,
           100.0 * tight / N, h1bad ? "FAIL" : "PASS");
    fail |= h1bad != 0;

    if (workers == 0) return fail;     /* H1/H2 only */

    /* ---- H3: run rubic.elf on every state, split across workers ---- */
    rec_t *recs = mmap(NULL, N * sizeof *recs, PROT_READ | PROT_WRITE,
                       MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    uint32_t *wbad = mmap(NULL, 64 * sizeof *wbad, PROT_READ | PROT_WRITE,
                          MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    struct timespec w0, w1;
    clock_gettime(CLOCK_MONOTONIC, &w0);
    for (int w = 0; w < workers; w++) {
        if (fork()) continue;
        uint32_t nb = 0;
        for (uint32_t r = w; r < N; r += workers) {
            char s[15];
            digits(r, s);
            memcpy(data, data0, data_hi);
            memcpy(at(a_in), s, 14);
            uint64_t ret = 0;
            int rc = run(&ret, 200000000ull);
            cube_t end;
            int len = rc ? -1 : replay(unrank(r), out, &end);
            int ok = len == dist[r] && !memcmp(&end, &id, sizeof id);
            if (!ok) {
                if (nb < 5) printf("  H3 FAIL %s: rc=%d len=%d exact=%d out=\"%s\"\n", s, rc, len, dist[r], out);
                nb++;
            }
            recs[r].rank = r;
            recs[r].retired = (uint32_t) ret;
            if (w == 0 && (r & 0xffff) == 0)
                fprintf(stderr, "\r  H3 %5.1f%%", 100.0 * r / N);
        }
        wbad[w] = nb;
        _exit(0);
    }
    while (wait(NULL) > 0) {}
    clock_gettime(CLOCK_MONOTONIC, &w1);
    fprintf(stderr, "\r  H3 100.0%%\n");
    unsigned h3bad = 0;
    for (int w = 0; w < workers; w++) h3bad += wbad[w];
    double total = 0;
    for (uint32_t r = 0; r < N; r++) total += recs[r].retired;
    printf("H3: %u states executed on rubic.elf, %u wrong, %.0f s wall (%d workers), "
           "mean %.0f retired  -> %s\n",
           N, h3bad, (w1.tv_sec - w0.tv_sec) + (w1.tv_nsec - w0.tv_nsec) / 1e9, workers,
           total / N, h3bad ? "FAIL" : "PASS");
    fail |= h3bad != 0;

    /* ---- distance-11 states, hardest first ---- */
    rec_t *h11 = malloc(hist[11] * sizeof *h11);
    int k = 0;
    for (uint32_t r = 0; r < N; r++) if (dist[r] == 11) h11[k++] = recs[r];
    qsort(h11, k, sizeof *h11, by_ret);
    FILE *f = fopen("d11_asm.txt", "w");
    for (int i = 0; i < k; i++) {
        char s[15];
        digits(h11[i].rank, s);
        fprintf(f, "%s %u\n", s, h11[i].retired);
    }
    fclose(f);
    char a[15], b[15], v[15] = "21345671111111";
    digits(h11[0].rank, a);
    digits(h11[k - 1].rank, b);
    cube_t vc;
    for (int i = 0; i < 7; i++) { vc.p[i] = v[i] - '1'; vc.o[i] = v[i + 7] - '1'; }
    printf("distance 11: worst %s %u retired, best %s %u retired; 21345671111111: %u "
           "-> d11_asm.txt\n",
           a, h11[0].retired, b, h11[k - 1].retired, recs[rank(vc)].retired);
    return fail;
}
