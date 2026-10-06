/*
 * kernel_thread()'s two trampolines, split out of process.c.
 *
 * Clang emits an unnumbered ".file <name>" marker around file-scope
 * inline asm to bound the raw-text region for its assembler, in
 * addition to the numbered ".file N <path>" entries its own DWARF
 * line table uses under CONFIG_DEBUG_INFO. GNU as treats the first
 * ".file" it sees, numbered or not, as claiming slot 1, so the two
 * collide ("file number 1 already allocated") in any translation
 * unit combining -g with a file-scope asm() block. Real assembly
 * files carry no such marker, so moving this code here removes the
 * condition instead of working around the diagnostic.
 */
#include <linux/linkage.h>
#include <asm/unwind.h>

	.text

ENTRY(kernel_thread_helper)
#ifdef CONFIG_TRACE_IRQFLAGS
	bl	trace_hardirqs_on
#endif
	msr	cpsr_c, r7
	mov	r0, r4
	mov	lr, r6
	mov	pc, r5
ENDPROC(kernel_thread_helper)

#ifdef CONFIG_ARM_UNWIND
	.fnstart
	.cantunwind
ENTRY(kernel_thread_exit)
	bl	do_exit
	nop
ENDPROC(kernel_thread_exit)
	.fnend
#endif
