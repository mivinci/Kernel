#ifndef _PROTO_INIT_H
#define _PROTO_INIT_H
#include <types.h>

#define INIT_VERSION  UINT64_C(1)
#define INIT_READY    1
#define INIT_LOOKUP   2
#define INIT_SHUTDOWN 3
#define INIT_STATUS   4

/* Four message words: version/opcode, sequence, argument0, argument1.
   READY arguments are the manifest service number and the sender image.
   Source identity and service state must be checked by the receiver. */
#define INIT_WORD(opcode)       ((INIT_VERSION << 32) | (u32) (opcode))
#define INIT_WORD_VERSION(word) ((u32) ((u64) (word) >> 32))
#define INIT_WORD_OPCODE(word)  ((u32) (word))

#endif
