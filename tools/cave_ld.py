"""Link cave asm and C at 0x080F1E80 and return the loadable blob plus symbols."""

import struct
import subprocess
from pathlib import Path

ASM_FLAGS = [
    "-target", "armv7em-none-eabi", "-mthumb",
    "-mfloat-abi=hard", "-mfpu=fpv5-sp-d16",
]
C_FLAGS = ASM_FLAGS + [
    "-ffreestanding", "-fno-builtin", "-fno-exceptions",
    "-fno-unwind-tables", "-fno-asynchronous-unwind-tables",
    "-Os",
]


def link_cave(sources, elf_path, va, includes=()):
    elf_path = Path(elf_path)
    objs = []
    for src in sources:
        src = Path(src)
        obj = elf_path.with_name(src.stem + ".o")
        flags = C_FLAGS if src.suffix == ".c" else ASM_FLAGS
        cmd = ["clang", *flags]
        for inc in includes:
            cmd.extend(["-I", str(inc)])
        cmd.extend(["-c", "-o", str(obj), str(src)])
        subprocess.check_call(cmd, cwd=str(src.parent))
        objs.append(obj)
    ld = Path(__file__).resolve().parents[1] / "firmware" / "patches" / "cave.ld"
    subprocess.check_call([
        "arm-none-eabi-ld", "-n", f"-T{ld}", "-o", str(elf_path), *map(str, objs),
    ])
    return load_elf(elf_path, va)


def load_elf(path, va):
    data = Path(path).read_bytes()
    if data[:4] != b"\x7fELF":
        raise SystemExit(f"{path} is not ELF")
    e_phoff = struct.unpack_from("<I", data, 28)[0]
    e_shoff = struct.unpack_from("<I", data, 32)[0]
    e_phentsize = struct.unpack_from("<H", data, 42)[0]
    e_phnum = struct.unpack_from("<H", data, 44)[0]
    e_shentsize = struct.unpack_from("<H", data, 46)[0]
    e_shnum = struct.unpack_from("<H", data, 48)[0]
    e_shstrndx = struct.unpack_from("<H", data, 50)[0]
    loads = []
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        p_type, p_offset, p_vaddr, _p_paddr, p_filesz, _p_memsz, _p_flags, _p_align = struct.unpack_from(
            "<IIIIIIII", data, off)
        if p_type == 1 and p_filesz:
            loads.append((p_vaddr, p_offset, p_filesz))
    if len(loads) != 1 or loads[0][0] != va:
        raise SystemExit(f"expected one load at {va:#x}, got {[(hex(a), hex(n)) for a, _, n in loads]}")
    p_vaddr, p_offset, p_filesz = loads[0]
    blob = data[p_offset:p_offset + p_filesz]
    if p_vaddr + p_filesz >= 0x08100000:
        raise SystemExit(f"cave crosses into bank 2 at {p_vaddr + p_filesz:#x}")

    def shdr(i):
        return struct.unpack_from("<IIIIIIIIII", data, e_shoff + i * e_shentsize)

    strtab = shdr(e_shstrndx)
    names = data[strtab[4]:strtab[4] + strtab[5]]
    sym = strs = None
    for i in range(e_shnum):
        s = shdr(i)
        name = names[s[0]:].split(b"\0", 1)[0].decode()
        if name == ".symtab":
            sym = s
        elif name == ".strtab":
            strs = s
    if not sym or not strs:
        raise SystemExit("linked elf has no symbol table")
    strbytes = data[strs[4]:strs[4] + strs[5]]
    resolved = {}
    off = sym[4]
    for _ in range(sym[5] // sym[9]):
        st_name, st_value, _st_size, st_info, _st_other, st_shndx = struct.unpack_from("<IIIBBH", data, off)
        off += sym[9]
        if st_shndx == 0 or st_name == 0:
            continue
        name = strbytes[st_name:].split(b"\0", 1)[0].decode()
        if not name or name.startswith("$") or name.startswith("."):
            continue
        if not (va <= (st_value & ~1) < va + len(blob)):
            continue
        resolved[name] = st_value & ~1
    return bytes(blob), resolved
