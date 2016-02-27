#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>

#include <fcntl.h>
#include <sys/time.h>
#include <unistd.h>

#include <math.h>

#define STYP_NUL 0
#define STYP_BOL 2
#define STYP_STR 4
#define STYP_INT 6
#define STYP_FLT 8
#define STYP_HDL 10

#define SYS_ARG_TYP(n) (*(volatile int32_t *)(0xBFF00304+(n)*8))
#define SYS_ARG_INT(n) (*(volatile int32_t *)(0xBFF00300+(n)*8))
#define SYS_ARG_FLT(n) (*(volatile float *)(0xBFF00300+(n)*8))
#define SYS_ARG_STR(n) (*(volatile const char **)(0xBFF00300+(n)*8))

extern char _end[];

extern const char *mclib_find_device(const char *dtyp);
extern int mclib_gpu_get_resolution(int *w, int *h);
extern void mclib_gpu_set_fg(int rgb, int is_pal);
extern void mclib_gpu_set_bg(int rgb, int is_pal);
extern void mclib_gpu_fill(int x, int y, int w, int h, const char *cs);
extern void mclib_gpu_copy(int x, int y, int w, int h, int dx, int dy);
extern void mclib_gpu_set(int x, int y, const char *s);
extern void mclib_gpu_set_pal(int idx, int rgb);
extern int gpu_x;
extern int gpu_y;

int mount_filesystem(const char *address, const char *path);
ssize_t get_abs_correct_path(char *dst, const char *src, const char *cwd, size_t dst_len);

// fs.c
void probe_filesystems(void);

// isr.c
extern volatile int bus_read_one_fault;
extern volatile uint32_t kernel_gp;
void isr_wrapper(void);
void interrupt_setup(void);

// panic.c
void __attribute__((noreturn)) panic_core(const char *msg);
void __attribute__((noreturn)) panic(const char *msg);

// vmem.c
extern volatile uint32_t *ptab_direct;
extern volatile uint32_t *ptab_old_val;
extern volatile int fd_vmem;
void *vmem_new_page(uint32_t vaflags);
int vmem_fetch_new_page(uint32_t c0_vaddr);
void vmem_setup(void);

// 8MB
//define VSPACE_BYTES 0x800000
//define VSPACE_BYTES_STR "0x800000"
// 4MB
#define VSPACE_BYTES 0x400000
#define VSPACE_BYTES_STR "0x400000"

