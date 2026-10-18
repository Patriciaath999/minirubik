#!/usr/bin/env python3
"""Check RENDER build frames against the independent sticker model.
usage: check_led.py elf state"""
import subprocess, sys, os
import geometry as G
elf, state = sys.argv[1], sys.argv[2]
nm = subprocess.run(["llvm-nm", elf], capture_output=True, text=True).stdout
sym = {l.split()[2]: l.split()[0] for l in nm.splitlines() if len(l.split()) == 3}
env = dict(os.environ, LED_HOOK=sym["draw_done"])
out = subprocess.run(["./iss_led", elf, state, sym["input_state"]], env=env,
                     capture_output=True, text=True).stdout.strip()
frames = [[int(v, 16) for v in l.split()] for l in open("led_frames.txt")]
p = [int(c) - 1 for c in state[:7]]
o = [int(c) - 1 for c in state[7:]]
st = G.stickers_from_state(p, o)
moves = out.split()
assert len(frames) == len(moves) + 1, (len(frames), len(moves))
def flat(img): return [v for row in img for v in row]
bad = 0
for k, fr in enumerate(frames):
    if k:
        mv = moves[k - 1]
        q = 1 if len(mv) == 1 else (2 if mv[1] == "2" else 3)
        st = G.sticker_turn(st, mv[0], q)
    if fr != flat(G.render_stickers(st)):
        bad += 1
solved = flat(G.render_stickers(G.stickers_from_state(range(7), [0] * 7)))
print(f"{state}: {len(moves)} moves, {len(frames)} frames, mismatching frames {bad}, "
      f"final frame solved: {frames[-1] == solved}")
sys.exit(bad != 0 or frames[-1] != solved)
