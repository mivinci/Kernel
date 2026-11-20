import ctypes as C
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]


class StringPrimitivesTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();folder=Path(cls.temp.name)
        names=('memcpy','memmove','memset','memcmp','strlen','strcmp','strchr','memchr','strnlen',
               'strncmp','strrchr','strstr','strspn','strcspn','strpbrk','strcpy','strncpy','strcat','strncat')
        cls.libs=[]
        for level in ('O0','O2'):
            path=folder/(level+'.so')
            cmd=['clang','-std=c89','-'+level,'-ffreestanding','-fno-builtin','-Wall','-Wextra','-Werror',
                 '-shared','-fPIC','-I'+str(ROOT/'lib/libc/include'),'-I'+str(ROOT/'include'),
                 *['-D'+name+'=user_'+name for name in names],str(ROOT/'lib/libc/string.c'),'-o',str(path)]
            if sys.platform=='darwin':
                sdk=subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip()
                cmd+=['-isysroot',sdk,'-Wl,-syslibroot,'+sdk]
            subprocess.run(cmd,check=True,capture_output=True)
            lib=C.CDLL(str(path))
            pointer=C.c_void_p
            signatures={
                'strncmp':([pointer,pointer,C.c_size_t],C.c_int),
                'strnlen':([pointer,C.c_size_t],C.c_size_t),
                'memchr':([pointer,C.c_int,C.c_size_t],pointer),
                'strrchr':([pointer,C.c_int],pointer),
                'strstr':([pointer,pointer],pointer),
                'strspn':([pointer,pointer],C.c_size_t),
                'strcspn':([pointer,pointer],C.c_size_t),
                'strpbrk':([pointer,pointer],pointer),
                'strcpy':([pointer,pointer],pointer),'strcat':([pointer,pointer],pointer),
                'strncpy':([pointer,pointer,C.c_size_t],pointer),
                'strncat':([pointer,pointer,C.c_size_t],pointer)}
            for name,(args,result) in signatures.items():
                function=getattr(lib,'user_'+name);function.argtypes=args;function.restype=result
            cls.libs.append(lib)

    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()

    def text(self,rng,limit=48):
        return bytes(rng.randrange(1,256) for _ in range(rng.randrange(limit+1)))

    def test_unsigned_bounded_comparison_and_length_match_byte_reference(self):
        for lib in self.libs:
            rng=random.Random(1661)
            for _ in range(2048):
                a,b=self.text(rng),self.text(rng);n=rng.randrange(60)
                result=lib.user_strncmp(a,b,n)
                self.assertEqual((result>0)-(result<0),(a[:n]>b[:n])-(a[:n]<b[:n]))
                self.assertEqual(lib.user_strnlen(a,n),min(n,len(a)))

    def test_binary_search_includes_nul_high_bytes_and_int_conversion(self):
        for lib in self.libs:
            rng=random.Random(1662)
            for _ in range(2048):
                data=bytes(rng.randrange(256) for _ in range(rng.randrange(1,65)))
                buffer=C.create_string_buffer(data);n=rng.randrange(len(data)+1);value=rng.randrange(-512,513)
                at=data[:n].find(bytes([value&255]))
                self.assertEqual(lib.user_memchr(buffer,value,n),None if at<0 else C.addressof(buffer)+at)

    def test_last_character_and_substring_return_exact_first_or_last_address(self):
        for lib in self.libs:
            rng=random.Random(1663)
            for _ in range(1024):
                data=self.text(rng);buffer=C.create_string_buffer(data);value=rng.randrange(-512,513)
                at=len(data) if value&255==0 else data.rfind(bytes([value&255]))
                self.assertEqual(lib.user_strrchr(buffer,value),None if at<0 else C.addressof(buffer)+at)
                needle=self.text(rng,12) if rng.randrange(2) else data[rng.randrange(len(data)+1):]
                at=data.find(needle)
                self.assertEqual(lib.user_strstr(buffer,needle),None if at<0 else C.addressof(buffer)+at)

    def test_span_and_set_search_with_empty_duplicate_and_high_byte_sets(self):
        for lib in self.libs:
            rng=random.Random(1664)
            for _ in range(1024):
                data,chars=self.text(rng),self.text(rng,12);buffer=C.create_string_buffer(data)
                accepted=next((i for i,b in enumerate(data) if b not in chars),len(data))
                rejected=next((i for i,b in enumerate(data) if b in chars),len(data))
                self.assertEqual(lib.user_strspn(buffer,chars),accepted)
                self.assertEqual(lib.user_strcspn(buffer,chars),rejected)
                self.assertEqual(lib.user_strpbrk(buffer,chars),None if rejected==len(data) else C.addressof(buffer)+rejected)

    def test_copy_truncation_padding_zero_count_and_surrounding_canaries(self):
        for lib in self.libs:
            rng=random.Random(1665)
            for _ in range(1024):
                data=self.text(rng,31);n=rng.randrange(36)
                buffer=C.create_string_buffer(bytes([0xa5])*64,64);at=C.addressof(buffer)+7
                self.assertEqual(lib.user_strncpy(at,data,n),at)
                self.assertEqual(buffer.raw,bytes([0xa5])*7+data[:n].ljust(n,b'\0')+bytes([0xa5])*(57-n))
                buffer=C.create_string_buffer(bytes([0xa5])*64,64);at=C.addressof(buffer)+7
                self.assertEqual(lib.user_strcpy(at,data),at)
                self.assertEqual(buffer.raw,bytes([0xa5])*7+data+b'\0'+bytes([0xa5])*(56-len(data)))

    def test_concat_preserves_base_and_always_terminates_within_supplied_capacity(self):
        for lib in self.libs:
            rng=random.Random(1666)
            for _ in range(1024):
                a,b=self.text(rng,15),self.text(rng,15);n=rng.randrange(19)
                for function,tail,args in ((lib.user_strcat,b,()),(lib.user_strncat,b[:n],(n,))):
                    initial=a+b'\0'+bytes([0xa5])*(63-len(a))
                    buffer=C.create_string_buffer(initial,64);at=C.addressof(buffer)
                    self.assertEqual(function(at,b,*args),at)
                    wanted=a+tail+b'\0'
                    self.assertEqual(buffer.raw,wanted+bytes([0xa5])*(64-len(wanted)))
