#include "common.h"

extern int installed_ram;
extern void *vspace_beg;
extern void *vspace_end;

static const uint8_t e_ident_match[16+8] = 
	"\x7F" "ELF\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00"
	"\x02\x00\x08\x00\x01\x00\x00\x00";

struct ElfHeader
{
	uint8_t e_ident[16];
	uint16_t e_type, e_machine;
	uint32_t e_version;
	uint32_t e_entry, e_phoff, e_shoff, e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize, e_phnum;
	uint16_t e_shentsize, e_shnum;
	uint16_t e_shstrndx;
};

struct ProgHeader
{
	uint32_t p_type;
	uint32_t p_offset;
	uint32_t p_vaddr;
	uint32_t p_paddr;
	uint32_t p_filesz;
	uint32_t p_memsz;
	uint32_t p_flags;
	uint32_t p_align;
};

int main(int argc, char *argv[])
{
	int i, j;

	printf("Kernel booting!\n\n");

	printf("Setting up interrupts..."); fflush(stdout);
	interrupt_setup();
	printf(" OK\n");
	
	printf("Calculating dedotated WAM..."); fflush(stdout);
	{
		char *base_addr = (char *)0xA0000000;
		char *walk_addr = (char *)0xA0000000;
		bus_read_one_fault = 1;

		while(bus_read_one_fault)
		{
			walk_addr += 0x1000;
			volatile char c = *walk_addr;
		}

		installed_ram = walk_addr - base_addr;
	}
	printf(" %dKB\n", installed_ram>>10);

	int used_ram = _end - (char *)0xA0000000;
	printf("Used RAM: %dKB\n", (used_ram+1023)>>10);
	printf("Free RAM: %dKB\n", (installed_ram-used_ram)>>10);

	printf("Setting up virtual memory..."); fflush(stdout);
	vmem_setup();
	printf(" OK\n");

	//printf("VMEM beg: %p\n", vspace_beg);
	//printf("VMEM end: %p\n", vspace_end);
	printf("Available pages: %d\n", (vspace_end-vspace_beg)>>12);

	// temporarily disabled so we don't end up calling sbrk
	/*
	printf("Probing filesystems..."); fflush(stdout);
	probe_filesystems();
	printf("OK\n");
	*/

	uint32_t dummy0, dummy1;
	printf("Enabling interrupts\n");
	asm volatile (
		// disable SWIs
		"mfc0 %0, $13\n"
		"nop\n"
		"lui %1, 0xFFFF\n"
		"ori %1, 0xFCFF\n"
		"and %0, %0, %1\n"
		"mtc0 %0, $13\n"
		"nop\n"

		// enable ints
		"mfc0 %0, $12\n"
		"nop\n"
		"ori %0, %0, 0x0001\n"
		"mtc0 %0, $12\n"
		: "=r"(dummy0), "=r"(dummy1)
	);
	printf("Beep boop.\n");

	printf("Opening lua.elf..."); fflush(stdout);
	fd_vmem = open("/lua.elf", O_RDONLY);
	if(fd_vmem < 3) panic("couldn't open program");
	printf(" OK\n");

	struct ElfHeader ehdr;
	read(fd_vmem, &ehdr, sizeof(struct ElfHeader));
	if(memcmp(ehdr.e_ident, e_ident_match, 16+8)
			|| ehdr.e_ehsize != 0x34
			|| ehdr.e_phentsize != 0x20
			)
		panic("program not a MIPS ELF file");

	// prefill vmem with stuff
	lseek(fd_vmem, ehdr.e_phoff, SEEK_SET);

	struct ProgHeader phdr;
	for(i = 0; i < ehdr.e_phnum; i++)
	{
		read(fd_vmem, &phdr, sizeof(struct ProgHeader));
		if(phdr.p_type == 1) //PT_LOAD
		{
			// TODO: not assume things here
			uint32_t vaddr = phdr.p_vaddr;
			uint32_t foffs = phdr.p_offset;
			vaddr >>= 12;
			foffs &= ~0xFFF;

			uint32_t pflags = (phdr.p_flags & 0x2 ? 0x501 : 0x101);
			for(j = 0; j < ((phdr.p_filesz+0xFFF)>>12); j++)
			{
				ptab_direct[vaddr] = foffs | pflags;
				ptab_old_val[vaddr] = foffs | pflags;
				vaddr += 1;
				foffs += 1<<12;
				//printf("%08X %08X\n", vaddr, foffs);
			}
		}
	}

	// Destroy reset vector
	*(volatile uint32_t *)0xBFC00000 = 0xFFFFFFFF;

	// Destroy EEPROM space
	memset((void *)0xA0001000, 0xFF, 0x1000);

	// this one deliberately crashes
	/*
	printf("Testing virtual memory...\n");
	ptab_direct[0x203] = 0x00001300;
	printf("TLB test: %08X\n", *(volatile uint32_t *)0x00203000);
	*(volatile uint32_t *)0x00203000 = 0x12345678;
	*/

	// this one does not
	/*
	printf("Testing virtual memory...\n");
	ptab_direct[0x203] = 0x1FF00700;
	printf("TLB test: %s\n", (const char *)0x00203200);
	*/
	
	// save GP
	asm volatile (
		"move %0, $gp\n"
		: "=r"(kernel_gp)
		:
		:
	);

	printf("Entry point: %08X - let's go!\n", ehdr.e_entry);
	vmem_fetch_new_page(ehdr.e_entry);
	vmem_fetch_new_page(0x3F00);
	asm volatile(
		".set noreorder\n"
		".set noat\n"
		"mfc0 %0, $12\n" // c0_status
		"nop\n"
		"srl %0, %0, 6\n"
		"sll %0, %0, 6\n"
		"ori %0, %0, 0x0003\n"
		// nuke the stack
		"la $sp, 0x3F00\n"
		"jr %1\n"
		"mtc0 %0, $12\n" // c0_status
		".set at\n"
		".set noreorder\n"
		: "+r"(dummy0)
		, "+r"((uint32_t)ehdr.e_entry)
		:
		:
	);
	//((void (*)())ehdr.e_entry)();

	/*
	printf("Testing virtual memory...\n");
	ptab_direct[1] = 0x1FF00700;
	printf("TLB test: %s\n", (const char *)0x00001200);
	*/

	panic("nothing left to do!");

	return 0;
}

