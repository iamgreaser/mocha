#include "common.h"

volatile uint32_t user_context[32 + 256];
volatile uint32_t kernel_gp;

volatile uint32_t last_epc0;
volatile uint32_t last_epc1;
volatile uint32_t last_epc2;
volatile uint32_t last_epc3;

asm (
".global isr_wrapper\n"
"isr_wrapper:\n"
	// start frame
	".set noat\n"
	".set noreorder\n"

	// get user context
	"la $k0, user_context+256*4\n"
	"lw $k1, 0($k0)\n"
	"nop\n"

	// dump registers
	"sw $1, 1*4($k0)\n"
	"sw $2, 2*4($k0)\n"
	"sw $3, 3*4($k0)\n"
	"sw $4, 4*4($k0)\n"
	"sw $5, 5*4($k0)\n"
	"sw $6, 6*4($k0)\n"
	"sw $7, 7*4($k0)\n"
	"sw $8, 8*4($k0)\n"
	"sw $9, 9*4($k0)\n"
	"sw $10, 10*4($k0)\n"
	"sw $11, 11*4($k0)\n"
	"sw $12, 12*4($k0)\n"
	"sw $13, 13*4($k0)\n"
	"sw $14, 14*4($k0)\n"
	"sw $15, 15*4($k0)\n"
	"sw $16, 16*4($k0)\n"
	"sw $17, 17*4($k0)\n"
	"sw $18, 18*4($k0)\n"
	"sw $19, 19*4($k0)\n"
	"sw $20, 20*4($k0)\n"
	"sw $21, 21*4($k0)\n"
	"sw $22, 22*4($k0)\n"
	"sw $23, 23*4($k0)\n"
	"sw $24, 24*4($k0)\n"
	"sw $25, 25*4($k0)\n"
	"sw $26, 26*4($k0)\n"
	"sw $27, 27*4($k0)\n"
	"sw $28, 28*4($k0)\n"
	"sw $29, 29*4($k0)\n" // SP
	"sw $30, 30*4($k0)\n"
	"sw $31, 31*4($k0)\n"
	"nop\n"

	// load new GP
	"la $k1, kernel_gp\n"
	"lw $gp, 0($k1)\n"
	"nop\n"

	// dump EPC location
	"mfc0 $2, $14\n" // EPC
	"nop\n"
	"sw $2, 0($k0)\n"
	"nop\n"

	// set stack and call function
	"addiu $sp, $k0, -4*32\n"
	"lui $k0, %hi(isr_handler)\n"
	"addiu $k0, $k0, %lo(isr_handler)\n"
	"jalr $k0\n" "addiu $a0, $sp, 4*32\n"
	"move $k0, $v0\n"

	// undump registers
	"nop\n"
	"lw $1, 1*4($k0)\n"
	"lw $2, 2*4($k0)\n"
	"lw $3, 3*4($k0)\n"
	"lw $4, 4*4($k0)\n"
	"lw $5, 5*4($k0)\n"
	"lw $6, 6*4($k0)\n"
	"lw $7, 7*4($k0)\n"
	"lw $8, 8*4($k0)\n"
	"lw $9, 9*4($k0)\n"
	"lw $10, 10*4($k0)\n"
	"lw $11, 11*4($k0)\n"
	"lw $12, 12*4($k0)\n"
	"lw $13, 13*4($k0)\n"
	"lw $14, 14*4($k0)\n"
	"lw $15, 15*4($k0)\n"
	"lw $16, 16*4($k0)\n"
	"lw $17, 17*4($k0)\n"
	"lw $18, 18*4($k0)\n"
	"lw $19, 19*4($k0)\n"
	"lw $20, 20*4($k0)\n"
	"lw $21, 21*4($k0)\n"
	"lw $22, 22*4($k0)\n"
	"lw $23, 23*4($k0)\n"
	"lw $24, 24*4($k0)\n"
	"lw $25, 25*4($k0)\n"
	//"lw $26, 26*4($k0)\n" // k0
	"lw $27, 27*4($k0)\n"
	"lw $28, 28*4($k0)\n"
	"lw $29, 29*4($k0)\n" // SP
	"lw $30, 30*4($k0)\n"
	"lw $31, 31*4($k0)\n"
	"nop\n"

	// undump EPC location
	"lw $k0, 0($k0)\n"
	"nop\n"

	// return
	"jr $k0\n"
	"rfe\n"

	".set reorder\n"
	".set at\n"
);

