/* QEMU virt 早期轮询控制台；不定义设备授权和中断模型。 */
#ifndef _CONSOLE_H
#define _CONSOLE_H
#include <types.h>
void arch_console_init(void);
void arch_console_putc(char c);
void arch_console_write(const char *s, size_t n);
#endif
