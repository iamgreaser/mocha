#include "common.h"

uint32_t reg_dump[31];
void __attribute__((noreturn)) panic_core(const char *msg)
{
	fprintf(stderr, "PANIC: %s\n", msg);
	fprintf(stderr, "GPR dump:\n");

	int i;
	for(i = 0; i < 32; i++)
	{
		if(i == 0)
			fprintf(stderr, " %2d=--------", i);
		else
			fprintf(stderr, " %2d=%08X", i, reg_dump[i-1]);

		if((i&3)==3)
			fprintf(stderr, "\n");

	}

	// TEST: force address fault
	//*(volatile uint32_t *)0x040000C3 = 0;

	fprintf(stderr, "Kernel halted.\n");

	for(;;)
		*(volatile uint32_t *)0xBFF00020 = 1;
}

void __attribute__((noreturn)) panic(const char *msg)
{
	// dump all registers somewhere
	asm volatile (
		// start frame
		".set noat\n"
		"addiu $sp, $sp, -4\n"
		"sw $1, 0($sp)\n"

		// get pointer to reg_dump
		"lui $1, %%hi(%0)\n"
		"addiu $1, %%lo(%0)\n"

		// dump registers
		"sw $2, 1*4($1)\n"
		"sw $3, 2*4($1)\n"
		"sw $4, 3*4($1)\n"
		"sw $5, 4*4($1)\n"
		"sw $6, 5*4($1)\n"
		"sw $7, 6*4($1)\n"
		"sw $8, 7*4($1)\n"
		"sw $9, 8*4($1)\n"
		"sw $10, 9*4($1)\n"
		"sw $11, 10*4($1)\n"
		"sw $12, 11*4($1)\n"
		"sw $13, 12*4($1)\n"
		"sw $14, 13*4($1)\n"
		"sw $15, 14*4($1)\n"
		"sw $16, 15*4($1)\n"
		"sw $17, 16*4($1)\n"
		"sw $18, 17*4($1)\n"
		"sw $19, 18*4($1)\n"
		"sw $20, 19*4($1)\n"
		"sw $21, 20*4($1)\n"
		"sw $22, 21*4($1)\n"
		"sw $23, 22*4($1)\n"
		"sw $24, 23*4($1)\n"
		"sw $25, 24*4($1)\n"
		"sw $26, 25*4($1)\n"
		"sw $27, 26*4($1)\n"
		"sw $28, 27*4($1)\n"
		"sw $29, 28*4($1)\n"
		"sw $30, 29*4($1)\n"
		"sw $31, 30*4($1)\n"

		// pop $at into $t0 and dump
		"lw $t0, 0($sp)\n"
		"sw $t0, 0*4($1)\n"

		// end frame
		"addiu $sp, $sp, 4\n"
		".set at\n"
		:
		: "i"(reg_dump)
		: "t0", "memory"
	);

	// panic properly
	panic_core(msg);
}


