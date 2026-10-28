#!/usr/bin/env python3
"""
Mod Loader (Android port) - checks a libGTASA.so against what the port expects.

    python3 android/tools/verify_libgtasa.py path/to/libGTASA.so

The port finds everything it hooks/reads BY NAME in the dynamic symbol table of libGTASA.so (the game exports its C++
symbols) and, before it hooks a function, compares the first bytes of the function with a fingerprint taken from the
library the port was written against. This script runs the very same checks offline (it reads the symbol names and the
fingerprints straight from src/game/gtasa200.hpp / gtasa200.cpp, so it can never go out of sync with the C++ code), which
is handy to know whether a given build of the game is supported without installing anything on a phone.

Exit code: 0 = every hook/data symbol is present and every fingerprint matches, 1 = something differs.
Only the Python standard library is needed.
"""
import hashlib, os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.join(HERE, '..', 'src', 'game')

# Sizes of the exported data objects the port relies on (see gtasa200.hpp: StreamingFile = 48 bytes, CStreamingInfo = 20 bytes)
EXPECTED_SIZES = {
    '_ZN10CStreaming8ms_filesE':                8 * 48,
    '_ZN10CStreaming16ms_aInfoForModelE':       26316 * 20,
    '_ZN10CStreaming22ms_streamingBufferSizeE': 4,
}


def read_elf(path):
    data = open(path, 'rb').read()
    if data[:4] != b'\x7fELF' or data[4] != 1 or data[5] != 1:
        raise SystemExit('not a 32 bits little endian ELF file')
    e_machine, = struct.unpack_from('<H', data, 0x12)
    if e_machine != 40:
        raise SystemExit('not an ARM (32 bits) library, GTA SA 2.00 is armeabi-v7a')

    e_phoff, = struct.unpack_from('<I', data, 0x1c)
    e_shoff, = struct.unpack_from('<I', data, 0x20)
    e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx = struct.unpack_from('<HHHHH', data, 0x2a)

    loads = []
    for i in range(e_phnum):
        p_type, p_off, p_va, _pa, p_fsz, p_msz, _fl, _al = struct.unpack_from('<8I', data, e_phoff + i * e_phentsize)
        if p_type == 1:
            loads.append((p_va, p_off, p_fsz))

    sections = []
    for i in range(e_shnum):
        sh = struct.unpack_from('<10I', data, e_shoff + i * e_shentsize)
        sections.append(sh)   # name, type, flags, addr, offset, size, link, info, addralign, entsize

    dynsym = next((s for s in sections if s[1] == 11), None)       # SHT_DYNSYM
    if not dynsym:
        raise SystemExit('no .dynsym: this library exports nothing, the port cannot work with it')
    dynstr = sections[dynsym[6]]

    symbols = {}
    for off in range(dynsym[4], dynsym[4] + dynsym[5], 16):
        st_name, st_value, st_size, st_info, _other, st_shndx = struct.unpack_from('<IIIBBH', data, off)
        if st_shndx == 0:
            continue
        end = data.index(b'\0', dynstr[4] + st_name)
        symbols[data[dynstr[4] + st_name:end].decode('ascii', 'replace')] = (st_value, st_size)

    def read_at(vaddr, n):
        for va, off, fsz in loads:
            if va <= vaddr < va + fsz:
                return data[off + vaddr - va: off + vaddr - va + n]
        return b''

    return data, symbols, read_at


def load_expectations():
    hpp = open(os.path.join(GAME, 'gtasa200.hpp')).read()
    cpp = open(os.path.join(GAME, 'gtasa200.cpp')).read()

    names = dict(re.findall(r'static const char\* const (\w+)\s*=\s*"([^"]+)";', hpp))
    fps = []
    for m in re.finditer(r'const Fingerprint (\w+)\s*=\s*\{\s*"([^"]+)",\s*sym::(\w+),\s*\{([^}]*)\}', cpp):
        raw = bytes(int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{2})', m.group(4)))
        fps.append((m.group(2), names[m.group(3)], raw))
    return names, fps


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2

    path = sys.argv[1]
    data, symbols, read_at = read_elf(path)
    names, fps = load_expectations()

    print('library : %s (%d bytes)' % (path, len(data)))
    print('sha256  : %s' % hashlib.sha256(data).hexdigest())
    print('exports : %d symbols' % len(symbols))
    print()

    bad = 0
    fp_by_symbol = {sym: (label, raw) for label, sym, raw in fps}

    for key, mangled in sorted(names.items()):
        sym = symbols.get(mangled)
        if sym is None:
            print('  MISSING  %-26s %s' % (key, mangled))
            bad += 1
            continue

        status = 'ok'
        if mangled in fp_by_symbol:
            label, raw = fp_by_symbol[mangled]
            got = read_at(sym[0] & ~1, len(raw))
            if got != raw:
                status = 'FINGERPRINT MISMATCH (expected %s, found %s)' % (raw.hex(' '), got.hex(' '))
                bad += 1
            else:
                status = 'ok (fingerprint matches, Thumb=%s)' % bool(sym[0] & 1)
        if mangled in EXPECTED_SIZES and sym[1] != EXPECTED_SIZES[mangled]:
            status = 'SIZE MISMATCH (expected %d, found %d)' % (EXPECTED_SIZES[mangled], sym[1])
            bad += 1
        print('  %-9s %-26s @ 0x%08x  %s' % ('' if status.startswith('ok') else 'PROBLEM', key, sym[0], status))

    print()
    if bad:
        print('%d problem(s): this build of the game is NOT fully supported, the affected features stay disabled at run time' % bad)
        return 1
    print('everything the port needs is there: this build of the game is supported')
    return 0


if __name__ == '__main__':
    sys.exit(main())
