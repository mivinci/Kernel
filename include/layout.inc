/* Internal virtual layout, shared by C and host page-table auditors. */
#ifndef _LAYOUT_INC
#define _LAYOUT_INC
#define KBASE         0xffffffc000000000
#define KMAP_BYTES    0x200000000
#define KIO           0xffffffc400000000
#define KSTACK_BASE   0xffffffc401000000
#define KSTACK_STRIDE 0x200000
#endif
