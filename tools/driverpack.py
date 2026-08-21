#!/usr/bin/env python3
import argparse, configparser, struct, zlib
from pathlib import Path

MAGIC=b"RDP10001"; VERSION=1; ENTRY_SIZE=180; HEADER_SIZE=32

def parse_num(s, wildcard):
    s=s.strip().lower()
    if s in ('*','any'): return wildcard
    return int(s, 0)

def rle8(data: bytes) -> bytes:
    out=bytearray(); i=0; n=len(data)
    while i<n:
        run=1
        while i+run<n and data[i+run]==data[i] and run<130: run+=1
        if run>=3:
            out.append(0x80 | (run-3)); out.append(data[i]); i+=run; continue
        start=i; i+=run
        while i<n and i-start<128:
            run=1
            while i+run<n and data[i+run]==data[i] and run<130: run+=1
            if run>=3: break
            i+=run
        lit=data[start:i]; out.append(len(lit)-1); out.extend(lit)
    return bytes(out)

def load_spec(spec):
    ini_s, elf_s=spec.split(':',1); ini=Path(ini_s); elf=Path(elf_s)
    c=configparser.ConfigParser(); c.read(ini)
    name=c['driver']['name'].strip(); desc=c['driver'].get('description','').strip()
    matches=[]
    for token in c['driver']['matches'].split(','):
        fields=token.strip().split('/')
        ids=fields[0].split(':')
        vendor=parse_num(ids[0],0xffff); device=parse_num(ids[1],0xffff)
        cls=sub=prog=0xff
        if len(fields)>1:
            cs=fields[1].split(':'); cls=parse_num(cs[0],0xff)
            if len(cs)>1: sub=parse_num(cs[1],0xff)
            if len(cs)>2: prog=parse_num(cs[2],0xff)
        matches.append((vendor,device,cls,sub,prog))
    if not (1<=len(matches)<=8): raise SystemExit(f'{name}: matches must contain 1..8 entries')
    raw=elf.read_bytes(); comp=rle8(raw)
    compression=1 if len(comp)<len(raw) else 0; stored=comp if compression else raw
    return dict(name=name,desc=desc,matches=matches,raw=raw,stored=stored,compression=compression,crc=zlib.crc32(raw)&0xffffffff)

def fixed(s,n):
    b=s.encode('utf-8')[:n-1]; return b+b'\0'*(n-len(b))

def main():
    p=argparse.ArgumentParser(); p.add_argument('--output',required=True); p.add_argument('--driver',action='append',required=True); a=p.parse_args()
    drivers=[load_spec(x) for x in a.driver]
    data_offset=HEADER_SIZE+ENTRY_SIZE*len(drivers); cursor=data_offset; entries=[]; blobs=[]
    for d in drivers:
        m=bytearray()
        for vendor,device,cls,sub,prog in d['matches']:
            m.extend(struct.pack('<HHBBBB',vendor,device,cls,sub,prog,0))
        m.extend(b'\0'*(8*8-len(m)))
        ent=(fixed(d['name'],32)+struct.pack('<IIIIBBBB',cursor,len(d['stored']),len(d['raw']),d['crc'],d['compression'],len(d['matches']),1,0)+bytes(m)+fixed(d['desc'],64))
        if len(ent)!=ENTRY_SIZE: raise AssertionError(len(ent))
        entries.append(ent); blobs.append(d['stored']); cursor+=len(d['stored'])
    header=struct.pack('<8sIIIIII',MAGIC,VERSION,len(drivers),HEADER_SIZE,ENTRY_SIZE*len(drivers),data_offset,cursor)
    output=Path(a.output); output.parent.mkdir(parents=True,exist_ok=True); output.write_bytes(header+b''.join(entries)+b''.join(blobs))
    print(f'RootOS Driver Pack: {output} ({len(drivers)} drivers, {cursor} bytes)')
    for d in drivers:
        alg='rle8' if d['compression'] else 'raw'; print(f"  {d['name']}: {len(d['raw'])} -> {len(d['stored'])} ({alg})")
if __name__=='__main__': main()
