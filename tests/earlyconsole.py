#!/usr/bin/env python3
"""Exercise early console failures using the normal image's actual firmware DTB."""
import argparse
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import time
import uuid

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from smoke import Qmp,SmokeError,symbols
from build import digest
from run import _stop_group,install_stop_handlers,run_guard
from fdt import properties

def run(arch,baseline,case,directory):
    original=(baseline/'runtime.dtb').read_bytes();data=bytearray(original);props=properties(data)
    # QEMU grows custom trees again. Drop only unused trailing baseline padding.
    header=struct.unpack_from('>10I',data);end=max(header[2]+header[9],header[3]+header[8])
    reserve=header[4]
    while any(struct.unpack_from('>QQ',data,reserve)):reserve+=16
    end=max(end,reserve+16);data=data[:end];struct.pack_into('>I',data,4,len(data))
    at,length=props['/chosen','stdout-path']
    path=bytes(data[at:at+length]).split(b'\0')[0].split(b':')[0].decode()
    if not path.startswith('/'):
        offset,size=props['/aliases',path];path=bytes(data[offset:offset+size]).rstrip(b'\0').decode()
    if case=='bad-selection':data[at]=ord('!')
    else:
        offset,size=props[path,'interrupts'];data[offset:offset+size]=b'\xff'*size
    dtb=directory/'input.dtb';dtb.write_bytes(data)
    record=json.loads((baseline/'result.json').read_text());command=list(record['argv'])
    if '-gdb' in command:
        index=command.index('-gdb');del command[index:index+2]
    if '-chardev' in command:
        index=command.index('-chardev');del command[index:index+2]
    serial=directory/'serial.log'
    command[command.index('-serial')+1]='file:'+str(serial)
    command[command.index('-qmp')+1]='stdio'
    command+=['-dtb',str(dtb)]
    names=symbols(subprocess.check_output([record['build']['tools']['readelf']['path'],'--symbols',
                                         record['build']['artifact']],text=True))
    deadline=time.monotonic()+15;qmp=None
    (directory/'launch.json').write_text(json.dumps(command,indent=2)+'\n')
    with (directory/'qemu.log').open('wb') as output:
        proc=subprocess.Popen(command,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=output,start_new_session=True)
        try:
            qmp=Qmp(None,deadline,proc);qmp.command('qmp_capabilities');qmp.command('cont')
            while time.monotonic()<deadline:
                time.sleep(.05);qmp.command('stop')
                halted=[]
                for cpu in qmp.command('query-cpus-fast'):
                    registers=qmp.command('human-monitor-command',{'command-line':'info registers','cpu-index':cpu['cpu-index']})
                    match=re.search(r'\bpc\s+([0-9a-f]+)',registers,re.I) if arch=='riscv' else re.search(r'\bPC=([0-9a-f]+)',registers)
                    if match and names['hal_halt']<=int(match[1],16)<names['hal_halt']+16:
                        halted.append((cpu['cpu-index'],registers))
                if len(halted)==1:
                    selected_cpu,registers=halted[0];break
                if len(halted)>1:raise SmokeError('multiple CPUs entered the kernel cold halt')
                qmp.command('cont')
            else:raise SmokeError('early failure did not reach physical halt')
            (directory/'registers.txt').write_text(registers)
            text=serial.read_text();lines=text.splitlines()
            boot=[x for x in lines if x.startswith('BOOT ')]
            if any(x.startswith(('LAYOUT ','META ','PAGES ','VM ','TRAP ','INIT ')) for x in lines):
                raise SmokeError('early failure progressed beyond device discovery')
            if case=='bad-selection':
                if boot or any(x.startswith(('DT ','PANIC ')) for x in lines):
                    raise SmokeError('invalid selection used a fallback console')
            elif len(boot)!=1 or [x for x in lines if x.startswith('PANIC ')]!=['PANIC dtb parse'] or not any(x.startswith('DT error=') for x in lines):
                raise SmokeError('late discovery failure lost early UART diagnostic')
            qmp.command('quit');proc.wait(timeout=max(.01,deadline-time.monotonic()))
            if proc.returncode:raise SmokeError('QEMU failed')
            result=dict(arch=arch,case='earlyconsole/'+case,ok=True,guest_executed=True,
                        build=record['build'],qmp_transport='stdio',baseline=str(baseline),
                        input_sha256=digest(dtb),physical_halt=True,boot_cpu_index=selected_cpu,serial_checked=True,argv=command,returncode=0)
            (directory/'result.json').write_text(json.dumps(result,indent=2)+'\n')
        finally:
            if qmp:qmp.close()
            _stop_group(proc)

@run_guard(ROOT)
def main():
    install_stop_handlers();parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arch',choices=('arm64','riscv'),required=True);args=parser.parse_args()
    artifact=ROOT/'build'/args.arch/'normal/kernel.elf'
    baseline=None
    for candidate in sorted((ROOT/'build'/args.arch/'smoke').iterdir(),key=lambda p:p.stat().st_mtime,reverse=True):
        path=candidate/'result.json'
        if not path.exists():continue
        record=json.loads(path.read_text())
        if (record.get('ok') and record.get('case')=='normal' and record.get('ram_mb')==256 and
                record.get('package_init',{}).get('catalog_checked') and
                record['build']['artifact_sha256']==digest(artifact)):
            baseline=candidate;break
    if baseline is None:raise SmokeError('run the current normal smoke first')
    suite=ROOT/'build'/args.arch/'earlyconsole'/('suite-'+uuid.uuid4().hex);suite.mkdir(parents=True)
    for case in ('late-discovery','bad-selection'):
        directory=suite/case;directory.mkdir();run(args.arch,baseline,case,directory)
        print('OK %s/earlyconsole/%s: %s'%(args.arch,case,directory),flush=True)
    return 0

if __name__=='__main__':sys.exit(main())
