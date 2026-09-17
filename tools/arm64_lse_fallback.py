"""Lower ARM64 SWP/LDADD/CAS to register-preserving exclusive loops.

For the emulator's ARM64 native bridge. Acquire/release exclusives deliberately
provide at least the ordering of every LSE variant. No package or engine offsets.
"""
import struct
from arm64_vector_math_fallback import branch

def instruction(word):
    if word & 0x3fa07c00 == 0x08a07c00:
        return 'cas'
    op = word & 0x3f20fc00
    return {0x38208000: 'swp', 0x38200000: 'ldadd'}.get(op)

def stub_words(word):
    op = instruction(word)
    if not op: raise ValueError('Not SWP/LDADD/CAS')
    size, source, address, result = word >> 30, (word >> 16) & 31, (word >> 5) & 31, word & 31
    old, status, new, base = [r for r in range(30, -1, -1) if r not in (source, address, result)][:4]
    words = [0xa9be03e0 | old | status << 10, 0xa90103e0 | new | base << 10]
    if address == 31:
        words.append(0x910083e0 | base)  # Original SP, before saving scratch registers.
        address = base
    retry = len(words)
    words.append(0x085ffc00 | size << 30 | address << 5 | old)  # LDAXR[B/H]
    value = source
    mismatch = None
    if op == 'cas':
        # CAS compares Rs, stores Rt and returns the old value in Rs.
        value, result = result, source
        compare = source
        if size < 2:
            words.append(0x53000000 | ((8 << size)-1) << 10 | source << 5 | new)  # UXTB/UXTH
            compare = new
        words.append((0xca000000 if size == 3 else 0x4a000000) | compare << 16 | old << 5 | new)
        mismatch = len(words)
        words.append(0)  # CBNZ to CLREX, without changing condition flags.
    if op == 'ldadd':
        words.append((0x8b000000 if size == 3 else 0x0b000000) | source << 16 | old << 5 | new)
        value = new
    words.append(0x0800fc00 | size << 30 | status << 16 | address << 5 | value)  # STLXR[B/H]
    words.append(0x35000000 | ((retry-len(words)) & 0x7ffff) << 5 | status)  # CBNZ; preserves NZCV.
    if mismatch is not None:
        words[mismatch] = (0xb5000000 if size == 3 else 0x35000000) | (len(words)-mismatch) << 5 | new
        words.append(0xd5033f5f)  # CLREX (also harmless after successful STLXR).
    if result != 31:
        words.append((0xaa0003e0 if size == 3 else 0x2a0003e0) | old << 16 | result)
    words += [0xa94103e0 | new | base << 10, 0xa8c203e0 | old | status << 10]
    return words

def sites(data):
    if len(data)<64 or data[:6]!=b'\x7fELF\x02\x01' or struct.unpack_from('<H',data,18)[0]!=183:
        raise ValueError('Expected little-endian ARM64 ELF64')
    shoff=struct.unpack_from('<Q',data,40)[0]
    shsize,shnum=struct.unpack_from('<HH',data,58)
    if shsize!=64 or shoff+shnum*shsize>len(data):raise ValueError('Invalid section headers')
    sections=[struct.unpack_from('<IIQQQQIIQQ',data,shoff+i*shsize) for i in range(shnum)]
    mappings={}
    # ELF mapping symbols distinguish literal pools from A64 instructions.
    for section in sections:
        if section[1] not in (2,11):continue
        offset,size,link,entsize=section[4],section[5],section[6],section[9]
        if entsize!=24 or size%24 or offset+size>len(data) or link>=shnum:
            raise ValueError('Invalid symbol table')
        strings=sections[link]
        if strings[4]+strings[5]>len(data):raise ValueError('Invalid string table')
        names=data[strings[4]:strings[4]+strings[5]]
        for pos in range(offset,offset+size,24):
            name,info,other,index,value,_=struct.unpack_from('<IBBHQQ',data,pos)
            if name>=len(names):continue
            end=names.find(b'\0',name)
            symbol=names[name:end]
            if symbol in (b'$x',b'$d') or symbol.startswith((b'$x.',b'$d.')):
                mappings.setdefault(index,[]).append((value,symbol[1:2]==b'x'))
    found=[]
    for i,section in enumerate(sections):
        _,typ,flags,addr,offset,size,*_=section
        if typ!=1 or not flags&4:continue
        if offset%4 or size%4 or offset+size>len(data):raise ValueError('Invalid code section')
        markers=sorted(set(mappings.get(i,[])));marker=0;is_code=True
        for pos in range(offset,offset+size,4):
            while marker<len(markers) and markers[marker][0]<=addr+pos-offset:
                is_code=markers[marker][1];marker+=1
            if not is_code:continue
            word=struct.unpack_from('<I',data,pos)[0]
            if instruction(word):found.append((pos,addr+pos-offset,word))
    return found

def rewrite(data):
    found=sites(data)
    counts={'swp':0,'ldadd':0,'cas':0}
    if not found:return data,counts
    phoff=struct.unpack_from('<Q',data,32)[0]
    phsize,phnum=struct.unpack_from('<HH',data,54)
    if phsize!=56 or phoff+phnum*phsize>len(data):raise ValueError('Invalid program headers')
    headers=[struct.unpack_from('<IIQQQQQQ',data,phoff+i*phsize) for i in range(phnum)]
    notes=[i for i,p in enumerate(headers) if p[0]==4]
    if not notes:raise ValueError('No optional PT_NOTE header available for atomic fallback code')
    align=lambda x:(x+4095)&~4095
    start=align(len(data));va=align(max(p[3]+p[6] for p in headers if p[0]==1))
    out=bytearray(data);code=bytearray()
    for pos,pc,word in found:
        target=va+len(code);words=stub_words(word)
        words.append(branch(target+4*len(words),pc+4))
        struct.pack_into('<I',out,pos,branch(pc,target))
        code.extend(struct.pack('<'+'I'*len(words),*words));counts[instruction(word)]+=1
    out.extend(bytes(start-len(out)));out.extend(code)
    struct.pack_into('<IIQQQQQQ',out,phoff+notes[0]*phsize,1,5,start,va,va,len(code),len(code),4096)
    return out,counts

if __name__=='__main__':
    import argparse,json
    from pathlib import Path
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('input',type=Path);p.add_argument('output',type=Path)
    args=p.parse_args();data,counts=rewrite(args.input.read_bytes());args.output.write_bytes(data);print(json.dumps(counts))
