#!/usr/bin/env python3
"""Translates the original client's swept-sphere maths into C++, instruction by instruction.

Input: `objdump -d -M intel` listings of eqgame.exe's functions (rof2decomp's NMS/eqgame.exe, the RoF2 client of
May 10 2013), one file per function, named f_<hex address>.txt. Output: src/game/sweep_x87.inc, the body of
nmsc::game::SweepX87 (game/sweep_x87.h): one C++ statement per original instruction, run on a small x87 machine
(32-bit registers and flags, a 64 KB memory, the FPU stack with the original's rounding from game/mechanics/fpu.h).

Why a translation and not a rewrite: the collision sweep's results depend on every x87 rounding, the order of the
operands (NaN payloads) and the comparisons' unordered cases; rof2decomp keeps these routines as copies of the
original's instruction streams (re/src/game/spawn/collision_x87.inc) for the same reason, and its vectors
(SweptSphereTriangle, SweepStopAndSlide, s3dCollideActorSweep) check the result bit for bit. The algorithm itself is
written out in rof2decomp notes/collision-sweep.md.

x87 register forms are decoded from the opcode bytes (Intel's operand order), not from objdump's mnemonics.

Usage: gen_sweep_x87.py <listing dir> <output .inc>
"""
import os
import re
import sys

FUNCS = {
    0x441bb0: 'Vec3_Normalize',
    0x509130: 'Vec3_NormalizeInPlace',
    0x542830: 'Vec3_Scale',
    0x7a3550: 'PointSegmentDistanceSq',
    0x7a98c0: 'LineCapsuleIntersect',
    0x7a9d30: 'SegmentCapsuleHits',
    0x7a9220: 'SweptSphereEdge',
    0x7a63c0: 'SweptSphereTriangle_EdgeRegion',
    0x7a65d0: 'SweptSphereVertex',
    0x7a6770: 'SweptSphereTriangle',
    0x7a8300: 'PointTriangleDistanceSq',
    0x7a6380: 'SphereTriangleOverlap',
    0x7a28e0: 'CCollisionInfoSphere_vf0',
    0x7a2680: 'CollisionInfo_GetClampPoint',
    0x7a2720: 'CCollisionInfoBase_ComputeSkitter',
    0x7aa200: 'Vec3_PerpendicularBasis',
}

# .rdata constants the routines read (eqgame.exe, little-endian float bits).
CONSTS = {
    0x9c3388: 0x34000000, 0x9c338c: 0xb4000000, 0x9c3394: 0x00000000, 0x9c4be4: 0x3c23d70a,
    0x9c4be8: 0x3a83126f, 0x9c58e8: 0x40000000, 0x9d9440: 0x38d1b717, 0xa01fb8: 0x3f7ffffe,
}
# The statics at 0xf80424..0xf80433 (the zero origin and its init flag) live in the machine's memory here.
STATIC_BASE, STATIC_MAP = 0xf80420, 0x40

REG32 = ['eax', 'ecx', 'edx', 'ebx', 'esp', 'ebp', 'esi', 'edi']
REG8 = {'al': ('eax', 0), 'cl': ('ecx', 0), 'dl': ('edx', 0), 'bl': ('ebx', 0),
        'ah': ('eax', 8), 'ch': ('ecx', 8), 'dh': ('edx', 8), 'bh': ('ebx', 8)}
REG16 = {'ax': 'eax', 'cx': 'ecx', 'dx': 'edx', 'bx': 'ebx', 'si': 'esi', 'di': 'edi', 'bp': 'ebp'}


def remap(addr):
    if STATIC_BASE <= addr < STATIC_BASE + 0x20:
        return addr - STATIC_BASE + STATIC_MAP
    raise ValueError('unmapped address 0x%x' % addr)


def ea(text):
    """C++ for the effective address of a memory operand like [esi+ecx*4-0x4] or ds:0x9c3388."""
    m = re.match(r'ds:(0x[0-9a-f]+)$', text)
    if m:
        return ('abs', int(m.group(1), 16))
    m = re.match(r'\[(.*)\]$', text)
    assert m, text
    parts = []
    for tok in re.findall(r'[+-]?[^+-]+', m.group(1)):
        sign = '-' if tok.startswith('-') else '+'
        tok = tok.lstrip('+-')
        if '*' in tok:
            r, s = tok.split('*')
            parts.append('%s %s * %s' % (sign, r, s))
        elif tok in REG32:
            parts.append('%s %s' % (sign, tok))
        else:
            parts.append('%s 0x%x' % (sign, int(tok, 16)))
    expr = ' '.join(parts).lstrip('+ ').strip()
    if expr.startswith('- '):
        expr = '0u ' + expr
    return ('expr', 'uint32_t(%s)' % expr)


def mem_operand(text):
    m = re.match(r'(BYTE|WORD|DWORD|QWORD) PTR (.*)$', text)
    assert m, text
    return m.group(1), ea(m.group(2))


