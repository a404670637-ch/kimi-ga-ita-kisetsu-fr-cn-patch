# -*- coding: utf-8 -*-
"""Regenerate namemap.h from the CURRENT cnpatch_txt.bin.

The colour layer matches the Chinese speaker name bytes in the running text
against g_namemap[i].zh.  When the tunnel codes were renumbered (v1.19) those
hard-coded bytes became stale, so colour_hint/colour_compare stopped matching
and every line fell back to the palette's Japanese name + default colour.
"""
import struct, io, os, sys
sys.stdout.reconfigure(encoding='utf-8', errors='replace')
BASE = r"C:\Users\40467\Documents\deepseek-harness\default-workspace"
p = os.path.join(BASE, 'work', 'p5', 'dist', 'cnpatch', 'cnpatch_txt.bin')
d = open(p, 'rb').read()
assert d[:4] == b'CNTX'
n = struct.unpack_from('<I', d, 4)[0]
offs = struct.unpack_from('<%dI' % n, d, 8)
base = 8 + 4 * n
pairs = {}
for o in offs:
    q = base + o
    kl = struct.unpack_from('<H', d, q)[0]; key = d[q+2:q+2+kl]
    q2 = q + 2 + kl
    vl = struct.unpack_from('<H', d, q2)[0]; val = d[q2+2:q2+2+vl]
    def name_of(b):
        if b.startswith(b'\x81\x79'):
            e = b.find(b'\x81\x7a', 2)
            if e > 2: return b[2:e]
        return None
    jn = name_of(key); zn = name_of(val)
    if jn and zn: pairs[jn] = zn
print('pairs', len(pairs))
dst = os.path.join(BASE, 'work', 'p5', 'namemap.h')
with io.open(dst, 'w', encoding='utf-8') as f:
    f.write('#pragma once\n')
    f.write('typedef struct { const unsigned char* jp; int jplen; const unsigned char* zh; int zhlen; } NAMEPAIR;\n')
    f.write('static const NAMEPAIR g_namemap[] = {\n')
    for jn, zn in sorted(pairs.items()):
        f.write('  {(const unsigned char*)"%s",%d,(const unsigned char*)"%s",%d},\n' % (
            ''.join('\\x%02x' % b for b in jn), len(jn), ''.join('\\x%02x' % b for b in zn), len(zn)))
    f.write('};\n')
    f.write('static const int g_namemap_n = %d;\n' % len(pairs))
# show a few samples with the characters that matter
for jn, zn in sorted(pairs.items()):
    if jn.startswith('まりか'.encode('cp932')) or jn.startswith('三島'.encode('cp932')):
        print(jn.decode('cp932'), '->', zn.hex(' '), '|', zn.decode('cp932','replace'))
