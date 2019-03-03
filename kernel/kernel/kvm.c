#include "../include/x86.h"
#include "../include/device.h"
#include <string.h>
#include <elf.h>

SegDesc gdt[NR_SEGMENTS];
TSS tss;

ProcessTable pcb[MAX_PCB_NUM];
uint32_t utop[MAX_PCB_NUM];
uint32_t pcb_cur;

Semaphore sem_pool[MAX_SEM_NUM];

#define SECTSIZE 512

void waitDisk(void) {
	while((inByte(0x1F7) & 0xC0) != 0x40); 
}
/*
static void outint(uint32_t x) {
	putChar('\n');
	int buf[10], tot = 0;
	if (x == 0) putChar('0');
	else {
		while (x > 0) {
			buf[tot++] = x % 10;
			x /= 10;
		}
		for (--tot; tot >= 0; --tot) {
			putChar(buf[tot]+'0');
		}
	}
	putChar('\n');
}
*/
void readSect(void *dst, int offset) {
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

void initSeg() {
	gdt[SEG_KCODE] = SEG(STA_X | STA_R, 0,       0xffffffff, DPL_KERN);
	gdt[SEG_KDATA] = SEG(STA_W,         0,       0xffffffff, DPL_KERN);
	gdt[SEG_UCODE] = SEG(STA_X | STA_R, 0,       0xffffffff, DPL_USER);
	gdt[SEG_UDATA] = SEG(STA_W,         0,       0xffffffff, DPL_USER);
	gdt[SEG_UCODE2]= SEG(STA_X | STA_R, 0x100000,  0xffffffff, DPL_USER);
	gdt[SEG_UDATA2]= SEG(STA_W,			0x100000,  0xffffffff, DPL_USER);
	gdt[SEG_TSS] = SEG16(STS_T32A,      &tss, sizeof(TSS)-1, DPL_KERN);
	gdt[SEG_TSS].s = 0;
	gdt[SEG_MOVIE] = SEG(STA_W, 0xb8000, 0xffffffff, DPL_KERN);
	setGdt(gdt, sizeof(gdt));

	/*
	 * 初始化TSS
	 */

	tss.ss0 = KSEL(SEG_KDATA);
	tss.esp0 = (uint32_t)&pcb[1].tf;

	asm volatile("ltr %%ax":: "a" (KSEL(SEG_TSS)));

	/*设置正确的段寄存器*/
	
	asm volatile("mov $0x0010, %%ax":::"ax");
	asm volatile("mov %ax, %ds");
	asm volatile("mov %ax, %es");
	asm volatile("mov %ax, %fs");
//	asm volatile("mov %ax, %ss");
	asm volatile("mov $0x0030, %%ax":::"ax");
	asm volatile("mov %ax, %gs");

	lLdt(0);
	int y;
	asm volatile("mov (%%esp), %0": "=g"(y));
}

void initPt(uint32_t id) {
	uint8_t* p = (uint8_t*)(pcb+id);
	uint32_t i = 0;
	for (; i < sizeof(ProcessTable); ++i, ++p) *p = 0;
	ProcessTable* pt = pcb+id;
	pt->timeCount = 100;
}

void idle() {
	while (1);
}

uint32_t availableId() {
	uint32_t i = 0;
	for (; i < MAX_PCB_NUM; ++i) if (pcb[i].state == 3) return i;
	return -1;
}

uint32_t availableTop() {
	uint32_t ret = MAX_UTOP;
	while (1) {
		uint32_t i = 0;
		for (; i < MAX_PCB_NUM; ++i) {
			if (utop[i] == ret) break;
		}
		if (i == MAX_PCB_NUM) return ret;
		--ret;
	}
	return 0;
}


void initIdle() {
	int cur_id = availableId();
	assert(cur_id != -1);
	initPt(cur_id);
	ProcessTable* pt = pcb+cur_id;
	pt->state = 1;
	pt->pid = cur_id;
	TrapFrame* tf = &(pt->tf);
	tf->cs = 3<<3|3;
	tf->eip = (uint32_t)(idle);
	tf->eflags = 0x216;
	tf->ss = 4<<3|3;
	tf->esp = (utop[cur_id] = availableTop())<<20;
	tf->ds = tf->es = tf->fs = tf->gs = 4<<3|3;
}

void initAll() {
	uint8_t* p = (uint8_t*)pcb;
	uint32_t i = 0;
	for (; i < sizeof(ProcessTable)*MAX_PCB_NUM; ++i, ++p) *p = 0;
	i = 0;
	for (; i < MAX_PCB_NUM; ++i) pcb[i].state = 3;
}

void initFirst(uint32_t entry) {
	int cur_id = availableId();
	assert(cur_id != -1);
	initPt(cur_id);
	ProcessTable* pt = pcb+cur_id;
	pt->state = 0;
	pt->pid = cur_id;
	tss.esp0 = (uint32_t)&pt->state;
	tss.ss0 = 2<<3;
	pcb_cur = 1;
	TrapFrame* tf = &pt->tf;
	tf->ss = 4<<3|3;
	tf->esp = (utop[cur_id] = availableTop())<<20;
	tf->eflags = 0x216;
	tf->cs = 3<<3|3;
	tf->eip = entry;
	tf->ds = tf->es = tf->fs = tf->gs = 4<<3|3;
}

void initSemPool() {
	int i = 0;
	for (; i < MAX_SEM_NUM; ++i) sem_pool[i].used = 0;
}

void enterUserSpace(uint32_t entry) {
	/*
	 * Before enter user space 
	 * you should set the right segment registers here
	 * and use 'iret' to jump to ring3
	 */
	initAll();

	initIdle();

	initFirst(entry);
	
	initSemPool();

	asm volatile("movl %0, %%esp"::"g"(&pcb[1].tf.edi));
	asm volatile("push $0x23");
	asm volatile("push $0x23");
	asm volatile("push $0x23");
	asm volatile("push $0x23");
	asm volatile("pop %gs");
	asm volatile("pop %fs");
	asm volatile("pop %es");
	asm volatile("pop %ds");
	asm volatile("movl %0, %%esp"::"g"(&pcb[1].tf.eip));
	asm volatile("addl $0x14, %esp");

	asm volatile("push $(4<<3|3)":::"memory");
	asm volatile("push %0"::"m"(pcb[1].tf.esp):"memory");
	asm volatile("push $0x216":::"memory");
	asm volatile("push $(3<<3|3)":::"memory");
	asm volatile("push %0"::"m"(entry):"memory");

	asm volatile("iret");
}

void loadUMain(void) {
	/*加载用户程序至内存*/
	ELFHeader* elf = (ELFHeader*)0x400000;
	int i=201;
	for (; i <= 400; ++i) readSect((char*)elf+(i-201)*SECTSIZE, i);
	ProgramHeader* ph = (ProgramHeader*)((char*)elf+elf->phoff);
	for (i = 0; i < elf->phnum; ++i) {
		if (ph->type == PT_LOAD) {
			unsigned int j = 0;
			for (; j < ph->filesz; ++j) *((char*)ph->vaddr+j) = *((char*)elf+ph->off+j);
		}
		++ph;
	}
	enterUserSpace(elf->entry);
}
