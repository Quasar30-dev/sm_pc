"""Size Matters: the localised strings of /HUD/LOCALDAT.BIN by text id. Research aid.

Format (docs/SIZE_MATTERS_GAMEPLAY_ORACLE.md, "Frontend title and localization"): "LOC_DATA", language count at byte
12, text count at 13 (u16), "LTOC" at 0x34 then 12-byte language records {base, size, auxiliary}; a language starts
with {TDEF, id, text offset} records, offsets relative to the language's base and pointing past an "ENTR" marker.

    python tools/sm/sm_text.py                 # write build/sm_pc_work/text_en.txt (id<TAB>text), English
    python tools/sm/sm_text.py 40 41 816       # print these ids
    python tools/sm/sm_text.py --find resume   # ids whose text contains a word (case-insensitive)
"""
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def load(language=0):
    index = json.loads((ROOT / 'mods/sm_native/oracle-v3/index.json').read_text())
    entry = next(e for e in index['disc_files'] if e['disc_path'] == '/HUD/LOCALDAT.BIN')
    with open(index['source_iso'], 'rb') as f:
        f.seek(entry['disc_lba'] * index['sector_size'])
        d = f.read(entry['file_size'])
    assert d[:8] == b'LOC_DATA'
    count, = struct.unpack_from('<H', d, 13)
    assert d[0x34:0x38] == b'LTOC'
    base, size, _ = struct.unpack_from('<3I', d, 0x38 + language * 12)
    out = {}
    for i in range(count):
        tag, ident, offset = struct.unpack_from('<4sII', d, base + i * 12)
        if tag != b'TDEF':
            break
        at = base + offset
        end = d.find(b'\0', at)
        raw = d[at:end]
        # colour control 0x90 + argument; other bytes above 0x7f are button glyphs
        text, k = [], 0
        while k < len(raw):
            c = raw[k]
            if c == 0x90:
                k += 2
                continue
            text.append(chr(c) if 32 <= c < 127 else '\\n' if c == 10 else f'<{c:02x}>')
            k += 1
        out[ident] = ''.join(text)
    return out


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    text = load()
    if len(sys.argv) > 2 and sys.argv[1] == '--find':
        word = sys.argv[2].lower()
        for ident, s in sorted(text.items()):
            if word in s.lower():
                print(ident, s[:140])
        return
    if len(sys.argv) > 1:
        for a in sys.argv[1:]:
            print(a, text.get(int(a), '(none)')[:200])
        return
    out = ROOT / 'build/sm_pc_work/text_en.txt'
    out.write_text('\n'.join(f'{i}\t{s}' for i, s in sorted(text.items())) + '\n', encoding='utf-8')
    print(len(text), 'strings ->', out)


if __name__ == '__main__':
    main()
