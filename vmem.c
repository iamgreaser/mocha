#include "common.h"

int installed_ram = 0;
void *vspace_beg = NULL;
void *vspace_end = NULL;

volatile uint32_t *ptab_direct = NULL;
volatile void *vmem_zero_page = NULL;
volatile uint32_t *ptab_rmap = NULL;
volatile uint32_t *ptab_old_val = NULL;
uint32_t vmem_page_count = 0;
uint32_t vmem_free_next = 0;

volatile int fd_vmem = -1;

void isr_utlb(void);
asm (
".global isr_utlb\n"
"isr_utlb:\n"
	// NO, I AM NOT A 6502 ADDICT.
	".set noat\n"
	".set noreorder\n"

	// TODO: get this damn thing to behave
	"j isr_wrapper\n"
	"nop\n"

	// check VAddr, is it in range?
	"mfc0 $k0, $8\n" // c0_vaddr
	"la $k1, " VSPACE_BYTES_STR "\n"
	//"la $k1, 0x3D000\n"
	"sltu $k1, $k0, $k1\n"
	"beqz $k1, isr_wrapper\n"
	"nop\n"

	// fetch table address - if NULL, call regular ISR
	"la $k1, ptab_direct\n"
	"lw $k1, 0($k1)\n"
	"nop\n" // load delay not emulated but it's good manners to add this nop
	"beqz $k1, isr_wrapper\n"
	"nop\n"

	// get other bits of BadVAddr (which is still in k0!)
	"srl $k0, $k0, 12\n" // Oh wait, now it's ruined.
	"sll $k0, $k0, 2\n"
	"addu $k0, $k0, $k1\n"

	// check validity and delegate to ordinary ISR if valid flag clear
	"lw $k1, 0($k0)\n" // k1 = prab_user_current[h][l]
	"nop\n"
	"andi $k1, $k1, 0x200\n"
	"beqz $k1, isr_wrapper\n"
	"nop\n"

	// valid. drop paddr + flags into c0_entrylo
	"lw $k1, 0($k0)\n"
	"nop\n"
	"srl $k1, $k1, 8\n"
	"sll $k1, $k1, 8\n"
	"mtc0 $k1, $2\n" // c0_entrylo

	// drop vaddr + ASID into c0_entryhi
	"lw $k1, 0($k0)\n"
	"nop\n"
	"andi $k1, $k1, 0x003F\n"
	"mfc0 $k0, $8\n" // c0_vaddr
	"sll $k1, $k1, 6\n"
	"srl $k0, $k0, 12\n"
	"sll $k0, $k0, 12\n"
	"or $k1, $k1, $k0\n"
	"mtc0 $k1, $10\n" // c0_entryhi

	// make new TLB entry
	"nop\n"
	"nop\n"
	"tlbwr\n"
	"nop\n"
	"nop\n"
	"nop\n"
	"nop\n"

	// return
	"mfc0 $k0, $14\n" // c0_epc
	"jr $k0\n"
	"rfe\n"

	"dud_address:\nb dud_address\nnop\n"
	".set reorder\n"
	".set at\n"
);

static void *vmem_new_page_do_alloc_rmap(int idx, uint32_t vaflags)
{
	uint32_t paddr = ((uint32_t)vspace_beg) + (idx<<12);
	ptab_rmap[idx] = vaflags;
	//ptab_old_val[vaflags>>12] = ptab_direct[vaflags>>12];
	ptab_direct[vaflags>>12] = (vaflags&0xFFF)|(paddr&0x1FFFFFFF);

	int32_t c0_index;
	asm volatile (
		"mtc0 %1, $10\n"
		"nop\n"
		"tlbp\n"
		"nop\n"
		"nop\n"
		"mfc0 %0, $0\n"
		"nop\n"
		: "=r"(c0_index)
		: "r"(vaflags&~0xFFF)
		: "memory"
	);

	if(c0_index < 0)
	{
		asm volatile (
			"mtc0 %1, $2\n"
			"nop\n"
			"mtc0 %0, $10\n"
			"nop\n"
			"tlbwr\n"
			"nop\n"
			:
			: "r"(vaflags&~0x0FF), "r"(paddr|((vaflags<<6)&0xFFF))
			:
		);

	} else {
		asm volatile (
			"mtc0 %1, $2\n"
			"nop\n"
			"mtc0 %0, $10\n"
			"nop\n"
			"tlbwi\n"
			"nop\n"
			:
			: "r"(vaflags&~0x0FF), "r"(paddr|((vaflags<<6)&0xFFF))
			:
		);
	}

	return (void *)paddr;
}

