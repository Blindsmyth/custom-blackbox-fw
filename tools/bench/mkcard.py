"""Build a small FAT32 card image with a preset tree, using dosfstools + mtools (no mounting).

Default tree:
  Presets/Alpha/preset.xml
  Presets/Beta/preset.xml, Beta/kick.wav
  Presets/Zeta Kit/preset.xml
  Presets/_Test/One/preset.xml
  Presets/_Test/Two/preset.xml
  Presets/_Test/Sub/Deep/preset.xml
"""
import os
import subprocess
import tempfile

PRESET_XML = ('<?xml version="1.0" encoding="UTF-8"?>\n'
              '<document>\n<session version="2">\n</session>\n</document>\n')

DEFAULT_TREE = {
    "Presets/Alpha/preset.xml": PRESET_XML,
    "Presets/Beta/preset.xml": PRESET_XML,
    "Presets/Beta/kick.wav": "RIFF\x24\0\0\0WAVEfmt ",
    "Presets/Zeta Kit/preset.xml": PRESET_XML,
    "Presets/_Test/One/preset.xml": PRESET_XML,
    "Presets/_Test/Two/preset.xml": PRESET_XML,
    "Presets/_Test/Sub/Deep/preset.xml": PRESET_XML,
}


def run(*cmd):
    subprocess.run(cmd, check=True, capture_output=True)


def build(path, tree=None, size_mb=64):
    tree = DEFAULT_TREE if tree is None else tree
    if os.path.exists(path):
        os.remove(path)
    run("mkfs.fat", "-F", "32", "-s", "1", "-n", "BENCH", "-C", path, str(size_mb * 1024))
    made = set()
    with tempfile.TemporaryDirectory() as tmp:
        for rel, content in sorted(tree.items()):
            parts = rel.split("/")
            for i in range(1, len(parts)):
                d = "/".join(parts[:i])
                if d not in made:
                    run("mmd", "-i", path, "::/" + d)
                    made.add(d)
            src = os.path.join(tmp, "f")
            with open(src, "wb") as f:
                f.write(content.encode("latin1"))
            run("mcopy", "-i", path, src, "::/" + rel)
    return path


def fsck(path):
    r = subprocess.run(["fsck.fat", "-n", path], capture_output=True, text=True)
    return r.returncode == 0, (r.stdout + r.stderr).strip()


def exists(path, rel):
    r = subprocess.run(["mdir", "-i", path, "-b", "::/" + rel.replace("\\", "/")],
                       capture_output=True, text=True)
    return r.returncode == 0


if __name__ == "__main__":
    import sys
    out = build(sys.argv[1] if len(sys.argv) > 1 else "/tmp/bench-card.img")
    print(out, fsck(out))
