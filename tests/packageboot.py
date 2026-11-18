#!/usr/bin/env python3
"""Production normal image: selected ELF, full-page source and fatal startup paths."""
import argparse
import json
from pathlib import Path
import struct
import sys
import uuid

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import bootpkg
import smoke
from run import install_stop_handlers


def cases(arch):
    machine=bootpkg.MACHINES[arch]
    init=ROOT/'build'/arch/'sbin/init.elf'
    raw=init.read_bytes()
    basic=bootpkg.pack([('sbin/init',raw)],machine)
    other=(ROOT/'build'/arch/'sdk/runtimeuser.elf').read_bytes()
    yield 'selected-index',bootpkg.pack([('bin/fixture',other),('sbin/init',raw)],machine),init,None,None
    aligned=bytearray(basic);aligned.extend(bytes(-len(aligned)%4096))
    struct.pack_into('<Q',aligned,16,len(aligned));struct.pack_into('<Q',aligned,56,bootpkg.checksum(aligned))
    yield 'whole-pages',aligned,init,None,None
    yield 'missing',None,init,'missing',None
    checksum=bytearray(basic);checksum[-1]^=1
    yield 'checksum',checksum,init,'checksum',None
    header=bootpkg.HEADER.unpack_from(basic);row=bootpkg.ITEM.unpack_from(basic,header[5])
    bad=bytearray(basic);struct.pack_into('<Q',bad,row[4],0)
    struct.pack_into('<Q',bad,56,bootpkg.checksum(bad))
    yield 'load',bad,init,'load',None
    exiting=ROOT/'build'/arch/'sdk/packageexit.elf'
    yield 'native-exit',bootpkg.pack([('sbin/init',exiting.read_bytes())],machine),exiting,None,42
    authority=ROOT/'build'/arch/'sdk/serviceauth.elf'
    yield 'service-authority',bootpkg.pack([('sbin/init',authority.read_bytes())],machine),authority,None,42


def main():
    install_stop_handlers()
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arch',choices=bootpkg.MACHINES,required=True)
    parser.add_argument('--firmware')
    parser.add_argument('--qemu')
    args=parser.parse_args()
    directory=ROOT/'build'/args.arch/'packageboot'/('suite-'+uuid.uuid4().hex)
    directory.mkdir(parents=True)
    records=[]
    for name,data,artifact,error,exitcode in cases(args.arch):
        run=directory/name;run.mkdir();path=run/'input.pkg'
        if data is not None:path.write_bytes(data)
        options=argparse.Namespace(arch=args.arch,case='normal',ram=256,qemu=args.qemu,
            firmware=args.firmware,trace=False,timeout=15,virtio=False,initrd=path if data is not None else None,
            bootpkg_error=error,init_elf=artifact,init_exit=exitcode)
        record=dict(arch=args.arch,case='normal',fixture=name,ok=False)
        try:
            smoke.exercise(options,run,record);record['ok']=True
        except Exception as failure:record['error']=str(failure)
        (run/'result.json').write_text(json.dumps(record,indent=2)+'\n')
        records.append(dict(fixture=name,ok=record['ok'],result=str(run/'result.json')))
        print('%s %s/packageboot/%s: %s'%('OK' if record['ok'] else 'FAILED',args.arch,name,run),flush=True)
        if not record['ok']:print(record['error'],file=sys.stderr)
    (directory/'suite.json').write_text(json.dumps(records,indent=2)+'\n')
    return 0 if all(x['ok'] for x in records) else 1


if __name__=='__main__':sys.exit(main())
