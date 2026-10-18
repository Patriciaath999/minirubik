#!/usr/bin/env python3
"""Recover the corner geometry behind solver.c's source/twist tables by brute
force, then emit the facelet tables the RV32I renderer needs.

Model: corners at {-1,1}^3, x = R(+)/L(-), y = U(+)/D(-), z = F(+)/B(-).
ULF (-1,+1,+1) is the fixed corner. A sticker is (corner position, face normal).
"""
from itertools import permutations, product
import gen_tables as g

FACE_N = {"R": (1, 0, 0), "L": (-1, 0, 0), "U": (0, 1, 0), "D": (0, -1, 0),
          "F": (0, 0, 1), "B": (0, 0, -1)}


def rot(v, axis, sign):
    """Quarter turn about +axis (right-handed), sign=+1 ccw, -1 cw."""
    x, y, z = v
    if axis == 0:
        return (x, -sign * z, sign * y)
    if axis == 1:
        return (sign * z, y, -sign * x)
    return (-sign * y, sign * x, z)


def face_turn(normal, cw):
    axis = [i for i in range(3) if normal[i]][0]
    s = normal[axis]                      # outward direction
    sign = -s if cw else s                # clockwise seen from outside
    return lambda v: rot(v, axis, sign)


FIXED = (-1, 1, 1)
CORNERS = [c for c in product((-1, 1), repeat=3) if c != FIXED]
FACES = ["R", "B", "D"]


def normals(c):
    return [n for n in FACE_N.values() if any(n[i] and n[i] == c[i] for i in range(3))]


def cyc_order(c, ref_axis, cw):
    """Three facelet normals of corner c, starting at the ref-axis facelet,
    then going clockwise (cw) or counter-clockwise seen from outside."""
    ns = normals(c)
    ref = [n for n in ns if n[ref_axis]][0]
    rest = [n for n in ns if n != ref]
    a, b = rest
    # ref -> a is counter-clockwise seen from outside iff (ref x a) . c > 0
    cr = (ref[1] * a[2] - ref[2] * a[1], ref[2] * a[0] - ref[0] * a[2],
          ref[0] * a[1] - ref[1] * a[0])
    ccw = cr[0] * c[0] + cr[1] * c[1] + cr[2] * c[2] > 0
    if ccw != (not cw):
        a, b = b, a
    return [ref, a, b]


found = []
for perm in permutations(CORNERS):          # perm[i] = position vector of index i
    pos = list(perm)
    for dirs in product((True, False), repeat=3):
        ok = True
        for f, name in enumerate(FACES):
            T = face_turn(FACE_N[name], dirs[f])
            n = FACE_N[name]
            ax = [k for k in range(3) if n[k]][0]
            for i in range(7):
                s = g.SOURCE[f][i]
                want = T(pos[s]) if pos[s][ax] == n[ax] else pos[s]
                if want != pos[i]:
                    ok = False
                    break
            if not ok:
                break
        if ok:
            found.append((pos, dirs))
assert len(found) >= 1, "no geometric match"
pos, dirs = found[0]
print("matches:", len(found), "dirs (cw?) R,B,D =", dirs, file=__import__("sys").stderr)
for i, p in enumerate(pos):
    print(f"  index {i}: {p}", file=__import__("sys").stderr)

# orientation convention: o = slot of the cubie's reference sticker
conv = None
for ref_axis in range(3):
    for cw in (True, False):
        for sign in (1, -1):
            good = True
            for f, name in enumerate(FACES):
                T = face_turn(FACE_N[name], dirs[f])
                for i in range(7):
                    s = g.SOURCE[f][i]
                    if pos[s] == pos[i]:
                        if g.TWIST[f][i] != 0:
                            good = False
                        continue
                    os_, oi = cyc_order(pos[s], ref_axis, cw), cyc_order(pos[i], ref_axis, cw)
                    # sticker in slot 0 of src goes to slot k of dst
                    k = oi.index(T(os_[0]))
                    if (sign * k) % 3 != g.TWIST[f][i]:
                        good = False
                if not good:
                    break
            if good:
                conv = (ref_axis, cw, sign)
print("orientation convention (ref_axis, cw, sign):", conv, file=__import__("sys").stderr)
assert conv

# ---------------------------------------------------------------- renderer data
REF_AXIS, CW, _ = conv
POS8 = pos + [FIXED]                                   # index 7 = fixed ULF
SLOTS = [cyc_order(p, REF_AXIS, CW) for p in POS8]     # slot k -> face normal
NORMAL_FACE = {v: k for k, v in FACE_N.items()}
COLOR = {"U": 0xFFFFFF, "D": 0xFFD500, "F": 0x009B48, "B": 0x0046AD,
         "R": 0xB71234, "L": 0xFF5800}
