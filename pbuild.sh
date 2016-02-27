#!/bin/sh

# grab labour.c from the ocmips project (it's in src/main/resources/)
# https://github.com/iamgreaser/ocmips/
export "CSRCS=\
	init.c \
	\
	fs.c isr.c panic.c vmem.c \
	\
	labour.c -lc -lm \
	"\

# set up your output directory here
export OUTDIR=

mipsel-none-elf-gcc -O1 -G0 -msoft-float -Wl,-Ttext-segment=0xA0005000 -o init.elf ${CSRCS} && \
cp init.elf "${OUTDIR}/" && \
mipsel-none-elf-strip "${OUTDIR}/init.elf" && \
mipsel-none-elf-objdump -p init.elf && \
ls -l init.elf "${OUTDIR}/init.elf" && \
true


