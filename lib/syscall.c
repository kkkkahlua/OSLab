#include "lib.h"
#include "types.h"
/*
 * io lib here
 * 库函数写在这
 */
#include <stdarg.h>

int32_t syscall(int num, uint32_t a1,uint32_t a2,
		uint32_t a3, uint32_t a4, uint32_t a5)
{
	int32_t ret = 0;

	/* 内嵌汇编 保存 num, a1, a2, a3, a4, a5 至通用寄存器*/
	asm volatile("mov %0, %%eax"::"m"(num):"eax");
	asm volatile("mov %0, %%ebx"::"m"(a1):"ebx");
	asm volatile("mov %0, %%ecx"::"m"(a2):"ecx");
	asm volatile("mov %0, %%edx"::"m"(a3):"edx");
	asm volatile("mov %0, %%edi"::"m"(a4):"edi");
	asm volatile("mov %0, %%esi"::"m"(a5):"esi");

	asm volatile("int $0x80");

	asm volatile("movl %%eax, %0":"=m"(ret)::"memory");
	return ret;
}

void printChar(char ch) {
	syscall(1, ch, 0, 0, 0, 0);
}

void printf(const char *format,...) {
	va_list ap;
	va_start(ap, format);
	int i = 0;
	for (; format[i]; ++i) {
		if (format[i]!='%') {
			printChar(format[i]);
	 	}
		else {
			++i;
			switch (format[i]) {
				case 's': {
					char *str = va_arg(ap, char*);
					int j = 0;
					for (; str[j]; ++j) printChar(str[j]);
					break;
				}
				case 'c': {
					char ch = va_arg(ap, int);
					printChar(ch);
					break;
				}
				case 'd': {
					int x = va_arg(ap, int);
					unsigned int ux = x;
					if (x < 0) printChar('-'), ux = -(unsigned int)x;
					char ch[11]; int tot = 0;
					do { ch[tot++] = '0'+ux%10, ux/=10; } while (ux);
					for (--tot; tot>=0; --tot) printChar(ch[tot]);
					break;
				}
				case 'x': {
					unsigned int x = va_arg(ap, int);
					char ch[9]; int tot = 0;
					do { ch[tot++] = x%16<10 ? '0'+x%16 : 'a'+(x%16-10), x/=16; } while (x);
					for (--tot; tot>=0; --tot) printChar(ch[tot]);
				}
			}
	 	}
	 }
}

int fork() {
	return syscall(2, 0, 0, 0, 0, 0);
}

int sleep(uint32_t time) {
	syscall(3, time, 0, 0, 0, 0);
	return 0;
}

int exit() {
	syscall(4, 0, 0, 0, 0, 0);
	return 0;
}

int sem_init(sem_t* sem, uint32_t value) {
	int ret = syscall(5, (uint32_t)sem, value, 0, 0, 0);
	if (ret == -1) return -1;
	else return *sem = ret;
}

int sem_post(sem_t* sem) {
	return syscall(6, (uint32_t)sem, 0, 0, 0, 0);
}

int sem_wait(sem_t* sem) {
	return syscall(7, (uint32_t)sem, 0, 0, 0, 0);
}

int sem_destroy(sem_t* sem) {
	return syscall(8, (uint32_t)sem, 0, 0, 0, 0);
}
