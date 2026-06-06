obj-y += entry.o
obj-y += main.o
obj-y += timer.o
obj-y += trap_entry.o
obj-y += trap.o
obj-y += uart.o


ccflags-y += -march=rv64gc
ccflags-y += -mabi=lp64d
ccflags-y += -DNSTACK=$(SMP)
