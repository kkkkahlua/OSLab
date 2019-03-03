#include "../include/x86.h"
#include "../include/device.h"

void syscallHandle(struct TrapFrame *tf);

void GProtectFaultHandle(struct TrapFrame *tf);

void timeInterruptHandle(struct TrapFrame* tf);

extern ProcessTable pcb[MAX_PCB_NUM];
extern TSS tss;
extern uint32_t pcb_cur;
extern void initPt(uint32_t);
extern uint32_t utop[MAX_PCB_NUM];
extern uint32_t availableTop();
extern uint32_t availableId();

void irqHandle(struct TrapFrame *tf) {
	/*
	 * 中断处理程序
	 */
	/* Reassign segment register */
	asm volatile("movw $0x10, %ax");
	asm volatile("movw %ax, %ds");
	switch(tf->irq) {
		case -1:
			break;
		case 0xd:
			GProtectFaultHandle(tf);
			break;
		case 0x20:
			timeInterruptHandle(tf);
			break;
		case 0x80:
			syscallHandle(tf);
			break;
		default:assert(0);
	}
}

void outint(uint32_t x) {
	int buf[10], tot = 0;
	do {
		buf[tot++] = x % 10;
		x /= 10;
	} while (x);
	for (--tot; tot >= 0; --tot) putChar(buf[tot]+'0'); 
	putChar('\n');
}

void outstr(char* str) {
	int i = 0;
	while (str[i]) { putChar(str[i]); ++i; } putChar('\n');
}

void outhex(uint32_t x) {
	putChar('0'); putChar('x');
	char buf[10]; int tot = 0;
	do {
		buf[tot++] = x%16<10 ? '0'+x%16 : 'a'+(x%16-10);
		x /= 16;
	} while (x);
	for (--tot; tot >= 0; --tot) putChar(buf[tot]); 
	putChar('\n');
}

int row = 5, col = 0;

void doPrintf(TrapFrame* tf) {
//	outstr("print");
	if (tf->ebx == '\n') { ++row; col = 0; return; }
	asm volatile("movl $160, %%ecx":::"ecx");
	asm volatile("imul %0, %%ecx"::"g"(row):"ecx");
	asm volatile("movl %0, %%ebx"::"g"(col):"ebx");
	asm volatile("leal (%%ecx, %%ebx, 2), %%edi":::"edi");
	asm volatile("movb $0x0c, %%ah":::"ah");
	asm volatile("movb %0, %%al"::"m"(tf->ebx):"al");
	asm volatile("addl $0xb8000, %%edi":::"edi");
	asm volatile("movw %%ax, (%%edi)":::"memory");
	++col;
	if (col == 80) { ++row; col = 0; }
}

void schedule() {
//	outstr("schedule");
	uint32_t i = 1;
//	outint(availableTop());
	for (; i < MAX_PCB_NUM; ++i) {
		if (pcb[i].state < 2) {
			outint(i);
			outint(pcb[i].pid);
			outint(pcb[i].state);
	//		outhex(utop[0]); outhex(utop[1]); outhex(utop[2]);
			outhex(pcb[i].tf.cs);
			outhex(pcb[i].tf.eip);
			outhex(pcb[i].tf.ds);
			outhex(pcb[i].tf.esp);
			pcb_cur = i;
			pcb[i].state = 0;
			if (pcb[i].state == 1) {
				pcb[i].timeCount = 100;
			}
			tss.esp0 = (uint32_t)&pcb[i].state;
			tss.ss0 = 0x10;
			asm volatile("movl %0, %%esp"::"g"(&pcb[i].tf));
			asm volatile("pop %gs");
			asm volatile("pop %fs");
			asm volatile("pop %es");
			asm volatile("pop %ds");
	
			asm volatile("popa");
			asm volatile("addl $0x8, %esp");
			asm volatile("iret");
		}
	}
	pcb_cur = 0;
	tss.esp0 = (uint32_t)&pcb[0].state;
	tss.ss0 = 0x10;
	pcb[0].state = 0;
	pcb[0].timeCount = 100;
	asm volatile("movl %0, %%esp"::"g"(&pcb[0].tf.eip));
	asm volatile("iret");
}

void doSleep(TrapFrame* tf) {
	outstr("sleep");
	uint32_t i = pcb_cur;
	pcb[i].state = 2;
	pcb[i].sleepTime = tf->ebx;
	schedule();
}

void doExit() {
	outstr("exit");
	uint32_t i = pcb_cur;
	pcb[i].state = 3;
	utop[i] = 0;
	schedule();
}

void memCpy(uint8_t* p1, uint8_t* p2, uint32_t size) {
	uint32_t i = 0;
	for (; i < size; ++i, ++p1, ++p2) *p1 = *p2;
}

