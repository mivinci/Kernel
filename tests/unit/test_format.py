import ctypes as C
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]

class FormatTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();folder=Path(cls.temp.name);cls.path=folder/'format.so'
        shim=folder/'forward.c'
        shim.write_text('''#include <stdio.h>
#include <errno.h>
#include <unistd.h>
unsigned char captured[1024];
size_t used, stop;
void reset(size_t limit) { used=0; stop=limit; }
ssize_t write(int fd,const void *data,size_t count) {
 size_t i;
 if (fd != 3) { errno=EBADF; return -1; }
 if (used == stop && count) { errno=EINTR; return -1; }
 if (count > 7) count=7;
 if (count > stop-used) count=stop-used;
 for (i=0;i<count;i++) captured[used++]=((const unsigned char *)data)[i];
 return (ssize_t)count;
}
int testv(char *buffer,size_t size,const char *format,...) {
 va_list args,copy;int result,needed;
 va_start(args,format);__builtin_va_copy(copy,args);
 result=vsnprintf(buffer,size,format,args);
 needed=vsnprintf(NULL,0,format,copy);
 va_end(copy);va_end(args);
 return result==needed ? result : -999;
}
''')
        command=['clang','-std=c89','-O2','-ffreestanding','-fno-builtin','-Wall','-Wextra','-Werror','-shared','-fPIC',
                 '-I'+str(ROOT/'lib/libc/include'),'-I'+str(ROOT/'include'),'-Dsnprintf=user_snprintf',
                 '-Dvsnprintf=user_vsnprintf','-D__errno_location=user_errno_location','-Dstrerror=user_strerror',
                 '-Ddprintf=user_dprintf','-Dvdprintf=user_vdprintf','-Dwrite=user_write',
                 str(ROOT/'lib/libc/format.c'),str(ROOT/'lib/libc/formatfd.c'),
                 str(ROOT/'lib/libc/errno.c'),str(shim),'-o',str(cls.path)]
        if sys.platform=='darwin':
            sdk=subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip()
            command+=['-isysroot',sdk,'-Wl,-syslibroot,'+sdk]
        subprocess.run(command,check=True,capture_output=True)
        cls.lib=C.CDLL(str(cls.path));cls.native=C.CDLL(None)
        for function in (cls.lib.user_snprintf,cls.lib.testv,cls.native.snprintf):
            function.argtypes=[C.c_void_p,C.c_size_t,C.c_char_p];function.restype=C.c_int
        cls.lib.user_errno_location.restype=C.POINTER(C.c_int)
        cls.lib.user_dprintf.argtypes=[C.c_int,C.c_char_p]
        cls.lib.user_dprintf.restype=C.c_int
        cls.lib.reset.argtypes=[C.c_size_t]
        cls.lib.reset.restype=None

    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()

    def test_descriptor_short_write_and_interrupted_prefix(self):
        expected=b'['+b'0'*599+b'7'+b'\0]'
        captured=(C.c_ubyte*1024).in_dll(self.lib,'captured')
        used=C.c_size_t.in_dll(self.lib,'used')
        for limit,result,error in ((1024,len(expected),33),(265,-1,4)):
            self.lib.reset(limit)
            self.lib.user_errno_location()[0]=33
            self.assertEqual(self.lib.user_dprintf(3,b'[%0600d%c]',C.c_int(7),C.c_int(0)),result)
            self.assertEqual(self.lib.user_errno_location()[0],error)
            self.assertEqual(bytes(captured[:used.value]),expected[:limit])

    def call(self,format,args=(),size=96,function=None):
        memory=(C.c_ubyte*(size+16))(*([0xa5]*(size+16)))
        self.lib.user_errno_location()[0]=33
        result=(function or self.lib.user_snprintf)(C.addressof(memory)+8,size,format,*args)
        self.assertEqual(bytes(memory[:8])+bytes(memory[8+size:]),b'\xa5'*16)
        return result,bytes(memory[8:8+size]),self.lib.user_errno_location()[0]

    def compare(self,format,args=(),size=96):
        expected,data,_=self.call(format,args,size,self.native.snprintf)
        actual,output,error=self.call(format,args,size)
        self.assertEqual((actual,output),(expected,data),(format,size))
        self.assertEqual(error,33)

    def test_errno_message_consumes_no_argument(self):
        self.lib.user_strerror.restype=C.c_char_p
        message=self.lib.user_strerror(33)
        expected=b'['+message[:5]+b']:42'
        length,output,error=self.call(b'[%.5m]:%d',(C.c_int(42),))
        self.assertEqual((length,output[:length+1],error),(len(expected),expected+b'\0',33))

    def test_truncation_terminator_embedded_nul_and_zero_capacity(self):
        args=(C.c_char_p(b'hello'),C.c_int(-12),C.c_uint(0x2a),C.c_int(0))
        format=b'%s:%+06d:%#x:%c!'
        for size in range(32):self.compare(format,args,size)
        self.lib.user_errno_location()[0]=33
        self.assertEqual(self.lib.user_snprintf(None,0,format,*args),20)
        self.assertEqual(self.lib.user_errno_location()[0],33)
        self.compare(b'')
        self.compare(b'%%:%c:%c',(C.c_int(0),C.c_int(255)))

    def test_integer_flags_precision_and_all_length_modifiers_match_native(self):
        for format,args in ((b'%+08.4d',(C.c_int(-12),)),(b'%#08x',(C.c_uint(0x42),)),
                (b'%#.0o:%#.0x:%#.0X',(C.c_uint(0),C.c_uint(0),C.c_uint(0))),
                (b'%#5.3o:%#05o',(C.c_uint(7),C.c_uint(7))),
                (b'%+ d:% d:%-08d',(C.c_int(0),C.c_int(12),C.c_int(-12)))):
            self.compare(format,args)
        for length,signed,unsigned in ((b'',C.c_int,C.c_uint),(b'hh',C.c_int,C.c_uint),
                (b'h',C.c_int,C.c_uint),(b'l',C.c_long,C.c_ulong),(b'll',C.c_longlong,C.c_ulonglong),
                (b'j',C.c_longlong,C.c_ulonglong),(b'z',C.c_ssize_t,C.c_size_t),(b't',C.c_ssize_t,C.c_size_t)):
            bits=C.sizeof(signed)*8
            for value in (0,1,-1,-(1<<(bits-1)),(1<<(bits-1))-1):
                self.compare(b'[%+025.12'+length+b'd]',(signed(value),))
            for value in (0,1,(1<<bits)-1):
                for conversion in (b'u',b'o',b'x',b'X'):
                    self.compare(b'[%#025.12'+length+conversion+b']' if conversion!=b'u' else b'[%025.12'+length+conversion+b']',(unsigned(value),))

    def test_seeded_integer_formats_match_native_with_truncated_buffers(self):
        rng=random.Random(0x736e7072696e74)
        lengths=[(b'',C.c_int,C.c_uint),(b'hh',C.c_int,C.c_uint),(b'h',C.c_int,C.c_uint),
                 (b'l',C.c_long,C.c_ulong),(b'll',C.c_longlong,C.c_ulonglong),
                 (b'j',C.c_longlong,C.c_ulonglong),(b'z',C.c_ssize_t,C.c_size_t),(b't',C.c_ssize_t,C.c_size_t)]
        for _ in range(1400):
            length,signed,unsigned=rng.choice(lengths);conversion=rng.choice((b'd',b'i',b'u',b'o',b'x',b'X'))
            is_signed=conversion in (b'd',b'i');kind=signed if is_signed else unsigned
            bits=C.sizeof(kind)*8;value=rng.randrange(-(1<<(bits-1)),1<<(bits-1)) if is_signed else rng.randrange(1<<bits)
            choices=b'-+ 0' if is_signed else b'-0' if conversion==b'u' else b'-#0'
            flags=bytes(byte for byte in choices if rng.choice((False,True)))
            width=str(rng.randrange(1,30)).encode();precision=b'.'+str(rng.randrange(0,24)).encode() if rng.choice((False,True)) else b''
            self.compare(b'A%'+flags+width+precision+length+conversion+b'Z',(kind(value),),rng.randrange(0,56))

    def test_dynamic_width_precision_strings_and_varargs_forwarding(self):
        for width,precision in ((8,4),(-8,4),(0,0),(3,-1)):
            self.compare(b'[%*.*s]:[%*.*d]',(C.c_int(width),C.c_int(precision),C.c_char_p(b'abcdef'),C.c_int(width),C.c_int(precision),C.c_int(-123)))
        result,output,error=self.call(b'[%*.*s]:[%*.*d]',(C.c_int(0),C.c_int(-2147483648),C.c_char_p(b'abcdef'),C.c_int(0),C.c_int(-2147483648),C.c_int(-123)))
        self.assertEqual((result,output[:16],error),(15,b'[abcdef]:[-123]'+bytes([0]),33))
        args=tuple(C.c_int(i) for i in range(12))+(C.c_char_p(b'tail'),C.c_longlong(-(1<<63)),C.c_size_t((1<<64)-1))
        format=b'%d/%d/%d/%d/%d/%d/%d/%d/%d/%d/%d/%d/%s/%lld/%zu'
        expected,data,_=self.call(format,args,160,self.native.snprintf)
        result,actual,error=self.call(format,args,160,self.lib.testv)
        self.assertEqual((result,actual,error),(expected,data,33))

    def test_pointer_representation_is_explicit_and_numeric_zero_rules_are_distinct(self):
        for format,value,expected in ((b'%p',0,b'0x0'),(b'%p',0x1234,b'0x1234'),
                (b'%012p',0x1234,b'0x0000001234'),(b'%-10p',0,b'0x0       '),(b'%.0p',0,b'0x0')):
            result,output,error=self.call(format,(C.c_void_p(value),),32)
            self.assertEqual((result,output[:result+1],error),(len(expected),expected+b'\0',33))
        self.compare(b'%.0d:%#.0o:%#.0x',(C.c_int(0),C.c_uint(0),C.c_uint(0)))

    def test_errors_do_not_store_percent_n_and_leave_a_terminated_prefix(self):
        count=C.c_int(77)
        for format,args in ((b'prefix%n',(C.byref(count),)),(b'%f',(C.c_double(1.5),)),
                (b'%ls',(C.c_void_p(1),)),(b'%q',()),(b'%',()),(b'%2$d',(C.c_int(1),)),
                (b'%05%',()),(b'%s',(C.c_void_p(0),))):
            result,output,error=self.call(format,args,16)
            self.assertEqual((result,error),(-1,22),format)
            self.assertIn(0,output)
        self.assertEqual(count.value,77)

    def test_huge_counts_are_bounded_by_output_space_and_detect_int_overflow(self):
        script=r'''
import ctypes as C,sys
lib=C.CDLL(sys.argv[1]);lib.user_snprintf.argtypes=[C.c_void_p,C.c_size_t,C.c_char_p];lib.user_snprintf.restype=C.c_int
lib.user_errno_location.restype=C.POINTER(C.c_int)
b=C.create_string_buffer(8);error=lib.user_errno_location()
assert lib.user_snprintf(None,0,b'%*s',C.c_int(2147483647),C.c_char_p(b''))==2147483647
assert lib.user_snprintf(b,8,b'%*s',C.c_int(2147483647),C.c_char_p(b''))==2147483647
assert b.raw==b' '*7+bytes([0])
for format,args in ((b'%*s!',(C.c_int(2147483647),C.c_char_p(b''))),
 (b'%*d',(C.c_int(-2147483648),C.c_int(1))),(b'%2147483648d',()),
 (b'%.2147483648d',()),(b'%2147483648.*d',())):
 error[0]=33
 assert lib.user_snprintf(b,8,format,*args)==-1 and error[0]==75
'''
        subprocess.run([sys.executable,'-c',script,str(self.path)],check=True,capture_output=True,timeout=3)

    def test_precision_limited_readonly_strings_need_no_nul_and_zero_precision_does_not_read(self):
        script=r'''
import ctypes as C,mmap,sys
lib=C.CDLL(sys.argv[1]);lib.user_snprintf.argtypes=[C.c_void_p,C.c_size_t,C.c_char_p];lib.user_snprintf.restype=C.c_int
native=C.CDLL(None);native.mprotect.argtypes=[C.c_void_p,C.c_size_t,C.c_int];native.mprotect.restype=C.c_int
page=mmap.PAGESIZE;memory=mmap.mmap(-1,2*page,prot=mmap.PROT_READ|mmap.PROT_WRITE)
address=C.addressof(C.c_char.from_buffer(memory));source=address+page-3
C.memmove(source,b'abc',3)
assert native.mprotect(address,page,mmap.PROT_READ)==0
assert native.mprotect(address+page,page,0)==0
buffer=C.create_string_buffer(16)
assert lib.user_snprintf(buffer,16,b'[%.3s]',C.c_void_p(source))==5 and buffer.value==b'[abc]'
assert lib.user_snprintf(buffer,16,b'[%.0s]',C.c_void_p(1))==2 and buffer.value==b'[]'
assert native.mprotect(address,2*page,mmap.PROT_READ|mmap.PROT_WRITE)==0
memory.close()
'''
        subprocess.run([sys.executable,'-c',script,str(self.path)],check=True,capture_output=True)
