#!/usr/bin/env python3
"""Prove the register auditor rejects a real kernel with I-cache enable omitted.

This is a control-state counterexample, not a model of noncoherent DMA or proof
that QEMU implements stale physical cache contents.
"""
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time
import uuid

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from build import digest
from gdbpipe import GdbPipe
from smoke import Qmp,SmokeError,symbols,check_arm64_cache
from run import _stop_group,_group_exists,install_stop_handlers

def main():
    install_stop_handlers();artifact=ROOT/'build/arm64/normal/kernel.elf';baseline=None
    for directory in sorted((ROOT/'build/arm64/smoke').iterdir(),key=lambda p:p.stat().st_mtime,reverse=True):
        path=directory/'result.json'
        if not path.exists():continue
        record=json.loads(path.read_text())
        if (record.get('ok') and record.get('case')=='normal' and record.get('ram_mb')==256 and
                record.get('cache_state',{}).get('independent_gdb_read') and
                record['build']['artifact_sha256']==digest(artifact)):
            baseline=directory;break
    if baseline is None:raise SmokeError('run current ARM64 normal smoke with cache audit first')
    build=record['build'];original=Path(build['boot_image']['path']);before=digest(original)
    names=symbols(subprocess.check_output([build['tools']['readelf']['path'],'--symbols',str(artifact)],text=True))
    image=bytearray(original.read_bytes());offset=names['hal_vm_enable_icache']-names['__kernel_start']
    if image[offset:offset+4]!=bytes.fromhex('080174b2'):
        raise SmokeError('counterexample target is not ORR x8,x8,#0x1000')
    image[offset:offset+4]=struct.pack('<I',0xd503201f) # NOP only this enable.
    directory=ROOT/'build/arm64/cacheaudit'/('suite-'+uuid.uuid4().hex);directory.mkdir(parents=True)
    fixture=directory/'Image';fixture.write_bytes(image);serial=directory/'serial.log'
    command=list(record['argv']);index=command.index('-gdb');del command[index:index+2]
    command[command.index('-kernel')+1]=str(fixture)
    command[command.index('-serial')+1]='file:'+str(serial)
    command[command.index('-qmp')+1]='stdio'
    deadline=time.monotonic()+15
    with tempfile.TemporaryDirectory(prefix='kernel-cache-counter-') as temporary:
        gdb=GdbPipe(Path(temporary)/'gdb',deadline);command+=['-gdb',gdb.argument]
        (directory/'launch.json').write_text(json.dumps(command,indent=2)+'\n')
        with (directory/'qemu.log').open('wb') as log:
            process=None;qmp=None
            try:
                process=subprocess.Popen(command,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=log,start_new_session=True)
                qmp=Qmp(None,deadline,process);qmp.command('qmp_capabilities')
                entry=gdb.registers(['SCTLR_EL1','TCR_EL1','MAIR_EL1']);check_arm64_cache(entry,False)
                if entry['SCTLR_EL1']&0x1000:raise SmokeError('counterexample requires reset I-cache disabled')
                qmp.command('cont')
                while time.monotonic()<deadline:
                    text=serial.read_text() if serial.exists() else ''
                    if 'INIT pid=0000000000000001 image=0000000000000001 state=0000000000000006\n' in text:break
                    if 'PANIC ' in text:raise SmokeError('counterexample did not run production PID1')
                    time.sleep(.02)
                else:raise SmokeError('cache counterexample startup timed out')
                qmp.command('stop');values=gdb.registers(['SCTLR_EL1','TCR_EL1','MAIR_EL1'])
                if values['SCTLR_EL1']&0x1005!=5:raise SmokeError('counterexample did not omit only I-cache enable')
                try:check_arm64_cache(values,True)
                except SmokeError as error:rejection=str(error)
                else:raise SmokeError('cache auditor accepted disabled I-cache')
                qmp.command('quit');process.wait(timeout=max(.01,deadline-time.monotonic()))
                if process.returncode or _group_exists(process.pid):raise SmokeError('QEMU counterexample cleanup failed')
            finally:
                if qmp:qmp.close()
                if process:_stop_group(process)
                gdb.close()
    if digest(original)!=before:raise SmokeError('counterexample changed production Image')
    result=dict(arch='arm64',case='cacheaudit/missing-icache',ok=True,guest_executed=True,
                build=build,baseline=str(baseline),qmp_transport='stdio',cache_entry=entry,
                observed_registers=values,auditor_rejection=rejection,control_counterexample=True,
                fixture_patch=dict(offset=offset,original='080174b2',replacement='1f2003d5',sha256=digest(fixture)),
                production_image_unchanged=True,returncode=0)
    (directory/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print('OK arm64/cacheaudit/missing-icache: '+str(directory),flush=True)
    return 0

if __name__=='__main__':sys.exit(main())
