# Copyright (c) 2025-2026 gugueh (Kirv Studio). All rights reserved.
import importlib.util
from pathlib import Path
import struct
import sys
import tempfile
import zipfile

root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('gate',root/'tools/verify_vpk.py')
gate=importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)
original=Path(sys.argv[1])
gate.inspect(original)
with zipfile.ZipFile(original) as archive:
    files={name:archive.read(name) for name in archive.namelist()}
scenarios=[]
unsafe=dict(files)
binary=bytearray(files['eboot.bin'])
appoff=struct.unpack_from('<Q',binary,56)[0]
struct.pack_into('<Q',binary,appoff,0x2f00000000000001)
unsafe['eboot.bin']=bytes(binary)
scenarios.append(('extended_permissions',unsafe))
extra=dict(files); extra['modules/hidden.suprx']=b'not permitted'
scenarios.append(('unexpected_module',extra))
wrong=dict(files); wrong['sce_sys/param.sfo']=files['sce_sys/param.sfo'].replace(b'VMPH00001',b'SVMP00001')
scenarios.append(('original_title_id',wrong))
temp_root=original.resolve().parent
with tempfile.TemporaryDirectory(dir=temp_root,prefix='gate-test-') as folder:
    if not Path(folder).resolve().is_relative_to(temp_root):
        raise RuntimeError('Unexpected temporary directory')
    for name,entries in scenarios:
        path=Path(folder)/(name+'.vpk')
        with zipfile.ZipFile(path,'w') as archive:
            for key,value in entries.items(): archive.writestr(key,value)
        try:
            gate.inspect(path)
        except ValueError:
            continue
        raise RuntimeError('Verifier accepted '+name)
print('PASS: valid package accepted; 3 altered packages rejected (also under python -O).')
