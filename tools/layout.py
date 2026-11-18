"""Read the internal C/assembler virtual layout without a second literal copy."""
from pathlib import Path
import re

_text=(Path(__file__).resolve().parents[1]/'include/layout.inc').read_text()
def _literal(name):
    match=re.search(r'^#define\s+'+name+r'\s+(0x[0-9a-fA-F]+|[0-9]+)\s*$',_text,re.M)
    if not match:raise ValueError('missing layout literal '+name)
    return int(match[1],0)

KBASE=_literal('KBASE')
KMAP_BYTES=_literal('KMAP_BYTES')
KIO=_literal('KIO')
KSTACK_BASE=_literal('KSTACK_BASE')
KSTACK_STRIDE=_literal('KSTACK_STRIDE')
