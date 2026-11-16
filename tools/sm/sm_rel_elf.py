"""Size Matters (PS2 USA, SCUS-97615): one ELF per level module for Ghidra. Read-only research aid.

A level's code is /LVL/LEVEL_nn.REL: an SNR2 module linked at address 0 (a pointer in the file is a file offset) whose
only relocations are its imports from the resident executable /SCUS_976.15, which is linked at 0x1e62500. The two do not
overlap, so this writes one executable ELF holding both: the module at 0 with its import sites resolved to the
resident's symbols, the resident's segment at its own address, and a symbol table with

  - the resident's SNR2 symbols (its own names),
  - the module's exports,
  - one name per class callback, taken from the module's class records (see `class_records`):
    <Class>__init / __cb1 / __update / __event / __cbN, <Class>__record, <Class>__props.

Nothing is executed and the disc is not modified. Inputs are the hig_oracle report (mods/sm_native/oracle-v3) and the
ISO it names.

    python tools/sm/sm_rel_elf.py 02 build/sm_pc_work/ghidra/LEVEL_02.elf [--classes build/sm_pc_work/classes_02.json]
"""
import argparse
import json
import re
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REPORT = ROOT / 'mods/sm_native/oracle-v3'
IDENT = re.compile(rb'[A-Za-z_][A-Za-z0-9_]{2,47}\Z')
CALLBACKS = ('init', 'cb1', 'update', 'event')          # record words 1..4 (names are working labels)


def disc_file(index, path):
    entry = next(e for e in index['disc_files'] if e['disc_path'] == path)
    with open(index['source_iso'], 'rb') as f:
        f.seek(entry['disc_lba'] * index['sector_size'])
        return f.read(entry['file_size'])


def cstring(image, p, limit):
    if p <= 0 or p >= len(image):
        return None
    e = image.find(b'\0', p, p + limit)
    if e <= p:
        return None
    return image[p:e]


def class_records(image, code_end=None):
    """The module's class records: {offset, name, callbacks[4], declaration, props_size, instance_size, extra[8]}.

    A record is sixteen words: a pointer to the class name, four callbacks (the first is never 0), a pointer to the
    property declaration text (C-like, `Type name; ...`) or 0, the property byte size (the BOG class definition's), the
    instance data size, and eight further words (callbacks, data pointers, flags). Records are found by shape; the BOG
    class hash is CRC32 of the upper-cased name.
    """
    n = len(image)
    words = struct.unpack_from(f'<{n // 4}I', image)
    if code_end is None:                                 # the first record's first callback bounds the code from above
        code_end = n
    out = []
    for i in range(len(words) - 16):
        w = words[i:i + 16]
        if not (0x100 <= w[0] < n) or not (0 < w[1] < code_end) or w[1] & 3:
            continue
        if any(x & 3 or x >= code_end for x in w[2:5]):
            continue
        name = cstring(image, w[0], 49)
        if not name or not IDENT.match(name):
            continue
        decl = b''
        if w[5]:
            decl = cstring(image, w[5], 8192)
            if not decl or b';' not in decl or w[6] == 0 or w[6] > 0x4000:
                continue
        elif w[6] > 0x4000 or w[6] & 3:                  # (WaterWaves: 16 property bytes and no declaration)
            continue
        if w[7] > 0x10000:
            continue
        # a callback is a function: it is the target of no branch into its middle; cheap check: the word before it is
        # a delay slot of `jr ra` / a nop / a branch, or it starts the file
        out.append(dict(offset=i * 4, name=name.decode(), hash=zlib.crc32(name.upper()) & 0xffffffff,
                        callbacks=list(w[1:5]), declaration=decl.decode('latin-1'), props_size=w[6],
                        instance_size=w[7], extra=list(w[8:16])))
    # keep the densest run: records live together in the module's data, after all code
    if out:
        first_code = min(r['callbacks'][0] for r in out)
        out = [r for r in out if r['offset'] > first_code]
    return out