def read(size, where):
    kind, v = where
    if kind == 'abs':
        if v in CONSTS:
            assert size == 'DWORD'
            return 'k32(0x%08xu)' % CONSTS[v]
        v = '0x%xu' % remap(v)
    return {'BYTE': 'rd8', 'WORD': 'rd16', 'DWORD': 'rd32'}[size] + '(%s)' % v


def addr_of(where):
    kind, v = where
    return '0x%xu' % remap(v) if kind == 'abs' else v


def value(op):
    """An integer source operand: (C++ expression, size in bits)."""
    if op in REG32:
        return op, 32
    if op in REG8:
        r, sh = REG8[op]
        return '((%s >> %d) & 0xffu)' % (r, sh), 8
    if op in REG16:
        return '(%s & 0xffffu)' % REG16[op], 16
    if re.match(r'(0x[0-9a-f]+|\d+)$', op):
        v = int(op, 0)
        if STATIC_BASE <= v < STATIC_BASE + 0x20:
            v = remap(v)
        return '0x%xu' % v, 32
    size, where = mem_operand(op)
    return read(size, where), {'BYTE': 8, 'WORD': 16, 'DWORD': 32}[size]


def store(op, expr):
    if op in REG32:
        return '%s = %s;' % (op, expr)
    if op in REG8:
        r, sh = REG8[op]
        return '%s = (%s & ~(0xffu << %d)) | ((uint32_t(%s) & 0xffu) << %d);' % (r, r, sh, expr, sh)
    if op in REG16:
        r = REG16[op]
        return '%s = (%s & 0xffff0000u) | (uint32_t(%s) & 0xffffu);' % (r, r, expr)
    size, where = mem_operand(op)
    fn = {'BYTE': 'wr8', 'WORD': 'wr16', 'DWORD': 'wr32'}[size]
    return '%s(%s, %s);' % (fn, addr_of(where), expr)


def x87_reg(b0, b1):
    """The register forms of D8..DF: a C++ statement."""
    i = b1 & 7
    hi = b1 & 0xf8
    t = {
        (0xd8, 0xc0): 'st(0) = add(st(0), st(%d));',
        (0xd8, 0xc8): 'st(0) = mul(st(0), st(%d));',
        (0xd8, 0xd0): 'fcom(st(%d));',
        (0xd8, 0xd8): 'fcom(st(%d)); pop();',
        (0xd8, 0xe0): 'st(0) = sub(st(0), st(%d));',
        (0xd8, 0xe8): 'st(0) = sub(st(%d), st(0));',
        (0xd8, 0xf0): 'st(0) = div(st(0), st(%d));',
        (0xd8, 0xf8): 'st(0) = div(st(%d), st(0));',
        (0xdc, 0xc0): 'st(%d) = add(st(%d), st(0));',
        (0xdc, 0xc8): 'st(%d) = mul(st(%d), st(0));',
        (0xdc, 0xe0): 'st(%d) = sub(st(0), st(%d));',
        (0xdc, 0xe8): 'st(%d) = sub(st(%d), st(0));',
        (0xdc, 0xf0): 'st(%d) = div(st(0), st(%d));',
        (0xdc, 0xf8): 'st(%d) = div(st(%d), st(0));',
        (0xde, 0xc0): 'st(%d) = add(st(%d), st(0)); pop();',
        (0xde, 0xc8): 'st(%d) = mul(st(%d), st(0)); pop();',
        (0xde, 0xe0): 'st(%d) = sub(st(0), st(%d)); pop();',
        (0xde, 0xe8): 'st(%d) = sub(st(%d), st(0)); pop();',
        (0xde, 0xf0): 'st(%d) = div(st(0), st(%d)); pop();',
        (0xde, 0xf8): 'st(%d) = div(st(%d), st(0)); pop();',
        (0xd9, 0xc0): 'push_st(st(%d));',
        (0xd9, 0xc8): 'fxch(%d);',
        (0xdd, 0xd0): 'st(%d) = st(0);',
        (0xdd, 0xd8): 'st(%d) = st(0); pop();',
        (0xdd, 0xe0): 'fcom(st(%d));',
        (0xdd, 0xe8): 'fcom(st(%d)); pop();',
    }.get((b0, hi))
    if t:
        return t.replace('%d', str(i))
    fixed = {
        (0xd9, 0xe0): 'st(0) = -st(0);',
        (0xd9, 0xe1): 'st(0) = std::fabs(st(0));',
        (0xd9, 0xe8): 'push_st(1.0);',
        (0xd9, 0xee): 'push_st(0.0);',
        (0xd9, 0xfa): 'st(0) = fpu_.sqrt(st(0));',
        (0xde, 0xd9): 'fcom(st(1)); pop(); pop();',
        (0xda, 0xe9): 'fcom(st(1)); pop(); pop();',
        (0xdf, 0xe0): 'eax = (eax & 0xffff0000u) | fnstsw();',
    }.get((b0, b1))
    assert fixed, 'x87 %02x %02x' % (b0, b1)
    return fixed


