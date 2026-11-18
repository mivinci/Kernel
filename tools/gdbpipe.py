"""Bounded read-only register access through QEMU's GDB named pipes."""
import os
from pathlib import Path
import select
import time
import xml.etree.ElementTree as ET

class GdbError(ValueError):
    pass

class GdbPipe:
    def __init__(self,base,deadline):
        self.base=Path(base);self.deadline=deadline;self.buffer=bytearray()
        self.paths=[]
        self.fds=[];self.descriptions={};self.regs={};self.next_register=0
        try:
            for suffix in ('.in','.out'):
                path=Path(str(base)+suffix)
                os.mkfifo(path)
                self.paths.append(path)
                self.fds.append(os.open(path,os.O_RDWR|os.O_NONBLOCK))
        except BaseException:
            self.close();raise

    @property
    def argument(self):return 'pipe:'+str(self.base)

    def close(self):
        for fd in self.fds:os.close(fd)
        self.fds=[]
        for path in self.paths:
            if path.exists():path.unlink()
        self.paths=[]

    def _wait(self,fd,write=False):
        remaining=self.deadline-time.monotonic()
        if remaining<=0:raise GdbError('GDB pipe deadline expired')
        ready=select.select([] if write else [fd],[fd] if write else [],[],remaining)
        if not ready[1 if write else 0]:raise GdbError('GDB pipe timed out')

    def _write(self,data):
        while data:
            self._wait(self.fds[0],True)
            try:count=os.write(self.fds[0],data)
            except BlockingIOError:continue
            if count<=0:raise GdbError('GDB pipe short write')
            data=data[count:]

    def _packet(self):
        while True:
            start=self.buffer.find(b'$');end=self.buffer.find(b'#',max(start,0))
            if start>=0 and end>=0 and len(self.buffer)>=end+3:
                body=bytes(self.buffer[start+1:end]);checksum=bytes(self.buffer[end+1:end+3])
                del self.buffer[:end+3]
                try:expected=int(checksum,16)
                except ValueError:raise GdbError('invalid GDB checksum')
                if sum(body)&255!=expected:raise GdbError('GDB response checksum mismatch')
                self._write(b'+');out=bytearray();i=0
                while i<len(body):
                    byte=body[i];i+=1
                    if byte==ord('}'):
                        if i==len(body):raise GdbError('truncated GDB escape')
                        out.append(body[i]^0x20);i+=1
                    elif byte==ord('*'):
                        if not out or i==len(body) or body[i]<29:raise GdbError('invalid GDB run length')
                        out.extend(bytes([out[-1]])*(body[i]-29));i+=1
                    else:out.append(byte)
                    if len(out)>1024*1024:raise GdbError('oversized decoded GDB response')
                return bytes(out)
            if b'-' in self.buffer and start<0:raise GdbError('GDB rejected request checksum')
            if len(self.buffer)>1024*1024:raise GdbError('oversized GDB response')
            self._wait(self.fds[1])
            try:chunk=os.read(self.fds[1],65536)
            except BlockingIOError:continue
            if not chunk:raise GdbError('GDB pipe closed')
            self.buffer.extend(chunk)

    def request(self,text):
        payload=text.encode('ascii')
        self._write(b'$'+payload+b'#'+('%02x'%(sum(payload)&255)).encode())
        return self._packet()

    def _feature(self,name):
        if name in self.descriptions:return
        if len(self.descriptions)>=16:raise GdbError('too many GDB feature descriptions')
        data=bytearray()
        while True:
            part=self.request('qXfer:features:read:%s:%x,fff'%(name,len(data)))
            if not part or part[:1] not in (b'l',b'm') or (part[:1]==b'm' and len(part)==1):
                raise GdbError('invalid GDB feature response')
            data.extend(part[1:])
            if len(data)>512*1024:raise GdbError('oversized GDB feature description')
            if part[:1]==b'l':break
        text=data.decode('utf-8');self.descriptions[name]=text
        if '<target>' in text:text=text.replace('<target>','<target xmlns:xi="http://www.w3.org/2001/XInclude">',1)
        try:root=ET.fromstring(text)
        except ET.ParseError as error:raise GdbError('invalid GDB feature XML: '+str(error))
        for child in root:
            if child.tag.endswith('include'):
                self._feature(child.attrib['href'])
            elif child.tag=='reg':
                name=child.attrib['name'];number=int(child.attrib.get('regnum',str(self.next_register)))
                bits=int(child.attrib['bitsize'])
                if name in self.regs or bits<=0 or bits>65536 or bits%8:raise GdbError('invalid GDB register description')
                self.regs[name]=(number,bits)
                self.next_register=number+1

    def registers(self,names):
        if not self.descriptions:
            if b'qXfer:features:read+' not in self.request('qSupported:xmlRegisters=aarch64;qXfer:features:read+'):
                raise GdbError('GDB XML register descriptions unavailable')
            self._feature('target.xml')
        out={}
        for name in names:
            if name not in self.regs:raise GdbError('GDB register missing: '+name)
            number,bits=self.regs[name];raw=self.request('p%x'%number)
            try:data=bytes.fromhex(raw.decode('ascii'))
            except ValueError:raise GdbError('GDB register unavailable: '+name)
            if len(data)*8!=bits:raise GdbError('GDB register width mismatch: '+name)
            out[name]=int.from_bytes(data,'little')
        return out