def build(level, out_path, classes_path=None):
    index = json.loads((REPORT / 'index.json').read_text())
    name = f'/LVL/LEVEL_{level}.REL' if level != 'FRONTEND' else '/LVL/FRONTEND.REL'
    rel = bytearray(disc_file(index, name))
    resident = disc_file(index, '/SCUS_976.15')
    rel_report = json.loads((REPORT / f'{name.rsplit("/", 1)[1]}.json').read_text())
    res_report = json.loads((REPORT / 'resident.json').read_text())
    address = {s['name']: s['value'] for s in res_report['symbols'] if s['kind'] >= 2 and s['name']}
    missing = set()
    for ref in rel_report['references']:
        target = address.get(ref['symbol'])
        if target is None:
            missing.add(ref['symbol'])
            continue
        value = (target + ref['addend']) & 0xffffffff
        at = ref['file_offset']
        word, = struct.unpack_from('<I', rel, at)
        kind = ref['relocation']
        if kind == 4:                                    # R_MIPS_26
            word = (word & 0xfc000000) | ((value >> 2) & 0x3ffffff)
        elif kind == 5:                                  # R_MIPS_HI16
            word = (word & 0xffff0000) | (((value + 0x8000) >> 16) & 0xffff)
        elif kind == 6:                                  # R_MIPS_LO16
            word = (word & 0xffff0000) | (value & 0xffff)
        elif kind == 2:                                  # R_MIPS_32
            word = value
        else:
            raise SystemExit(f'relocation {kind} at {at:#x}')
        struct.pack_into('<I', rel, at, word)
    records = class_records(bytes(rel))
    if classes_path:
        Path(classes_path).parent.mkdir(parents=True, exist_ok=True)
        Path(classes_path).write_text(json.dumps(dict(module=name, records=records), indent=1))

    # resident: its one PT_LOAD
    phoff, = struct.unpack_from('<I', resident, 28)
    p_type, p_offset, p_vaddr, _, p_filesz, p_memsz, _, _ = struct.unpack_from('<8I', resident, phoff)
    assert p_type == 1
    res_bytes = resident[p_offset:p_offset + p_filesz]
    res_text = next(s for s in res_report['sections'] if s['name'] == '.text')
    text_end = res_text['address'] + res_text['size']

    symbols = []                                         # (name, value, size, info, shndx)
    FUNC, OBJECT = 0x12, 0x11                            # GLOBAL | FUNC / OBJECT
    for s in res_report['symbols']:
        if s['kind'] >= 2 and s['name']:
            symbols.append((s['name'], s['value'], 0, FUNC if p_vaddr <= s['value'] < text_end else OBJECT, 2))
    for s in rel_report['symbols']:
        if s['kind'] >= 2 and s['name']:
            symbols.append((s['name'], s['value'], 0, FUNC if s['name'].startswith('lib_main') else OBJECT, 1))
    for key in ('load_function', 'unload_function'):
        if rel_report.get(key):
            symbols.append((f'module_{key}', rel_report[key], 0, FUNC, 1))
    seen = {}
    for r in records:
        base = r['name']
        if base in seen:                                 # the same name twice: keep both apart by offset
            base = f'{base}_{r["offset"]:x}'
        seen[r['name']] = 1
        symbols.append((f'{base}__record', r['offset'], 64, OBJECT, 1))
        for label, cb in zip(CALLBACKS, r['callbacks']):
            if cb:
                symbols.append((f'{base}__{label}', cb, 0, FUNC, 1))
        for k, cb in enumerate(r['extra'][:5]):
            if cb and not cb & 3 and cb < r['offset'] and cb > 0x1000:
                symbols.append((f'{base}__cb{5 + k}', cb, 0, FUNC, 1))
    # a function has one name: the first given wins
    named, unique = set(), []
    for s in symbols:
        if s[3] == FUNC:
            if s[1] in named:
                continue
            named.add(s[1])
        unique.append(s)
    symbols = unique

    strtab = bytearray(b'\0')
    symtab = bytearray(16)
    for sname, value, size, info, shndx in symbols:
        symtab += struct.pack('<IIIBBH', len(strtab), value, size, info, 0, shndx)
        strtab += sname.encode() + b'\0'
    shstr = b'\0.rel\0.resident\0.resbss\0.symtab\0.strtab\0.shstrtab\0'
    off = lambda s: shstr.index(b'\0' + s + b'\0') + 1

    ehsize, phsize, shsize = 52, 32, 40
    phnum = 2
    data_at = (ehsize + phsize * phnum + 0xfff) & ~0xfff
    rel_at = data_at
    res_at = (rel_at + len(rel) + 0xfff) & ~0xfff
    sym_at = (res_at + len(res_bytes) + 0xf) & ~0xf
    str_at = sym_at + len(symtab)
    shstr_at = str_at + len(strtab)
    sh_at = (shstr_at + len(shstr) + 3) & ~3
    sections = [
        (0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
        (off(b'.rel'), 1, 7, 0, rel_at, len(rel), 0, 0, 16, 0),
        (off(b'.resident'), 1, 7, p_vaddr, res_at, len(res_bytes), 0, 0, 16, 0),
        (off(b'.resbss'), 8, 3, p_vaddr + p_filesz, res_at + len(res_bytes), p_memsz - p_filesz, 0, 0, 16, 0),
        (off(b'.symtab'), 2, 0, 0, sym_at, len(symtab), 5, 1, 4, 16),
        (off(b'.strtab'), 3, 0, 0, str_at, len(strtab), 0, 0, 1, 0),
        (off(b'.shstrtab'), 3, 0, 0, shstr_at, len(shstr), 0, 0, 1, 0),
    ]
    entry = rel_report['entry']
    elf = bytearray(sh_at + shsize * len(sections))
    elf[:16] = b'\x7fELF' + bytes([1, 1, 1, 0]) + bytes(8)
    struct.pack_into('<HHIIIIIHHHHHH', elf, 16, 2, 8, 1, entry, ehsize, sh_at, 0x20924001, ehsize, phsize, phnum,
                     shsize, len(sections), len(sections) - 1)
    struct.pack_into('<8I', elf, ehsize, 1, rel_at, 0, 0, len(rel), len(rel), 7, 0x1000)
    struct.pack_into('<8I', elf, ehsize + phsize, 1, res_at, p_vaddr, p_vaddr, len(res_bytes), p_memsz, 7, 0x1000)
    elf[rel_at:rel_at + len(rel)] = rel
    elf[res_at:res_at + len(res_bytes)] = res_bytes
    elf[sym_at:sym_at + len(symtab)] = symtab
    elf[str_at:str_at + len(strtab)] = strtab
    elf[shstr_at:shstr_at + len(shstr)] = shstr
    for i, s in enumerate(sections):
        struct.pack_into('<10I', elf, sh_at + shsize * i, *s)
    out_path = Path(out_path)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(elf)
    print(f'{name}: {len(rel)} bytes, {len(rel_report["references"])} import sites resolved '
          f'({len(missing)} symbols missing), {len(records)} class records, {len(symbols)} symbols -> {out_path}')
    return records


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('level', help='two digits (01..24) or FRONTEND')
    ap.add_argument('out', type=Path)
    ap.add_argument('--classes', type=Path, help='also write the class records as JSON')
    a = ap.parse_args()
    build(a.level, a.out, a.classes)
