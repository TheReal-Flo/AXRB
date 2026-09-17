"""Experimental ELF rewrite for NDK bridge's missing F64 vector estimates.

Uses hardware-translated FSQRT/FDIV for a full-precision estimate. This is not
bit-identical to ARM's approximate estimate and may change FP exception flags.
Only use on inspected binaries whose callers tolerate a more accurate estimate.
"""
import struct


def branch(pc, target):
    delta = target - pc
    if delta % 4 or not -(1 << 27) <= delta < (1 << 27):
        raise ValueError("ARM64 branch target out of range")
    return 0x14000000 | ((delta // 4) & 0x3ffffff)


def rewrite(data):
    out = bytearray(data)
    if out[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<H', out, 18)[0] != 183:
        raise ValueError('Expected little-endian ARM64 ELF64')
    phoff, shoff = struct.unpack_from('<QQ', out, 32)
    phsize, phnum, shsize, shnum = struct.unpack_from('<HHHH', out, 54)
    if phsize != 56 or shsize != 64:
        raise ValueError('Unexpected ELF header sizes')
    headers = [struct.unpack_from('<IIQQQQQQ', out, phoff+i*phsize) for i in range(phnum)]
    notes = [i for i,p in enumerate(headers) if p[0] == 4]
    if len(notes) != 1:
        raise ValueError('Expected exactly one reusable PT_NOTE header')
    align = lambda x: (x+4095) & ~4095
    start = align(len(out))
    va = align(max(p[3]+p[6] for p in headers if p[0] == 1))
    stubs = bytearray()
    counts = {'frsqrte_2d': 0, 'frecpe_2d': 0}
    for i in range(shnum):
        s = struct.unpack_from('<IIQQQQIIQQ', data, shoff+i*shsize)
        _, typ, flags, addr, offset, size, *_ = s
        if typ != 1 or not flags & 4:
            continue
        if offset % 4 or size % 4:
            raise ValueError('Unaligned executable section')
        for pos in range(offset, offset+size, 4):
            word = struct.unpack_from('<I', data, pos)[0]
            op = word & 0xfffffc00
            if op not in (0x6ee1d800, 0x4ee1d800):
                continue
            dest, src = word & 31, (word >> 5) & 31
            scratch = next(r for r in range(31,-1,-1) if r not in (dest,src))
            pc, stub = addr+pos-offset, va+len(stubs)
            code = [0xd10043ff, 0x3d8003e0 | scratch]
            denom = src
            if op == 0x6ee1d800:
                code.append(0x6ee1f800 | src << 5 | dest)
                denom = dest
            code += [0x6f03f600 | scratch,
                     0x6e60fc00 | denom << 16 | scratch << 5 | dest,
                     0x3dc003e0 | scratch, 0x910043ff]
            code.append(branch(stub+4*len(code),pc+4))
            stubs.extend(struct.pack('<'+'I'*len(code), *code))
            struct.pack_into('<I',out,pos,branch(pc,stub))
            counts['frsqrte_2d' if op == 0x6ee1d800 else 'frecpe_2d'] += 1
    if not stubs:
        return out, counts
    out.extend(bytes(start-len(out)))
    out.extend(stubs)
    # Retain note bytes/sections; repurpose only their optional program header.
    struct.pack_into('<IIQQQQQQ',out,phoff+notes[0]*phsize,
                     1,5,start,va,va,len(stubs),len(stubs),4096)
    return out, counts


if __name__ == '__main__':
    import argparse
    from pathlib import Path
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('input',type=Path)
    p.add_argument('output',type=Path)
    a=p.parse_args()
    result, counts=rewrite(a.input.read_bytes())
    a.output.write_bytes(result)
    print(counts)
