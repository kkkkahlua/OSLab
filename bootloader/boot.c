#include "boot.h"
#include <elf.h>
#include "../kernel/include/common.h"
#include <string.h>

#define SECTSIZE 512

void bootMain(void) {
	/* 加载内核至内存，并跳转执行 */
	ELFHeader* elf = (ELFHeader*)0x400000;
	int i = 1;
	for (; i <= 200; ++i) readSect((char*)elf+(i-1)*SECTSIZE, i);

	ProgramHeader* ph = (ProgramHeader*)((char*)elf+elf->phoff);
	for (i = 0; i < elf->phnum; ++i) {
		if (ph->type == PT_LOAD) {
			unsigned int j = 0;
			for (; j < ph->filesz; ++j) *((char*)ph->vaddr+j) = *((char*)elf+ph->off+j);
		}
		++ph;
	}
	((void(*)(void))elf->entry)();
}

void waitDisk(void) { // waiting for disk
	while((inByte(0x1F7) & 0xC0) != 0x40);
}

void readSect(void *dst, int offset) { // reading a sector of disk
	int i;
	waitDisk();
	outByte(0x1F2, 1);
	outByte(0x1F3, offset);
	outByte(0x1F4, offset >> 8);
	outByte(0x1F5, offset >> 16);
	outByte(0x1F6, (offset >> 24) | 0xE0);
	outByte(0x1F7, 0x20);

	waitDisk();
	for (i = 0; i < SECTSIZE / 4; i ++) {
		((int *)dst)[i] = inLong(0x1F0);
	}
}
