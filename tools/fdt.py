"""Bounded host edits of actual firmware DTBs; no guest address authority."""
import struct

def inspect(data):
    if len(data)<40:raise ValueError('truncated FDT header')
    header=struct.unpack_from('>10I',data)
    magic,total,begin,strings,reserve,version,compatible,boot,stringbytes,size=header
    if (magic!=0xd00dfeed or not 40<=total<=min(len(data),0x200000) or version<17 or compatible>17 or
            begin<40 or strings<40 or reserve<40 or reserve%8 or begin%4 or
            size>total-begin or stringbytes>total-strings):raise ValueError('invalid FDT bounds')
    if begin<strings+stringbytes and strings<begin+size:raise ValueError('overlapping FDT blocks')
    at=begin;limit=begin+size;stack=[];props={};ends={};nodes=0;seen=set()
    def text(start,end):
        stop=data.find(b'\0',start,end)
        if stop<0:raise ValueError('unterminated FDT name')
        return data[start:stop].decode('utf-8'),stop
    while at<limit:
        if at+4>limit:raise ValueError('truncated FDT token')
        offset=at;tag=struct.unpack_from('>I',data,at)[0];at+=4
        if tag==1:
            name,end=text(at,limit);at=(end+4)&~3;stack.append(name);nodes+=1
            path='/'+'/'.join(stack[1:])
            if path in seen:raise ValueError('duplicate FDT node')
            seen.add(path)
            if len(stack)>64 or nodes>4096:raise ValueError('FDT host node capacity')
        elif tag==2:
            if not stack:raise ValueError('unbalanced FDT end')
            ends['/'+'/'.join(stack[1:])]=offset;stack.pop()
        elif tag==3:
            if not stack or at+8>limit:raise ValueError('truncated FDT property')
            length,name=struct.unpack_from('>II',data,at);at+=8
            if name>=stringbytes or length>limit-at:raise ValueError('FDT property bounds')
            key,_=text(strings+name,strings+stringbytes);path='/'+'/'.join(stack[1:])
            if (path,key) in props:raise ValueError('duplicate FDT property')
            props[path,key]=(at,length);at=(at+length+3)&~3
            if len(props)>32768:raise ValueError('FDT host property capacity')
        elif tag==4:pass
        elif tag==9:
            if stack:raise ValueError('incomplete FDT tree')
            return header,props,ends
        else:raise ValueError('invalid FDT token')
    raise ValueError('missing FDT end')

def properties(data):return inspect(data)[1]

def cold_boot_hart(data,hart):
    header,props,ends=inspect(data);phandle=None
    for (path,key),(at,size) in props.items():
        if key!='device_type' or data[at:at+size]!=b'cpu\0' or path.rsplit('/',1)[0]!='/cpus':continue
        reg,n=props[path,'reg']
        if n not in (4,8) or int.from_bytes(data[reg:reg+n],'big')!=hart:continue
        status=props.get((path,'status'))
        if status and data[status[0]:status[0]+status[1]] not in (b'ok\0',b'okay\0'):continue
        address,width=props[path,'phandle']
        if width!=4 or phandle is not None:raise ValueError('ambiguous cold boot CPU')
        phandle=struct.unpack_from('>I',data,address)[0]
    if not phandle or '/chosen' not in ends:raise ValueError('missing enabled cold boot CPU or chosen node')
    for (path,key),(at,size) in props.items():
        if path.rsplit('/',1)[0]=='/chosen' and key=='compatible' and b'opensbi,config' in data[at:at+size].split(b'\0'):
            raise ValueError('refusing to replace existing OpenSBI config')
    strings=bytearray(data[header[3]:header[3]+header[8]])
    def string(name):
        encoded=name.encode()+b'\0';at=0
        while at<len(strings):
            end=strings.index(0,at)+1
            if strings[at:end]==encoded:return at
            at=end
        offset=len(strings);strings.extend(encoded);return offset
    def padded(value):return value+bytes(-len(value)%4)
    def prop(name,value):return struct.pack('>3I',3,len(value),string(name))+padded(value)
    child=struct.pack('>I',1)+padded(b'opensbi-config\0')
    child+=prop('compatible',b'opensbi,config\0')+prop('cold-boot-harts',struct.pack('>I',phandle))+struct.pack('>I',2)
    structure=data[header[2]:header[2]+header[9]];offset=ends['/chosen']-header[2]
    structure=structure[:offset]+child+structure[offset:]
    reserve=header[4]
    while True:
        if reserve+16>header[1]:raise ValueError('truncated FDT reservation map')
        pair=struct.unpack_from('>QQ',data,reserve);reserve+=16
        if pair==(0,0):break
    reservations=data[header[4]:reserve];begin=(40+len(reservations)+7)&~7
    stringoff=begin+len(structure);used=stringoff+len(strings)
    total=max(header[1],(used+4096+4095)&~4095)
    if total>0x200000:raise ValueError('cold boot FDT exceeds input window')
    output=bytearray(struct.pack('>10I',0xd00dfeed,total,begin,stringoff,40,header[5],header[6],header[7],len(strings),len(structure)))
    output.extend(reservations);output.extend(bytes(begin-len(output)));output.extend(structure);output.extend(strings)
    output.extend(bytes(total-len(output)));inspect(output)
    return bytes(output),phandle
