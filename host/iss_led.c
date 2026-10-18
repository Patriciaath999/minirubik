/* Minimal RV32I ISS: loads ELF32 PT_LOAD segments, Ripes ecalls
 * (a7=1 int, 4 string, 10 exit, 11 char). Any non-RV32I opcode aborts.
 * usage: iss prog.elf [state14 input_addr_hex]  -> prints output, then
 *        "#retired N" on stderr. */
#include <elf.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MEMSZ (1u << 20)
static uint8_t text[MEMSZ], data[MEMSZ];
static uint8_t *addr(uint32_t a)
{
    if (a < MEMSZ) return &text[a];
    if (a >= 0x10000000u && a - 0x10000000u < MEMSZ) return &data[a - 0x10000000u];
    fprintf(stderr, "bad address %08x\n", a);
    exit(3);
}
static uint32_t ld(uint32_t a, int n)
{
    if (a & (n - 1)) { fprintf(stderr, "misaligned %d-byte load %08x\n", n, a); exit(3); }
    uint32_t v = 0; uint8_t *p = addr(a);
    for (int i = 0; i < n; i++) v |= (uint32_t) p[i] << (8 * i);
    return v;
}
static void st(uint32_t a, uint32_t v, int n)
{
    if (a & (n - 1)) { fprintf(stderr, "misaligned %d-byte store %08x\n", n, a); exit(3); }
    uint8_t *p = addr(a);
    for (int i = 0; i < n; i++) p[i] = v >> (8 * i);
}
#define SX(v, b) ((int32_t) ((v) << (32 - (b))) >> (32 - (b)))

int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb");
    static uint8_t buf[1 << 22];
    fread(buf, 1, sizeof buf, f);
    Elf32_Ehdr *eh = (Elf32_Ehdr *) buf;
    Elf32_Phdr *ph = (Elf32_Phdr *) (buf + eh->e_phoff);
    for (int i = 0; i < eh->e_phnum; i++)
        if (ph[i].p_type == PT_LOAD)
            memcpy(addr(ph[i].p_vaddr), buf + ph[i].p_offset, ph[i].p_filesz);
    if (argc > 3) memcpy(addr(strtoul(argv[3], 0, 16)), argv[2], 14);

    uint32_t x[32] = {0}, pc = eh->e_entry;
    uint64_t n = 0;
    char *hk = getenv("LED_HOOK");
    uint32_t hook = hk ? strtoul(hk, 0, 16) : 1;
    FILE *dump = hk ? fopen("led_frames.txt", "w") : NULL;
    for (;;) {
        if (pc == hook) {
            for (uint32_t i = 0; i < 35 * 20; i++) fprintf(dump, "%x ", ld(0x10080000u + 4 * i, 4));
            fputc('\n', dump);
        }
        uint32_t in = ld(pc, 4), op = in & 0x7f, rd = (in >> 7) & 31,
                 f3 = (in >> 12) & 7, r1 = x[(in >> 15) & 31], r2 = x[(in >> 20) & 31],
                 f7 = in >> 25, npc = pc + 4, v = 0;
        int32_t ii = SX(in >> 20, 12);
        int wr = 1;
        n++;
        switch (op) {
        case 0x37: v = in & 0xfffff000; break;
        case 0x17: v = pc + (in & 0xfffff000); break;
        case 0x6f: v = npc; npc = pc + SX(((in >> 31) << 20) | (((in >> 12) & 0xff) << 12) |
                                 (((in >> 20) & 1) << 11) | (((in >> 21) & 0x3ff) << 1), 21); break;
        case 0x67: v = npc; npc = (r1 + ii) & ~1u; break;
        case 0x63: {
            int32_t b = SX(((in >> 31) << 12) | (((in >> 7) & 1) << 11) |
                           (((in >> 25) & 0x3f) << 5) | (((in >> 8) & 0xf) << 1), 13);
            int t;
            switch (f3) {
            case 0: t = r1 == r2; break; case 1: t = r1 != r2; break;
            case 4: t = (int32_t) r1 < (int32_t) r2; break;
            case 5: t = (int32_t) r1 >= (int32_t) r2; break;
            case 6: t = r1 < r2; break; case 7: t = r1 >= r2; break;
            default: goto illegal;
            }
            if (t) npc = pc + b;
            wr = 0; break;
        }
        case 0x03:
            switch (f3) {
            case 0: v = SX(ld(r1 + ii, 1), 8); break; case 1: v = SX(ld(r1 + ii, 2), 16); break;
            case 2: v = ld(r1 + ii, 4); break; case 4: v = ld(r1 + ii, 1); break;
            case 5: v = ld(r1 + ii, 2); break; default: goto illegal;
            } break;
        case 0x23: {
            int32_t s = SX((f7 << 5) | rd, 12);
            if (f3 > 2) goto illegal;
            st(r1 + s, r2, 1 << f3); wr = 0; break;
        }
        case 0x13:
            switch (f3) {
            case 0: v = r1 + ii; break; case 2: v = (int32_t) r1 < ii; break;
            case 3: v = r1 < (uint32_t) ii; break; case 4: v = r1 ^ ii; break;
            case 6: v = r1 | ii; break; case 7: v = r1 & ii; break;
            case 1: if (f7) goto illegal; v = r1 << (ii & 31); break;
            case 5: if (f7 == 0x20) v = (int32_t) r1 >> (ii & 31);
                    else if (!f7) v = r1 >> (ii & 31); else goto illegal; break;
            } break;
        case 0x33:
            if (f7 == 1) { fprintf(stderr, "M-extension instruction at %08x\n", pc); return 4; }
            switch (f3 | (f7 == 0x20) << 3) {
            case 0: v = r1 + r2; break; case 8: v = r1 - r2; break;
            case 1: v = r1 << (r2 & 31); break; case 2: v = (int32_t) r1 < (int32_t) r2; break;
            case 3: v = r1 < r2; break; case 4: v = r1 ^ r2; break;
            case 5: v = r1 >> (r2 & 31); break; case 13: v = (int32_t) r1 >> (r2 & 31); break;
            case 6: v = r1 | r2; break; case 7: v = r1 & r2; break;
            default: goto illegal;
            } break;
        case 0x73:
            wr = 0;
            switch (x[17]) {
            case 1: printf("%d", (int32_t) x[10]); break;
            case 4: for (uint32_t a = x[10]; *addr(a); a++) putchar(*addr(a)); break;
            case 11: putchar(x[10]); break;
            case 10: if (dump) fclose(dump); fprintf(stderr, "#retired %llu\n", (unsigned long long) n); return 0;
            default: fprintf(stderr, "bad ecall %u\n", x[17]); return 5;
            } break;
        default:
        illegal:
            fprintf(stderr, "illegal instruction %08x at %08x\n", in, pc);
            return 4;
        }
        if (wr && rd) x[rd] = v;
        pc = npc;
    }
}
