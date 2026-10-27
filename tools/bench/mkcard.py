"""Build a small FAT32 card image with a preset tree, using dosfstools + mtools (no mounting).
build_exfat makes an exFAT image instead (macOS: hdiutil + newfs_exfat, mounted only to fill it
and to read it back). The other helpers work on either.

Default tree:
  Presets/Alpha/preset.xml
  Presets/Beta/preset.xml, Beta/kick.wav
  Presets/Zeta Kit/preset.xml
  Presets/_Test/One/preset.xml
  Presets/_Test/Two/preset.xml
  Presets/_Test/Sub/Deep/preset.xml
"""
import atexit
import contextlib
import os
import re
import shutil
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


_TEMPLATES = {}


def build_cached(path, tree=None, size_mb=64):
    """build(), but each (tree, size) is made once per run and copied after that: mkfs and mtools are most
    of what a bench case costs to set up."""
    tree = DEFAULT_TREE if tree is None else tree
    key = (size_mb, tuple(sorted(tree.items())))
    if key not in _TEMPLATES:
        tmp = tempfile.mkdtemp(prefix="bench-card-")
        atexit.register(shutil.rmtree, tmp, True)
        _TEMPLATES[key] = build(os.path.join(tmp, "card.img"), tree, size_mb)
    shutil.copyfile(_TEMPLATES[key], path)
    return path


# ---- exFAT (the format SDXC cards ship with; FatFs in 3.1.9 has FF_FS_EXFAT = 1)
def exfat_available():
    return all(shutil.which(t) for t in ("hdiutil", "newfs_exfat", "fsck_exfat"))


def is_exfat(path):
    with open(path, "rb") as f:
        return f.read(11)[3:] == b"EXFAT   "


def _attach(path, *opts):
    r = subprocess.run(["hdiutil", "attach", "-imagekey", "diskimage-class=CRawDiskImage", *opts, path],
                       check=True, capture_output=True, text=True)
    return re.search(r"/dev/disk\d+", r.stdout).group(0)


def _detach(dev):
    subprocess.run(["hdiutil", "detach", dev], check=True, capture_output=True)


@contextlib.contextmanager
def _mounted(path, readonly=True):
    with tempfile.TemporaryDirectory() as mnt:
        dev = _attach(path, "-nobrowse", "-mountpoint", mnt, *(["-readonly"] if readonly else []))
        try:
            yield mnt
        finally:
            _detach(dev)


def build_exfat(path, tree=None, size_mb=256, cluster_kb=32):
    """An unpartitioned exFAT image with cluster_kb clusters (SD cards use 32-128 KB)."""
    tree = DEFAULT_TREE if tree is None else tree
    with open(path, "wb") as f:
        f.truncate(size_mb << 20)
    dev = _attach(path, "-nomount")
    try:
        run("newfs_exfat", "-c", str(cluster_kb * 2), "-v", "BENCH", dev)
    finally:
        _detach(dev)
    with _mounted(path, readonly=False) as mnt:
        for rel, content in sorted(tree.items()):
            dest = os.path.join(mnt, *rel.split("/"))
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            with open(dest, "wb") as f:
                f.write(content.encode("latin1"))
    return path


def fsck(path):
    if is_exfat(path):
        dev = _attach(path, "-nomount")
        try:
            r = subprocess.run(["fsck_exfat", "-n", dev], capture_output=True, text=True)
        finally:
            _detach(dev)
        return r.returncode == 0, (r.stdout + r.stderr).strip()
    r = subprocess.run(["fsck.fat", "-n", path], capture_output=True, text=True)
    return r.returncode == 0, (r.stdout + r.stderr).strip()


def exists(path, rel):
    if is_exfat(path):
        return rel.replace("\\", "/") in listing(path)
    r = subprocess.run(["mdir", "-i", path, "-b", "::/" + rel.replace("\\", "/")],
                       capture_output=True, text=True)
    return r.returncode == 0


def listing(path):
    """Every file and directory on the image, as paths like 'Presets/Alpha/preset.xml'."""
    if is_exfat(path):
        out = set()
        with _mounted(path) as mnt:
            for d, dirs, files in os.walk(mnt):
                dirs[:] = [n for n in dirs if not n.startswith(".")]
                for n in dirs + [n for n in files if not n.startswith(".")]:
                    out.add(os.path.relpath(os.path.join(d, n), mnt).replace(os.sep, "/"))
        return out
    r = subprocess.run(["mdir", "-i", path, "-/", "-b", "::/"], capture_output=True, text=True, check=True)
    out = set()
    for line in r.stdout.splitlines():
        line = line.strip()
        if line.startswith("::/"):
            line = line[3:]
        line = line.rstrip("/")
        if line and not line.endswith("/.") and not line.endswith("/..") and line not in (".", ".."):
            out.add(line)
    return out


def read(path, rel):
    if is_exfat(path):
        with _mounted(path) as mnt:
            with open(os.path.join(mnt, *rel.split("/")), "rb") as f:
                return f.read()
    r = subprocess.run(["mtype", "-i", path, "::/" + rel], capture_output=True, check=True)
    return r.stdout


if __name__ == "__main__":
    import sys
    out = build(sys.argv[1] if len(sys.argv) > 1 else "/tmp/bench-card.img")
    print(out, fsck(out))
