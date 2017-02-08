	.file	"handle.c"
	.section	.rodata
.LC0:
	.string	"Nice try.\n"
	.text
	.globl	handler
	.type	handler, @function
handler:
.LFB2:
	.cfi_startproc
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	subq	$32, %rsp
	movl	%edi, -20(%rbp)
	movl	$1, -12(%rbp)
	movl	-12(%rbp), %eax
	movl	$10, %edx
	movl	$.LC0, %esi
	movl	%eax, %edi
	call	write
	movq	%rax, -8(%rbp)
	cmpq	$10, -8(%rbp)
	je	.L1
	movl	$-999, %edi
	call	exit
.L1:
	leave
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
.LFE2:
	.size	handler, .-handler
	.section	.rodata
.LC1:
	.string	"exiting\n"
	.text
	.globl	user_handler
	.type	user_handler, @function
user_handler:
.LFB3:
	.cfi_startproc
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	subq	$32, %rsp
	movl	%edi, -20(%rbp)
	movl	$1, -12(%rbp)
	movl	-12(%rbp), %eax
	movl	$9, %edx
	movl	$.LC1, %esi
	movl	%eax, %edi
	call	write
	movq	%rax, -8(%rbp)
	cmpq	$9, -8(%rbp)
	je	.L4
	movl	$-999, %edi
	call	exit
.L4:
	movl	$1, %edi
	call	exit
	.cfi_endproc
.LFE3:
	.size	user_handler, .-user_handler
	.section	.rodata
.LC2:
	.string	"%d\n"
.LC3:
	.string	"Still here"
	.text
	.globl	main
	.type	main, @function
main:
.LFB4:
	.cfi_startproc
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	subq	$64, %rsp
	movl	%edi, -52(%rbp)
	movq	%rsi, -64(%rbp)
	movq	$1, -32(%rbp)
	call	getpid
	movl	%eax, -36(%rbp)
	movl	-36(%rbp), %eax
	movl	%eax, %esi
	movl	$.LC2, %edi
	movl	$0, %eax
	call	printf
	movl	$handler, %esi
	movl	$2, %edi
	call	Signal
	movl	$user_handler, %esi
	movl	$10, %edi
	call	Signal
.L8:
	leaq	-16(%rbp), %rdx
	leaq	-32(%rbp), %rax
	movq	%rdx, %rsi
	movq	%rax, %rdi
	call	nanosleep
	testl	%eax, %eax
	jns	.L6
	call	__errno_location
	movl	(%rax), %eax
	cmpl	$4, %eax
	jne	.L6
	movq	-16(%rbp), %rax
	movq	%rax, -32(%rbp)
	movq	-8(%rbp), %rax
	movq	%rax, -24(%rbp)
	jmp	.L7
.L6:
	movq	$1, -32(%rbp)
	movq	$0, -24(%rbp)
.L7:
	movl	$.LC3, %edi
	call	puts
	jmp	.L8
	.cfi_endproc
.LFE4:
	.size	main, .-main
	.ident	"GCC: (Ubuntu 4.8.2-19ubuntu1) 4.8.2"
	.section	.note.GNU-stack,"",@progbits
