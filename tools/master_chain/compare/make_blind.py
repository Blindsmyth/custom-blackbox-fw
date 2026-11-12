"""make_blind.py: cut one identical loud excerpt from every render, gain-match to a common
integrated loudness (plain gain, no limiting, 32-bit float) and write them under hidden names.
usage: python3 -I make_blind.py OUTDIR TARGET_LUFS SECONDS PREMASTER.wav RENDER.wav ...
The name -> chain key goes to OUTDIR/KEY.txt; don't open it until you have scored the files."""
import random, re, subprocess, sys
out, target, secs, pre, *renders = sys.argv[1], float(sys.argv[2]), float(sys.argv[3]), sys.argv[4], *sys.argv[5:]
def run(args):
    return subprocess.run(["ffmpeg", "-hide_banner", "-nostats", *args], capture_output=True, text=True).stderr
S = []
for m in re.finditer(r"t:\s*([\d.]+)\s.*? S:\s*(-?[\d.]+|-inf)", run(["-i", pre, "-af", "ebur128", "-f", "null", "-"])):
    S.append(float(m.group(2)) if m.group(2) != "-inf" else -70.0)
n = int(secs * 10)
best = max(range(0, len(S) - n, 50), key=lambda i: sum(S[i:i + n]))
start = round(best / 10.0, 1)
print(f"excerpt: {start}s .. {start + secs}s (loudest {secs:.0f}s window of the premaster)")
names = [f"X{i + 1}" for i in range(len(renders))]
random.SystemRandom().shuffle(names)
import os
os.makedirs(out, exist_ok=True)
key = []
for name, src in zip(names, renders):
    tmp = f"{out}/_{name}.wav"
    run(["-y", "-ss", str(start), "-t", str(secs), "-i", src, "-c:a", "pcm_f32le", tmp])
    log = run(["-i", tmp, "-af", "ebur128=peak=true", "-f", "null", "-"])
    I = float(re.search(r"Integrated loudness:\s+I:\s+(-?[\d.]+)", log).group(1))
    gain = target - I
    run(["-y", "-i", tmp, "-af", f"volume={gain}dB", "-c:a", "pcm_f32le", f"{out}/{name}.wav"])
    os.remove(tmp)
    key.append(f"{name}  {os.path.basename(src)}  (excerpt {I:.1f} LUFS, gain {gain:+.1f} dB)")
open(f"{out}/KEY.txt", "w").write("\n".join(sorted(key)) + "\n")
print("wrote", ", ".join(sorted(names)), "+ KEY.txt")
