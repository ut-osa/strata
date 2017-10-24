/* Implementation for strong symbol of syscalldb,
   which will overwrite weak symbol in glibc by LD_PRELOAD */

.text
        .global syscalldb
        .type syscalldb, @function
        .extern shim_table

syscalldb:
        .cfi_startproc

		//pushq %rbp
		//movq  %rsp, %rbp
		pushq %rbx

		//int $3
        movq shim_table@GOTPCREL(%rip), %rbx
        movq (%rbx,%rax,8), %rbx
		
        cmp $0, (%rbx)
        je isundef

isdef:
	/* Kernel stores %rsp to %rdi before seting up 
	   a frame for signal handling. In entry_64.S
	   -----------------------------------------------
		GLOBAL(retint_user)
		mov %rsp,%rdi
		call    prepare_exit_to_usermode  //this calls do_signal().
		TRACE_IRQS_IRETQ
		SWAPGS
		jmp restore_regs_and_iret
	   -----------------------------------------------
	   %rdi must not be restored if a signal is delivered 
	   while handling io syscalls. 
	*/
        //pushq %rdi
        pushq %rsi
        pushq %rdx
        pushq %rcx
        pushq %r8
        pushq %r9
        pushq %r12
        pushq %r13
        pushq %r14
        pushq %r15

		//int $3
        call *%rbx

        popq %r15
        popq %r14
        popq %r13
        popq %r12
        popq %r9
        popq %r8
        popq %rcx
        popq %rdx
        popq %rsi
        //popq %rdi

        jmp ret

isundef:
        movq $-38, %rax

ret:
        popq %rbx
		//leaveq
        retq

        .cfi_endproc
        .size syscalldb, .-syscalldb
