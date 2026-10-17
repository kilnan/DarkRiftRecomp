#!/usr/bin/env python3
"""Find functions that exist in the decomp objects' .mdebug (IDO debug info) but have no
symbol in the linked ELF -- typically libultra `static` functions -- and print them as
N64Recomp `manual_funcs` entries.

usage: mdebug_static_funcs.py <decomp dir> <syms.toml from --dump-context>
"""
import re, struct, subprocess, sys, os

decomp, syms_path = sys.argv[1], sys.argv[2]

def sections(path):
    out = subprocess.run(['mips-linux-gnu-readelf', '-SW', path], capture_output=True, text=True).stdout
    secs = {}
    for m in re.finditer(r'\]\s+(\S+)\s+\S+\s+[0-9a-f]+\s+([0-9a-f]+)\s+([0-9a-f]+)', out):
        secs[m.group(1)] = (int(m.group(2), 16), int(m.group(3), 16))
    return secs

def mdebug_procs(path):
    secs = sections(path)
    if '.mdebug' not in secs:
        return []
    data = open(path, 'rb').read()
    off, _ = secs['.mdebug']
    h = struct.unpack('>hh23i', data[off:off + 96])
    # HDRR fields after magic/vstamp
    (ilineMax, cbLine, cbLineOffset, idnMax, cbDnOffset, ipdMax, cbPdOffset,
     isymMax, cbSymOffset, ioptMax, cbOptOffset, iauxMax, cbAuxOffset,
     issMax, cbSsOffset, issExtMax, cbSsExtOffset, ifdMax, cbFdOffset,
     crfd, cbRfdOffset, iextMax, cbExtOffset) = h[2:]
    procs = []
    # Use the procedure descriptor table (PDR): unlike the symbol table it only lists
    # procedures that are actually defined (no forward declarations).
    for f in range(ifdMax):
        fd = data[cbFdOffset + f * 72: cbFdOffset + f * 72 + 72]
        adr, rss, issBase, cbSs, isymBase, csym = struct.unpack('>6i', fd[:24])
        ipdFirst, cpd = struct.unpack('>hh', fd[40:44])
        for k in range(cpd):
            po = cbPdOffset + (ipdFirst + k) * 52
            pd_adr, isym = struct.unpack('>ii', data[po:po + 8])
            so = cbSymOffset + (isymBase + isym) * 12
            iss = struct.unpack('>i', data[so:so + 4])[0]
            sname = cbSsOffset + issBase + iss
            name = data[sname:data.index(b'\0', sname)].decode()
            procs.append((pd_adr, name))
    return sorted(set(procs))

# object -> .text vram, from the linker map
text_base = {}
for line in open(os.path.join(decomp, 'build/darkrift.map')):
    m = re.match(r'\s*\.text\s+0x0*([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+\.o)', line)
    if m and int(m.group(2), 16) > 0:
        text_base[m.group(3)] = (int(m.group(1), 16), int(m.group(2), 16))

known = {int(m.group(1), 16) for m in re.finditer(r'vram = 0x([0-9A-F]+)', open(syms_path).read())}
# Functions N64Recomp deliberately skips because the runtime reimplements them.
skip_lists = open(os.path.join(os.path.dirname(__file__), '../tools/N64Recomp/src/symbol_lists.cpp')).read()
reimplemented = set(re.findall(r'"([A-Za-z_][A-Za-z0-9_]*)"', skip_lists))

out = []
for obj, (base, size) in sorted(text_base.items(), key=lambda kv: kv[1][0]):
    try:
        procs = mdebug_procs(os.path.join(decomp, obj))
    except (struct.error, ValueError) as e:
        print(f'# skipped {obj}: {e}', file=sys.stderr)
        continue
    for i, (start, name) in enumerate(procs):
        end = procs[i + 1][0] if i + 1 < len(procs) else size
        vram = base + start
        if (vram not in known and not name.startswith('_asmpp_') and name not in reimplemented
                and re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', name) and end > start):
            out.append((vram, end - start, name, obj))
for vram, size, name, obj in out:
    print(f'    {{ name = "{name}", section = ".main", vram = 0x{vram:08X}, size = 0x{size:X} }}, # {obj}')
