/*
 * 基础类型。内核是 freestanding 的，只用编译器提供的定宽整数。
 */
#ifndef _TYPES_H
#define _TYPES_H

#include <stddef.h>
#include <stdint.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int64_t  isize;
typedef uint64_t usize;

#ifndef NULL
#define NULL ((void *) 0)
#endif

/* 编译期断言：条件不成立时数组长度为负，编译失败 */
#define STATIC_ASSERT(cond, msg) typedef char _static_assert_##msg[(cond) ? 1 : -1]

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define PAGE_SHIFT 12
#define PAGE_SIZE  (1UL << PAGE_SHIFT)
#define PAGE_MASK  (PAGE_SIZE - 1)

/* 向上/向下对齐到 2 的幂 */
#define ALIGN_UP(x, a)   (((x) + ((a) - 1)) & ~((a) - 1))
#define ALIGN_DOWN(x, a) ((x) & ~((a) - 1))
#define IS_ALIGNED(x, a) (((x) & ((a) - 1)) == 0)

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define UNUSED(x) ((void) (x))

/* 编译器扩展 */
#define NORETURN      __attribute__((noreturn))
#define PACKED        __attribute__((packed))
#define ALIGNED(n)    __attribute__((aligned(n)))
#define INLINE        static inline
#define UNREACHABLE() __builtin_unreachable()

#endif /* _TYPES_H */
