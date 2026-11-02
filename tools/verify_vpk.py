"""Independent structural release gate. Inspects SELF, SFO and imports; never executes them."""
import argparse
import hashlib
import json
import struct
import zipfile
import zlib
from pathlib import Path

EXPECTED = {'eboot.bin','sce_sys/param.sfo','sce_sys/icon0.png',
    'sce_sys/livearea/contents/template.xml','sce_sys/livearea/contents/startup.png',
    'sce_sys/livearea/contents/bg0.png'}
UI_NAMES = 'background logo library series movies folder play pause next previous volume subtitles settings search sort favorite check back image'.split()
EXPECTED.update('assets/ui/'+name+'.png' for name in UI_NAMES)

def require(condition, message='Invalid package structure'):
    if not condition:
        raise ValueError(message)

def inspect(path):
    with zipfile.ZipFile(path) as archive:
        require(archive.testzip() is None, 'ZIP corruption')
        names=archive.namelist()
        require(len(names)==len(set(names)) and set(names)==EXPECTED, 'Unexpected package files')
        for name in UI_NAMES:
            png=archive.read('assets/ui/'+name+'.png')
            require(33<=len(png)<=1024*1024 and png[:8]==b'\x89PNG\r\n\x1a\n', 'Invalid UI PNG')
            expected=(960,544) if name=='background' else (480,254) if name=='logo' else (64,64)
            require(struct.unpack_from('>II',png,16)==expected and png[24]==8,'Unexpected UI dimensions')
        sfo=archive.read('sce_sys/param.sfo')
        require(sfo[:4]==b'\0PSF')
        _,_,keys,data,count=struct.unpack_from('<5I',sfo)
        values={}
        for i in range(count):
            keyoff,fmt,length,maxlength,offset=struct.unpack_from('<HHIII',sfo,20+16*i)
            key=sfo[keys+keyoff:].split(b'\0')[0].decode('ascii')
            raw=sfo[data+offset:data+offset+length]
            values[key]=raw.rstrip(b'\0').decode('utf-8') if fmt==0x204 else raw.hex()
        require(values['TITLE_ID']=='VMPH00001', 'Must not overwrite original app')
        binary=archive.read('eboot.bin')
    require(binary[:4]==b'SCE\0')
    appoff,elfoff,phoff,segoff=[struct.unpack_from('<Q',binary,p)[0] for p in (56,64,72,88)]
    auth=struct.unpack_from('<Q',binary,appoff)[0]
    require(auth==0x2f00000000000002, 'Extended-permission SELF rejected')
    eh=struct.unpack_from('<16sHHIIIIIHHHHHH',binary,elfoff)
    require(eh[0][:4]==b'\x7fELF' and eh[2]==40)
    segments=[]
    for i in range(eh[10]):
        ph=struct.unpack_from('<8I',binary,phoff+i*eh[9])
        offset,length,comp,enc=struct.unpack_from('<4Q',binary,segoff+32*i)
        require(enc==2, 'Encrypted segment cannot be checked')
        raw=binary[offset:offset+length]
        raw=zlib.decompress(raw) if comp==2 and length else raw
        require(len(raw)==ph[4])
        segments.append((ph,raw))
    def readva(va,n):
        for ph,raw in segments:
            if ph[0]==1 and ph[2]<=va and va+n<=ph[2]+len(raw):
                start=va-ph[2]
                return raw[start:start+n]
        raise ValueError(f'Unmapped virtual address {va:x}')
    segidx=eh[4]>>30; modoff=eh[4]&0x3fffffff
    ph,raw=segments[segidx]
    mod=raw[modoff:modoff+92]
    pos,end=struct.unpack_from('<II',mod,44)
    imports=[]
    while pos<end:
        entry=readva(ph[2]+pos,4)
        size=struct.unpack_from('<H',entry)[0]
        require(size in (0x24,0x34))
        entry=readva(ph[2]+pos,size)
        functions=struct.unpack_from('<H',entry,6)[0]
        libptr=struct.unpack_from('<I',entry,20 if size==0x34 else 16)[0]
        nidptr=struct.unpack_from('<I',entry,28 if size==0x34 else 20)[0]
        name=bytearray()
        for i in range(128):
            c=readva(libptr+i,1)
            if c==b'\0': break
            name+=c
        else: raise ValueError('Unterminated import library')
        name=name.decode('ascii')
        require(not any(word in name.lower() for word in ('net','http','ssl','taihen','vshbridge','promoter','shellsvc','forkernel','fordriver')),name)
        nids=struct.unpack('<'+'I'*functions,readva(nidptr,4*functions)) if functions else []
        imports.append({'library':name,'function_nids':[f'{nid:08X}' for nid in nids]})
        pos+=size
    require(pos==end)
    return {'status':'PASS','vpk':path.name,'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
        'eboot_sha256':hashlib.sha256(binary).hexdigest(),'size':path.stat().st_size,
        'title_id':values['TITLE_ID'],'authid':f'{auth:016X}','entries':sorted(EXPECTED),'imports':imports,
        'limitations':'Structural and import checks only; no hardware execution or proof of absence of vulnerabilities.'}

if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('vpk',type=Path)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    result=inspect(args.vpk)
    payload=json.dumps(result,indent=2)
    if args.output: args.output.write_text(payload,encoding='utf-8')
    print(payload)