void doFork(TrapFrame* tf) {
	outstr("fork");
	int cur_id = availableId();
	assert(cur_id != -1);
	initPt(cur_id);
	ProcessTable* ptf = pcb+pcb_cur,
				* ptc = pcb+cur_id;
	ptc->state = 1;
	ptc->pid = cur_id;

	TrapFrame* tfc = &ptc->tf;
	memCpy((uint8_t*)tfc, (uint8_t*)tf, sizeof(TrapFrame));		//	TrapFrame
	tf->eax = ptc->pid;
	tfc->eax = 0;
	tfc->cs = USEL(SEG_UCODE2);
	tfc->ds = tfc->ss = USEL(SEG_UDATA2);

	utop[cur_id] = availableTop();
//	tfc->esp = tf->esp - ((utop[pcb_cur] - utop[cur_id])<<20);
	tfc->esp = tf->esp;
	
	//	kernel stack
	memCpy((uint8_t*)&ptc->stack, (uint8_t*)ptf->stack, MAX_STACK_SIZE);	

	//	user stack
//	memCpy((uint8_t*)((utop[cur_id]-1)<<20), (uint8_t*)((utop[pcb_cur]-1)<<20), 1<<20);
	memCpy((uint8_t*)(((utop[pcb_cur]-1)<<20)+0x100000), (uint8_t*)((utop[pcb_cur]-1)<<20), 1<<20);
	
	//	user code & data
	memCpy((uint8_t*)(0x300000), (uint8_t*)(0x200000), 0x3000);

	schedule();
}

extern Semaphore sem_pool[MAX_SEM_NUM];

void doSemInit(TrapFrame* tf) {
	int i = 0;
	int find = 0;
	for (; i < MAX_SEM_NUM; ++i) {
		if (!sem_pool[i].used) {
			find = 1;
			break;
		}
	}
	if (!find) {
		*((uint32_t*)tf->ebx) = 0;
		tf->eax = -1;
	}
	else {
		Semaphore* sem = sem_pool + i;
		sem->value = tf->ecx;
		outint((uint32_t)sem);
		outstr("sem->value = ");
		outint(sem->value);
		sem->used = 1;
		sem->front = 0;
		sem->rear = 0;
		tf->eax = *((uint32_t*)tf->ebx) = (uint32_t)sem;
		outstr("tf->eax = ");
		outhex(tf->eax);
		outhex((uint32_t)tf->ebx);
		outhex(*(uint32_t*)tf->ebx);
	}

	schedule();
}

void enqueue(Semaphore* sem, uint32_t cur) {
	if (sem->rear == MAX_PCB_NUM) sem->rear = 0;
	sem->queue[sem->rear++] = cur;	
}

void dequeue(Semaphore* sem) {
	++sem->front;
	if (sem->front == MAX_PCB_NUM) sem->front = 0;
}

void doSemPost(TrapFrame* tf) {
	Semaphore* sem = (Semaphore*)(*(uint32_t*)tf->ebx);
	++sem->value;
		outhex((uint32_t)sem);
		outstr("post : sem->value = ");
		outint(sem->value);
	if (sem->value <= 0) {
		uint32_t i = sem->queue[sem->front];
		pcb[i].state = 1;
		pcb[i].timeCount = 0;
		pcb[i].sleepTime = 0;

		if (sem->front == sem->rear) tf->eax = -1;
		else {
			dequeue(sem);
			tf->eax = 0;
		}
	}
	schedule();
}

void doSemWait(TrapFrame* tf) {
	Semaphore* sem = (Semaphore*)(*(uint32_t*)tf->ebx);
	--sem->value;
		outhex((uint32_t)sem);
		outstr("wait : sem->value = ");
		outint(sem->value);
	if (sem->value < 0) {
		uint32_t i = pcb_cur;
		pcb[i].state = 2;
		pcb[i].sleepTime = 2147483647;

		if ((sem->rear + MAX_PCB_NUM - sem->front + 1) % MAX_PCB_NUM == MAX_PCB_NUM) tf->eax = -1;
		else {
			enqueue(sem, i);
			tf->eax = 0;
		}
	}
	schedule();
}

void doSemDest(TrapFrame* tf) {
	Semaphore* sem = (Semaphore*)(*(uint32_t*)tf->ebx);
	sem->used = 0;
}

void syscallHandle(struct TrapFrame *tf) {
	/* 实现系统调用*/
	if (tf->eax==1) doPrintf(tf);				//	printf
	else if (tf->eax==2) doFork(tf);			//	fork
	else if (tf->eax==3) doSleep(tf);			//	sleep
	else if (tf->eax==4) doExit();				//	exit
	else if (tf->eax==5) doSemInit(tf);			//	sem_init
	else if (tf->eax==6) doSemPost(tf);			//	sem_post
	else if (tf->eax==7) doSemWait(tf);			//	sem_wait
	else if (tf->eax==8) doSemDest(tf);			//	sem_destroy
}

void timeInterruptHandle(TrapFrame* tf) {
	asm volatile("cli");
//	outstr("time interrupt");
	uint32_t i = 0;
	for (; i < MAX_PCB_NUM; ++i) {
		if (pcb[i].state == 0) {
			if (pcb[i].timeCount) --pcb[i].timeCount;
			if (!pcb[i].timeCount) pcb[i].state = 1;
		}
		if (pcb[i].state == 2) {
			if (pcb[i].sleepTime) --pcb[i].sleepTime;
			if (!pcb[i].sleepTime) pcb[i].state = 1;
		}
	}
	schedule();
}

void GProtectFaultHandle(struct TrapFrame *tf){
	assert(0);
	return;
}