def x87_mem(mn, opnd):
    size, where = mem_operand(opnd)
    assert size == 'DWORD', opnd
    if mn == 'fld':
        return 'push_st(ld(%s));' % read(size, where)
    if mn in ('fst', 'fstp'):
        return 'wr32(%s, st32(st(0)));%s' % (addr_of(where), ' pop();' if mn == 'fstp' else '')
    m = 'mem(%s)' % read(size, where)
    return {
        'fadd': 'st(0) = add(st(0), %s);',
        'fsub': 'st(0) = sub(st(0), %s);',
        'fsubr': 'st(0) = sub(%s, st(0));',
        'fmul': 'st(0) = mul(st(0), %s);',
        'fdiv': 'st(0) = div(st(0), %s);',
        'fdivr': 'st(0) = div(%s, st(0));',
        'fcom': 'fcom(%s);',
        'fcomp': 'fcom(%s); pop();',
    }[mn].replace('%s', m)


JCC = {'je': 'zf', 'jne': '!zf', 'jp': 'pf', 'jnp': '!pf', 'jb': 'cf', 'jbe': '(cf || zf)', 'jmp': 'true'}


def translate(addr, lines):
    out = []
    targets = set()
    parsed = []
    for line in lines:
        m = re.match(r'\s*([0-9a-f]+):\t([0-9a-f ]+)\t(\S+)\s*(.*)$', line)
        if not m:
            continue
        a = int(m.group(1), 16)
        bs = [int(x, 16) for x in m.group(2).split()]
        mn, ops = m.group(3), m.group(4).strip()
        parsed.append((a, bs, mn, ops))
        if mn in JCC:
            targets.add(int(ops, 16))
    for a, bs, mn, ops in parsed:
        asm = ('%s %s' % (mn, ops)).strip()
        if a in targets:
            out.append('L_%x:' % a)
        ops_l = [o.strip() for o in re.split(r',(?![^\[]*\])', ops)] if ops else []
        if bs[0] in (0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf):
            if bs[1] >= 0xc0:
                code = x87_reg(bs[0], bs[1])
            else:
                code = x87_mem(mn, ops_l[0])
        elif mn in JCC:
            code = ('if (%s) ' % JCC[mn] if mn != 'jmp' else '') + 'goto L_%x;' % int(ops, 16)
        elif mn == 'call':
            t = int(ops, 16)
            code = 'call(&SweepX87::f_%x);' % t
        elif mn == 'ret':
            n = int(ops, 0) if ops else 0
            code = 'esp += 4 + %d; return;' % n
        elif mn == 'push':
            v, _ = value(ops_l[0])
            code = 'push32(%s);' % v
        elif mn == 'pop':
            code = '%s = pop32();' % ops_l[0]
        elif mn == 'mov':
            v, _ = value(ops_l[1])
            code = store(ops_l[0], v)
        elif mn == 'lea':
            _, where = mem_operand('DWORD PTR ' + ops_l[1])
            code = '%s = %s;' % (ops_l[0], addr_of(where))
        elif mn in ('add', 'sub', 'xor', 'or', 'and', 'cmp', 'test', 'sbb'):
            a_, bits = value(ops_l[0])
            b_, _ = value(ops_l[1])
            fn = {'add': 'alu_add', 'sub': 'alu_sub', 'cmp': 'alu_sub', 'sbb': 'alu_sbb', 'xor': 'alu_xor',
                  'or': 'alu_or', 'and': 'alu_and', 'test': 'alu_and'}[mn]
            expr = '%s(%s, %s, %d)' % (fn, a_, b_, bits)
            code = expr + ';' if mn in ('cmp', 'test') else store(ops_l[0], expr)
        elif mn in ('inc', 'dec', 'neg'):
            a_, bits = value(ops_l[0])
            fn = {'inc': 'alu_inc(%s, %d)', 'dec': 'alu_dec(%s, %d)', 'neg': 'alu_neg(%s, %d)'}[mn] % (a_, bits)
            code = store(ops_l[0], fn)
        elif mn == 'cdq':
            code = 'edx = (eax & 0x80000000u) ? 0xffffffffu : 0u;'
        elif mn == 'idiv':
            v, _ = value(ops_l[0])
            code = 'idiv(%s);' % v
        else:
            raise ValueError('%x: %s' % (a, asm))
        out.append('    %-60s // %x: %s' % (code, a, asm))
    name = FUNCS[addr]
    return ('// %s @0x%x\nvoid f_%x() {\n' % (name, addr, addr)) + '\n'.join(out) + '\n}\n'


def main():
    src, dst = sys.argv[1], sys.argv[2]
    parts = ['// Generated by tools/gen_sweep_x87.py from eqgame.exe (RoF2, May 10 2013); do not edit.\n'
             '// The original client\'s swept-sphere routines, one statement per instruction (see game/sweep_x87.h).\n']
    for addr in sorted(FUNCS):
        with open(os.path.join(src, 'f_%x.txt' % addr)) as f:
            parts.append(translate(addr, f.read().split('\n')))
    with open(dst, 'w') as f:
        f.write('\n'.join(parts))


if __name__ == '__main__':
    main()