volatile int bus_read_one_fault = 0;
uint32_t * __attribute__((noinline)) isr_handler (uint32_t *sp)
{
	last_epc3 = last_epc2;
	last_epc2 = last_epc1;
	last_epc1 = last_epc0;
	last_epc0 = sp[0];
	uint32_t c0_status;
	uint32_t c0_cause;
	uint32_t c0_vaddr;
	asm (
		"mfc0 %0, $12\n"
		"mfc0 %1, $13\n"
		"mfc0 %2, $8\n"
		: "=r"(c0_status), "=r"(c0_cause), "=r"(c0_vaddr)
		:
		:
	); 
	int fault_code = ((c0_cause>>2)&15);

	//if(!bus_read_one_fault) printf("FAULT %08X %08X\n", c0_status, c0_cause);

	if((c0_status&0x30) != 0x00)
	{
		printf("FAULT WITHIN FAULT %08X\n", c0_status);
		printf("old EPCs: %08X %08X %08X %08X\n", last_epc0, last_epc1, last_epc2, last_epc3);
	}

	// interrupt
	if(fault_code == 0x00)
	{
		int iflags = (c0_cause>>8)&0xFF;

		if(iflags & 0x01)
		{
			puts("SWI #0");
			iflags &= ~0x01;
			c0_cause &= ~0x0100;
			asm volatile (
				"mtc0 %0, $13\n"
				:
				: "r"(c0_cause)
				:
			); 
		}

		if(iflags & 0x02)
		{
			puts("SWI #1");
			iflags &= ~0x02;
			c0_cause &= ~0x0200;
			asm volatile (
				"mtc0 %0, $13\n"
				:
				: "r"(c0_cause)
				:
			); 
		}

		if(iflags == 0x00)
		{
			return sp;
		}
	}

	// TLB miss?
	if(fault_code == 0x02 || fault_code == 0x03)
	{
		if(vmem_fetch_new_page(c0_vaddr))
		{
			// only remap virtual addresses!
			if((sp[0] & 0xC0000000) == 0x80000000)
				return sp;

			// FIXME: confirm if this is a bug in my CPU or a MIPS quirk
			//
			// check if epc needs to be remapped
			// otherwise kernel ends up faulting while unfaulting
			// and the fault handler is not reentrant!

			int32_t c0_index;
			asm volatile (
				"mtc0 %0, $10\n"
				"nop\n"
				"tlbp\n"
				"nop\n"
				"mfc0 %0, $0\n"
				"nop\n"
				: "=r"(c0_index)
				: "r"(sp[0])
				:
			); 

			if(c0_index >= 0)
				return sp;

			//printf("fetch %08X\n", sp[0]);
			if(vmem_fetch_new_page(sp[0]))
				return sp;
		}
	}

	/*
	uint32_t c0_index;
	asm volatile (
		"tlbp\n"
		"nop\n"
		"mfc0 %0, $0\n"
		"nop\n"
		: "=r"(c0_index)
		:
		:
	); 

	printf("INDEX = %08X\n", c0_index);
	*/

	// syscall?
	if(fault_code == 0x08)
	{
		switch(sp[4])
		{
			case 1:
				// TODO: extra flags
				// FIXME: need to prevent double-fault
				sp[7] = (uint32_t)open((const char *)sp[5], (int)sp[6]);
				sp[0] += 4;
				return sp;

			case 3: {
				// FIXME: need to prevent double-fault
				// FIXME: doesn't check if valid
				// get real address
				if(sp[7] + (sp[6]&0xFFF) > 0x1000)
					sp[7] = 0x1000 - (sp[6]&0xFFF);
				sp[7] = (uint32_t)read((int)sp[5], (void *)(sp[6],
					(ptab_direct[sp[6]>>12]&~0xFFF)|(sp[6]&0xFFF)|0xA0000000)
				, (size_t)sp[7]);
				sp[0] += 4;
				return sp;
			}

			case 4:
				// FIXME: need to prevent double-fault
				sp[7] = (uint32_t)write((int)sp[5], (const void *)sp[6], (size_t)sp[7]);
				sp[0] += 4;
				return sp;

			case 6:
				sp[7] = (uint32_t)close((int)sp[5]);
				sp[0] += 4;
				return sp;

			case 78:
				// FIXME: need to prevent double-fault
				sp[7] = gettimeofday((void *)sp[5], (void *)sp[6]);
				sp[0] += 4;
				return sp;
			default:
				printf("unhandled syscall %d %08X %08X %08X\n"
					, sp[4], sp[5], sp[6], sp[7]);
				break;
		}
	}

	// bus read?
	if(fault_code == 0x07 && bus_read_one_fault)
	{
		bus_read_one_fault = 0;
		sp[0] += 4;
		return sp;
	}

	fprintf(stderr, "FAULT %02X %08X %08X %08X\n", fault_code, c0_status, c0_cause, c0_vaddr);
	fprintf(stderr, "GPR dump (%08X):\n", sp);

	int i;
	for(i = 0; i < 32; i++)
	{
		fprintf(stderr, " %2d=%08X", i, sp[i]);

		if((i&3)==3)
			fprintf(stderr, "\n");
	}

	int32_t tlb_index;
	asm volatile (
		"mtc0 %1, $10\n"
		"nop\n"
		"tlbp\n"
		"nop\n"
		"mfc0 %0, $0\n"
		"nop\n"
		: "=r"(tlb_index)
		: "r"(sp[0])
		:
	);
	fprintf(stderr, "EPC TLBP: %08X\n", tlb_index);

	if(tlb_index >= 0)
	{
		int32_t elo, ehi;
		asm volatile (
			"tlbr\n"
			"nop\n"
			"mfc0 %0, $2\n"
			"nop\n"
			"mfc0 %1, $10\n"
			"nop\n"
			: "=r"(elo), "=r"(ehi)
			:
			:
		);
		fprintf(stderr, "EPC TLBR: %08X <- %08X\n", elo, ehi);
	}

	if(fault_code == 0x0A)
		fprintf(stderr, "EPC LW:   %08X\n", *(uint32_t *)(sp[0]));

	fprintf(stderr, "old EPCs: %08X %08X %08X %08X\n", last_epc0, last_epc1, last_epc2, last_epc3);
	fprintf(stderr, "- HALTED -\n", sp);

	for(;;)
		*(volatile uint32_t *)0xBFF00020 = 1;
}

void interrupt_setup(void)
{
	uint32_t isr = (uint32_t)isr_wrapper;

	// FIXME: find a more elegant way to make the optimiser not fuck up
	volatile uint32_t isr_real = (uint32_t)isr_handler;

	// regular exception handler
	*(volatile uint32_t *)0xBFC00180 = 0x3C1A0000 | (isr>>16);    // LUI k0,     %hi(isr)
	*(volatile uint32_t *)0xBFC00184 = 0x375A0000 | (isr&0xFFFF); // ORI k0, k0, %lo(isr)
	*(volatile uint32_t *)0xBFC00188 = 0x03400008 | (0x00000000); // JR k0
	*(volatile uint32_t *)0xBFC0018C = 0x00000000 | (0x00000000); // NOP
}

