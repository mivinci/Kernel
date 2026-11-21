import ctypes as C
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]

class NumberTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();cls.path=Path(cls.temp.name)/'number.so'
        command=['clang','-std=c89','-O2','-ffreestanding','-fno-builtin','-Wall','-Wextra','-Werror','-shared','-fPIC',
                 '-I'+str(ROOT/'lib/libc/include'),'-I'+str(ROOT/'include'),
                 '-Dstrtol=user_strtol','-Dstrtoul=user_strtoul','-D__errno_location=user_errno_location',
                 '-Dstrerror=user_strerror','-Disspace=user_isspace',str(ROOT/'lib/libc/number.c'),
                 str(ROOT/'lib/libc/ctype.c'),str(ROOT/'lib/libc/errno.c'),'-o',str(cls.path)]
        if sys.platform=='darwin':
            sdk=subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip()
            command+=['-isysroot',sdk,'-Wl,-syslibroot,'+sdk]
        subprocess.run(command,check=True,capture_output=True)
        cls.lib=C.CDLL(str(cls.path))
        for name,kind in (('user_strtol',C.c_long),('user_strtoul',C.c_ulong)):
            function=getattr(cls.lib,name);function.argtypes=[C.c_void_p,C.POINTER(C.c_void_p),C.c_int];function.restype=kind
        cls.lib.user_errno_location.restype=C.POINTER(C.c_int)
        cls.bits=C.sizeof(C.c_long)*8;cls.maximum=(1<<(cls.bits-1))-1;cls.minimum=-(1<<(cls.bits-1));cls.unsigned=(1<<cls.bits)-1

    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()

    def check(self,string,base,value,end,error=33,unsigned=False,null_end=False):
        data=C.create_string_buffer(string);tail=C.c_void_p(1);self.lib.user_errno_location()[0]=33
        function=self.lib.user_strtoul if unsigned else self.lib.user_strtol
        self.assertEqual(function(C.addressof(data),None if null_end else C.byref(tail),base),value,(string,base,unsigned))
        if not null_end:self.assertEqual(tail.value-C.addressof(data),end,(string,base,unsigned))
        self.assertEqual(self.lib.user_errno_location()[0],error,(string,base,unsigned))

    @staticmethod
    def encode(value,base):
        alphabet='0123456789abcdefghijklmnopqrstuvwxyz';digits=[]
        while value:
            value,remainder=divmod(value,base);digits.append(alphabet[remainder])
        return ''.join(reversed(digits or ['0'])).encode()

    def test_prefixes_signs_endptr_and_c_locale_bytes(self):
        cases=[(b'',0,0,0),(b' \t\r\n+',0,0,0),(b' -z',10,0,0),(b' +-1',10,0,0),
               (b' -0X2a!',0,-42,6),(b'0759',0,61,3),(b'089',0,0,1),
               (b'0x',0,0,1),(b'0Xg',16,0,1),(b'0x!',16,0,1),(b'0xFf!',16,255,4),
               (b'0xFf!',10,0,1),(b'0b11',0,0,1),(b'0b11',2,0,1),(b'0b11',16,2833,4),
               (b'zZ!',36,1295,2),(b'\xff12',10,0,0),(b'12\xff3',10,12,2),
               (b' \t\n\v\f\r+123 ',0,123,10),(b'-0!',0,0,2)]
        for string,base,value,end in cases:
            self.check(string,base,value,end)
            self.check(string,base,value & self.unsigned,end,unsigned=True)

    def test_every_base_checks_signed_and_unsigned_extrema_without_wraparound(self):
        for base in range(2,37):
            for magnitude in (0,1,self.maximum,self.maximum+1,self.maximum+2,self.unsigned,self.unsigned+1):
                digits=self.encode(magnitude,base)
                for negative in (False,True):
                    text=(b'-' if negative else b'+')+digits+b'!'
                    value=-magnitude if negative else magnitude
                    expected=min(self.maximum,max(self.minimum,value))
                    self.check(text,base,expected,len(text)-1,34 if expected!=value else 33)
                    self.check(text,base,self.unsigned if magnitude>self.unsigned else value&self.unsigned,
                               len(text)-1,34 if magnitude>self.unsigned else 33,unsigned=True)

    def test_seeded_values_and_mixed_case_digits_match_independent_integer_values(self):
        rng=random.Random(0x737472746f6c)
        for _ in range(1200):
            base=rng.randrange(2,37);magnitude=rng.randrange(1<<(self.bits+8));negative=rng.choice((False,True))
            digits=bytes(byte-32 if 97<=byte<=122 and rng.choice((False,True)) else byte for byte in self.encode(magnitude,base))
            text=b' \t'+(b'-' if negative else b'+')+digits+b'!'
            value=-magnitude if negative else magnitude
            clipped=min(self.maximum,max(self.minimum,value))
            self.check(text,base,clipped,len(text)-1,34 if clipped!=value else 33)
            self.check(text,base,self.unsigned if magnitude>self.unsigned else value&self.unsigned,len(text)-1,
                       34 if magnitude>self.unsigned else 33,unsigned=True)

    def test_invalid_base_and_optional_endptr_have_precise_error_effects(self):
        for base in (-2147483648,-1,1,37,2147483647):
            for unsigned in (False,True):
                self.check(b'123',base,0,0,22,unsigned)
        for unsigned in (False,True):
            self.check(b'42!',10,42,2,unsigned=unsigned,null_end=True)
            self.check(b'-',0,0,0,unsigned=unsigned,null_end=True)

    def test_overflow_keeps_scanning_all_digits_and_negative_unsigned_does_not_negate_clamp(self):
        for sign in (b'',b'+',b'-'):
            text=sign+b'9'*10000+b'!'
            self.check(text,10,self.minimum if sign==b'-' else self.maximum,len(text)-1,34)
            self.check(text,10,self.unsigned,len(text)-1,34,True)
        self.check(b'-1',10,self.unsigned,2,unsigned=True)
        self.check(b'-'+str(self.unsigned).encode(),10,1,len(str(self.unsigned))+1,unsigned=True)

    def test_short_readonly_strings_stop_at_guard_page(self):
        script=r'''
import ctypes as C,mmap,sys
lib=C.CDLL(sys.argv[1]);native=C.CDLL(None)
lib.user_strtol.argtypes=[C.c_void_p,C.POINTER(C.c_void_p),C.c_int];lib.user_strtol.restype=C.c_long
lib.user_strtoul.argtypes=lib.user_strtol.argtypes;lib.user_strtoul.restype=C.c_ulong
native.mprotect.argtypes=[C.c_void_p,C.c_size_t,C.c_int];native.mprotect.restype=C.c_int
page=mmap.PAGESIZE
for text in (b'',b'0',b'0x',b'0Xf',b'-',b' ',b'\xff'):
 memory=mmap.mmap(-1,page*2,prot=mmap.PROT_READ|mmap.PROT_WRITE)
 address=C.addressof(C.c_char.from_buffer(memory));start=address+page-len(text)-1
 C.memmove(start,text+b'\0',len(text)+1)
 assert native.mprotect(address,page,mmap.PROT_READ)==0
 assert native.mprotect(address+page,page,0)==0
 for name in ('user_strtol','user_strtoul'):
  tail=C.c_void_p();getattr(lib,name)(start,C.byref(tail),0)
 assert native.mprotect(address,page*2,mmap.PROT_READ|mmap.PROT_WRITE)==0
 memory.close()
'''
        subprocess.run([sys.executable,'-c',script,str(self.path)],check=True,capture_output=True)
