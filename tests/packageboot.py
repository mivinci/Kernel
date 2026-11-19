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
import initcfg
from elf import plan as elfplan
from run import install_stop_handlers, run_guard


def cases(arch,directory):
    machine=bootpkg.MACHINES[arch]
    init=ROOT/'build'/arch/'sbin/init.elf'
    raw=init.read_bytes()
    driver=ROOT/'build'/arch/'sbin/serial.elf'
    basic=bootpkg.pack([('sbin/init',raw),('sbin/serial',driver.read_bytes())],machine)
    other=(ROOT/'build'/arch/'sdk/runtimeuser.elf').read_bytes()
    yield 'selected-index',bootpkg.pack([('bin/fixture',other),('sbin/init',raw),('sbin/serial',driver.read_bytes())],machine),init,None,None
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
    serial=ROOT/'build'/arch/'sdk/serialservice.elf'
    yield 'serial-service',bootpkg.pack([('sbin/init',serial.read_bytes())],machine),serial,None,42
    yield 'serial-timeout',bootpkg.pack([('tests/serial-timeout',serial.read_bytes())],machine,init='tests/serial-timeout'),serial,None,42
    manager=ROOT/'build'/arch/'sdk/initservices.elf'
    no_ready=ROOT/'build'/arch/'sdk/noready.elf'
    for fixture,name,image in (
            ('manager-live','tests/init-services',driver),
            ('manager-timeout','tests/init-timeout',no_ready),
            ('manager-missing','tests/init-missing',None),
            ('manager-budget','tests/init-budget',driver)):
        entries=[(name,manager.read_bytes())]
        if image:entries.append(('sbin/serial',image.read_bytes()))
        yield fixture,bootpkg.pack(entries,machine,init=name),manager,None,42
    yield 'production-missing-serial',bootpkg.pack([('sbin/init',raw)],machine),init,None,110
    yield 'production-serial-timeout',bootpkg.pack([('sbin/init',raw),('sbin/serial',no_ready.read_bytes())],machine),init,None,110
    yield 'production-running-exit',basic,init,None,110
    original=(ROOT/'build'/arch/'sdk/init.cfg').read_bytes()
    if raw.count(original)!=1:raise ValueError('production init configuration is not unique in its ELF')
    offset=raw.index(original)
    configuration=json.loads((ROOT/'config/boot.json').read_text())
    configuration['services'][0]['memory_pages']=len(elfplan(driver.read_bytes(),machine,4096)[1])+8
    budget=initcfg.generate(configuration,{'sbin/serial':driver},machine)
    for fixture,payload,code in (
            ('production-load-budget',budget,110),
            ('production-bad-configuration',bytes([original[0]^1])+original[1:],105)):
        changed=raw[:offset]+payload+raw[offset+len(original):]
        artifact=directory/(fixture+'.elf');artifact.write_bytes(changed)
        yield fixture,bootpkg.pack([('sbin/init',changed),('sbin/serial',driver.read_bytes())],machine),artifact,None,code


@run_guard(ROOT)
def main():
    install_stop_handlers()
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arch',choices=bootpkg.MACHINES,required=True)
    parser.add_argument('--firmware')
    parser.add_argument('--qemu')
    parser.add_argument('--only',help='run one named package fixture')
    parser.add_argument('--uart-trace',action='store_true',help='record PL011 device events for diagnosis')
    args=parser.parse_args()
    directory=ROOT/'build'/args.arch/'packageboot'/('suite-'+uuid.uuid4().hex)
    directory.mkdir(parents=True)
    records=[]
    for name,data,artifact,error,exitcode in cases(args.arch,directory):
        if args.only and name!=args.only:continue
        run=directory/name;run.mkdir();path=run/'input.pkg'
        if data is not None:path.write_bytes(data)
        options=argparse.Namespace(arch=args.arch,case='normal',ram=256,qemu=args.qemu,
            firmware=args.firmware,trace=False,timeout=30 if name.startswith(('serial-','manager-','production-')) else 15,virtio=False,initrd=path if data is not None else None,
            bootpkg_error=error,init_elf=artifact,init_exit=exitcode)
        options.serial_input = name=='serial-service'
        options.serial_probe = name.startswith('serial-')
        options.manager_probe = name.startswith('manager-')
        options.production_init = name.startswith('production-')
        options.serial_failure = name=='production-running-exit'
        options.uart_trace = args.uart_trace
        record=dict(arch=args.arch,case='normal',fixture=name,ok=False)
        try:
            smoke.exercise(options,run,record)
            if name.startswith('production-'):
                proof=record['production_init'];service=proof['service']
                if record['retained_init']['charged']!=record['retained_init']['initial_charged']:
                    raise ValueError('production failure did not restore initial PID1 charge')
                if name!='production-bad-configuration':
                    expected={
                        'production-missing-serial':(1,-1,0,0),
                        'production-serial-timeout':(7,-21,1,1),
                        'production-load-budget':(5,-8,0,0),
                        'production-running-exit':(8,-14,1,1),
                    }[name]
                    actual=(service[2],struct.unpack('<q',struct.pack('<Q',service[8]))[0],proof['state'][6],int(bool(service[15]&4)))
                    if actual!=expected or not proof['cleanup_before_fatal']:
                        raise ValueError('production failure did not follow its exact cleanup path')
                    if name=='production-serial-timeout' and service[9:11]!=[1,265]:
                        raise ValueError('READY timeout did not reap actual forced termination')
                    if name=='production-running-exit' and (service[9:11]!=[1,121] or not record.get('production_ring_fault',{}).get('blocked_before_fault')):
                        raise ValueError('live serial protocol failure was not supervised and reaped')
                elif any(service):
                    raise ValueError('invalid configuration constructed a service')
            record['ok']=True
        except Exception as failure:record['error']=str(failure)
        (run/'result.json').write_text(json.dumps(record,indent=2)+'\n')
        records.append(dict(fixture=name,ok=record['ok'],result=str(run/'result.json')))
        print('%s %s/packageboot/%s: %s'%('OK' if record['ok'] else 'FAILED',args.arch,name,run),flush=True)
        if not record['ok']:print(record['error'],file=sys.stderr)
    (directory/'suite.json').write_text(json.dumps(records,indent=2)+'\n')
    if not records:parser.error('unknown package fixture')
    return 0 if all(x['ok'] for x in records) else 1


if __name__=='__main__':sys.exit(main())