FACE_SLOT = {"U": (1, 0), "L": (0, 1), "F": (1, 1), "R": (2, 1), "B": (3, 1), "D": (1, 2)}
WIDTH = 35


def facelet_xy(p, n):
    """Top-left pixel of the 4x3 block for sticker (corner p, normal n)."""
    x, y, z = p
    f = NORMAL_FACE[n]
    col, row = {
        "F": (x > 0, y < 0), "R": (z < 0, y < 0), "B": (x < 0, y < 0),
        "L": (z > 0, y < 0), "U": (x > 0, z > 0), "D": (x > 0, z < 0),
    }[f]
    sc, sr = FACE_SLOT[f]
    return sc * 9 + int(col) * 4, sr * 7 + int(row) * 3


def slot_offsets():
    return [[(lambda xy: (xy[1] * WIDTH + xy[0]) * 4)(facelet_xy(POS8[i], SLOTS[i][k]))
             for k in range(3)] for i in range(8)]


def cubie_colors():
    return [[COLOR[NORMAL_FACE[SLOTS[c][j]]] for j in range(3)] for c in range(8)]


def render_cube(p7, o7):
    """Reference renderer from the cubie state (same rule as the assembly)."""
    img = [[0] * WIDTH for _ in range(20)]
    p = list(p7) + [7]
    o = list(o7) + [0]
    for i in range(8):
        for k in range(3):
            col = COLOR[NORMAL_FACE[SLOTS[p[i]][(k - o[i]) % 3]]]
            x0, y0 = facelet_xy(POS8[i], SLOTS[i][k])
            for dy in range(3):
                for dx in range(4):
                    img[y0 + dy][x0 + dx] = col
    return img


def stickers_from_state(p7, o7):
    p = list(p7) + [7]
    o = list(o7) + [0]
    st = {}
    for i in range(8):
        for k in range(3):
            st[(POS8[i], SLOTS[i][k])] = NORMAL_FACE[SLOTS[p[i]][(k - o[i]) % 3]]
    return st


def sticker_turn(st, face, quarters):
    """Independent move model: rotate the physical stickers of one layer."""
    n = FACE_N[face]
    ax = [k for k in range(3) if n[k]][0]
    T = face_turn(n, True)
    for _ in range(quarters):
        new = {}
        for (c, m), col in st.items():
            if c[ax] == n[ax]:
                new[(T(c), T(m))] = col
            else:
                new[(c, m)] = col
        st = new
    return st


def render_stickers(st):
    img = [[0] * WIDTH for _ in range(20)]
    for (c, m), f in st.items():
        x0, y0 = facelet_xy(c, m)
        for dy in range(3):
            for dx in range(4):
                img[y0 + dy][x0 + dx] = COLOR[f]
    return img


# self-checks: solved net has 6 uniform faces; cubie renderer == sticker model
# after every single move from a few random states
_solved = render_cube(range(7), [0] * 7)
for f, (sc, sr) in FACE_SLOT.items():
    blk = {_solved[sr * 7 + y][sc * 9 + x] for y in range(6) for x in range(8)
           if x % 4 != 3 or True}
    assert blk == {COLOR[f]}, f
import random as _r
_r.seed(7)
for _ in range(200):
    pr = list(g.perms[_r.randrange(5040)])
    orr = g.unrank_o(_r.randrange(729))
    st = stickers_from_state(pr, orr)
    f = _r.randrange(3)
    q = _r.randrange(1, 4)
    p2, o2 = pr, orr
    for _ in range(q):
        p2 = [p2[g.SOURCE[f][i]] for i in range(7)]
        o2 = [(o2[g.SOURCE[f][i]] + g.TWIST[f][i]) % 3 for i in range(7)]
    assert render_cube(p2, o2) == render_stickers(sticker_turn(st, FACES[f], q))


def render_tables():
    out = ["slot_off:"]
    for row in slot_offsets():
        out.append("    .half " + ",".join(map(str, row + [0])))
    out.append("    .align 2")
    out.append("cubie_col:")
    for row in cubie_colors():
        out.append("    .word " + ",".join(f"0x{c:06X}" for c in row + [0]))
    out.append("src_tab:")
    for r in g.SOURCE:
        out.append("    .byte " + ",".join(map(str, r + [7])))
    out.append("tw_tab:")
    for r in g.TWIST:
        out.append("    .byte " + ",".join(map(str, r + [0])))
    return "\n".join(out)
