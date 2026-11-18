import os
from pathlib import Path
import socket
import sys
import tempfile
import threading
import time
import unittest

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from gdbpipe import GdbPipe,GdbError
from smoke import check_arm64_cache,SmokeError

class GdbPipeTests(unittest.TestCase):
    def test_fragmented_rsp_escape_and_run_length_decode(self):
        left,right=socket.socketpair();right.settimeout(1);errors=[]
        client=GdbPipe.__new__(GdbPipe);client.fds=[left.fileno(),left.fileno()]
        client.buffer=bytearray();client.deadline=time.monotonic()+2
        body=b'A*\x1f}\x03}\x04}]'
        response=b'+$'+body+b'#'+('%02x'%(sum(body)&255)).encode()
        def server():
            try:
                request=b''
                while b'#' not in request or len(request)<request.index(b'#')+3:
                    request+=right.recv(1024)
                for chunk in (response[:1],response[1:4],response[4:]):
                    right.sendall(chunk);time.sleep(.001)
                if right.recv(1)!=b'+':raise AssertionError('response not acknowledged')
            except BaseException as error:errors.append(error)
        thread=threading.Thread(target=server);thread.start()
        try:self.assertEqual(client.request('probe'),b'AAA#$}')
        finally:
            thread.join(2);left.close();right.close()
        self.assertFalse(thread.is_alive());self.assertEqual(errors,[])

    def test_corrupt_checksum_is_rejected_before_ack(self):
        client=GdbPipe.__new__(GdbPipe);client.buffer=bytearray(b'+$hello#00')
        with self.assertRaisesRegex(GdbError,'checksum mismatch'):client._packet()

    def test_pipe_deadline_and_close_are_bounded_and_remove_only_owned_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            base=Path(directory)/'gdb';client=GdbPipe(base,time.monotonic()+.03)
            try:
                with self.assertRaisesRegex(GdbError,'timed out|deadline'):client.request('qSupported')
            finally:client.close()
            self.assertFalse(Path(str(base)+'.in').exists())
            Path(str(base)+'.out').write_text('owned by caller')
            with self.assertRaises(FileExistsError):GdbPipe(base,time.monotonic()+1)
            self.assertEqual(Path(str(base)+'.out').read_text(),'owned by caller')

    def test_feature_numbering_gaps_and_little_endian_register_values(self):
        class Client(GdbPipe):
            def __init__(self):
                self.descriptions={};self.regs={};self.next_register=0;self.calls=[]
            def request(self,text):
                self.calls.append(text)
                if text.startswith('qSupported'):return b'qXfer:features:read+'
                if text.startswith('qXfer:features:read:target.xml:'):
                    return b'l<target><xi:include href="core.xml"/></target>'
                if text.startswith('qXfer:features:read:core.xml:'):
                    return b'l<feature><reg name="dummy" bitsize="64" regnum="20"/><reg name="SCTLR_EL1" bitsize="64"/></feature>'
                if text=='p15':return b'0510000000000000'
                raise AssertionError(text)
        client=Client()
        self.assertEqual(client.registers(['SCTLR_EL1']),{'SCTLR_EL1':0x1005})
        self.assertEqual(client.regs['SCTLR_EL1'],(21,64))
        self.assertNotIn('p14',client.calls)

    def test_cache_auditor_rejects_missing_cache_enable_or_wrong_memory_attributes(self):
        values={'SCTLR_EL1':0x1005,'TCR_EL1':0x2b5193519,'MAIR_EL1':0xff00}
        self.assertTrue(check_arm64_cache(values,True)['independent_gdb_read'])
        for register,value in [('SCTLR_EL1',0x1001),('SCTLR_EL1',5),('MAIR_EL1',0),('TCR_EL1',0)]:
            bad=dict(values);bad[register]=value
            with self.assertRaises(SmokeError):check_arm64_cache(bad,True)
        self.assertFalse(check_arm64_cache({'SCTLR_EL1':0x1000},False)['translated'])