void *vmem_new_page(uint32_t vaflags)
{
	int idx;

	// TODO: range check

	// see if our page is taken
	if((ptab_direct[vaflags>>12] & 0x200) != 0)
		return vmem_new_page_do_alloc_rmap(
			(ptab_direct[vaflags>>12]-(uint32_t)vspace_beg)>>12, vaflags);

	// find a free page
	for(idx = 0; idx < vmem_page_count; idx++)
	{
		if(ptab_rmap[idx] == 0)
			return vmem_new_page_do_alloc_rmap(idx, vaflags);
	}

	// try to free a page
	for(idx = 0; idx < vmem_page_count; idx++,
		vmem_free_next = (vmem_free_next+1)%vmem_page_count)
	{
		uint32_t old_vaflags = ptab_rmap[vmem_free_next];
		uint32_t ent = ptab_direct[old_vaflags>>12];
		uint32_t bent = ptab_old_val[old_vaflags>>12];
		//printf("idx=%4d vaf=%08X dir=%08X old=%08X\n"
			//, vmem_free_next, old_vaflags, ent, bent);

		// check if file-backed RO
		if((ent&0x700) == 0x300 && (bent & 0x301) == 0x101)
		{
			// it is, free it and replace it

			// TLB flush
			uint32_t probe_addr = old_vaflags;
			int reps;
			for(reps = 0; reps < 5; reps++)
			{
				int32_t c0_index;
				asm volatile (
					"mtc0 %1, $10\n"
					"nop\n"
					"tlbp\n"
					"nop\n"
					"nop\n"
					"mfc0 %0, $0\n"
					: "=r"(c0_index)
					: "r"(probe_addr&~0xFFF)
					: "memory"
				);

				//printf("Probe: %08X (%08X)\n", c0_index, probe_addr);
				if(c0_index < 0) break;

				asm volatile (
					"mtc0 $0, $2\n"
					"nop\n"
					"mtc0 $0, $10\n"
					"nop\n"
					"tlbwi\n"
					"nop\n"
					"nop\n"
					:
					:
					: "memory"
				);
			}

			ptab_direct[old_vaflags>>12] = ptab_old_val[old_vaflags>>12];
			idx = vmem_free_next;
			vmem_free_next = (vmem_free_next+1) % vmem_page_count;
			return vmem_new_page_do_alloc_rmap(idx, vaflags);
		}
	}


	// well, shit.
	panic("no more free pages!");
}

void *vmem_set_fd_page(uint32_t offs)
{
	return NULL;
}

void vmem_setup(void)
{
	int i;

	// set up space for pages
	vspace_beg = (void *)((((uint32_t)_end)+0xFFF)&~0xFFF);
	vspace_beg += 0x1000;
	vspace_end = (void *)((0xA0000000+installed_ram)&~0xFFF);

	// set up paging table
	ptab_direct = vspace_beg;
	vspace_beg += ((VSPACE_BYTES>>10)+0xFFF)&~0xFFF;
	for(i = 0; i < (VSPACE_BYTES>>10); i++)
		ptab_direct[i] = 0x00000000;

	// allocate zero page
	vmem_zero_page = vspace_beg;
	vspace_beg += 0x1000;
	memset((void *)vmem_zero_page, 0, 0x1000);

	// allocate reverse map
	vmem_page_count = (vspace_end - vspace_beg)>>12;
	ptab_old_val = vspace_beg;
	ptab_rmap = vspace_beg;

	int pages_grabbed = 0;
	while((pages_grabbed<<10) < vmem_page_count)
	{
		vspace_beg += 0x2000;
		ptab_old_val += (0x1000>>2);
		vmem_page_count-=2;
		pages_grabbed++;
	}

	// set up reverse map
	for(i = 0; i < vmem_page_count; i++)
		ptab_rmap[i] = 0;

	// set up old value table
	for(i = 0; i < vmem_page_count; i++)
		ptab_old_val[i] = 0;

	// set up UTLB exception handler
	uint32_t isr = (uint32_t)isr_utlb;
	volatile uint32_t tmp = (uint32_t)isr_wrapper;

	*(volatile uint32_t *)0xBFC00100 = 0x3C1A0000 | (isr>>16);    // LUI k0,     %hi(isr)
	*(volatile uint32_t *)0xBFC00104 = 0x375A0000 | (isr&0xFFFF); // ORI k0, k0, %lo(isr)
	*(volatile uint32_t *)0xBFC00108 = 0x03400008 | (0x00000000); // JR k0
	*(volatile uint32_t *)0xBFC0010C = 0x00000000 | (0x00000000); // NOP
}

int vmem_fetch_new_page(uint32_t c0_vaddr)
{
	//
	if(!(c0_vaddr <= VSPACE_BYTES && c0_vaddr >= 0x1000))
	{
		// get address
		// NEVER SET PAGE 0, IT'S A BAD IDEA.
		printf("NULL POINTER EXCEPTION %08X\n", c0_vaddr);
		return 0;
	}

	if(c0_vaddr <= VSPACE_BYTES && c0_vaddr >= 0x1000)
	{
		//printf("fetching %08X - ent %d\n", c0_vaddr, c0_vaddr>>12);
		uint32_t ent = ptab_direct[c0_vaddr>>12];
		//printf("vaddr %08X ent %08X old %08X\n", c0_vaddr, ent, ptab_old_val[c0_vaddr>>12]);

		// valid?
		if((ent & 0x200) != 0)
		{
			// bang it into the TLB
			uint32_t elo = (((uint32_t)ent) & ~0x000000FF);
			uint32_t ehi = (((uint32_t)c0_vaddr) & ~0x00000FFF) | ((ent<<6)&0xFFF);

			//printf("applying entry: %08X <- %08X\n", elo, ehi);

			int32_t c0_index;
			asm volatile (
				"mtc0 %1, $10\n"
				"nop\n"
				"tlbp\n"
				"nop\n"
				"nop\n"
				"mfc0 %0, $0\n"
				"nop\n"
				: "=r"(c0_index)
				: "r"(ehi&~0xFFF)
				: "memory"
			);

			if(c0_index < 0)
			{
				asm volatile (
					"mtc0 %0, $2\n"
					"nop\n"
					"mtc0 %1, $10\n"
					"nop\n"
					"tlbwr\n"
					"nop\n"
					:
					: "r"(elo), "r"(ehi)
					:
				);

			} else {
				asm volatile (
					"mtc0 %0, $2\n"
					"nop\n"
					"mtc0 %1, $10\n"
					"nop\n"
					"tlbwi\n"
					"nop\n"
					:
					: "r"(elo), "r"(ehi)
					:
				);
			}

			return 1;

		} else {
			// invalid. allocate new page
			void *paddr = NULL;
			
			// TODO: set flags properly
			if(ent == 0)
			{
				// empty page
				paddr = vmem_new_page((c0_vaddr & ~0xFFF) | 0x700);
				memset(paddr, 0, 0x1000);

				//printf("remap %08X: NEW_PAGE -> %08X\n", c0_vaddr, (uint32_t)paddr);

				// return!
				return 1;
			} else if((ent & 0x301) == 0x101) {
				// file-backed page
				paddr = vmem_new_page((c0_vaddr & ~0xFFF) | (ent & 0x400) | 0x300);

				lseek(fd_vmem, (ent & ~0xFFF), SEEK_SET);
				//printf("remap %08X: %08X -> %08X\n", c0_vaddr, ent, paddr);
				//printf("ptent = %08X\n", ptab_direct[c0_vaddr>>12]);
				// load from file
				int loffs = 0;
				while(loffs < 0x1000)
				{
					int retamt = read(fd_vmem, paddr+loffs, 0x1000-loffs);
					if(retamt <= 0)
					{
						memset(paddr+loffs, 0, 0x1000-loffs);
						break;
					}

					loffs += retamt;
				}

				/*
				int i;
				for(i = 0; i < 1024; i++)
				{
					printf("%08X ", ((uint32_t *)paddr)[i]);
					if((i&7) == 7)
						printf("\n");
				}
				printf("remap %08X: %08X -> %08X\n", c0_vaddr, ent, paddr);
				*/

				/*
				uint32_t elo = (((uint32_t)paddr) & ~0xE0000FFF) | 0x300;
				uint32_t ehi = (((uint32_t)c0_vaddr) & ~0x00000FFF) | 0x000;
				uint32_t c0_index;
				asm volatile (
					"mtc0 %0, $2\n"
					"nop\n"
					"mtc0 %1, $10\n"
					"nop\n"
					"tlbwr\n"
					"nop\n"
					:
					: "r"(elo), "r"(ehi)
					:
				); 
				*/

				// return!
				return 1;
			}
		}

		printf("remap %08X: %08X -> ????????\n", c0_vaddr, ent);
	}

	return 0;
}

