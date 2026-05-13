	.file	"dispatcher_regs_32.c"
	.text
	.p2align 4
	.def	_hex_str;	.scl	3;	.type	32;	.endef
_hex_str:
LFB116:
	.cfi_startproc
	pushl	%esi
	.cfi_def_cfa_offset 8
	.cfi_offset 6, -8
	movl	$28, %ecx
	movl	%edx, %esi
	movl	%eax, %edx
	pushl	%ebx
	.cfi_def_cfa_offset 12
	.cfi_offset 3, -12
	movl	%eax, %ebx
	.p2align 4,,10
	.p2align 3
L2:
	movl	%esi, %eax
	addl	$1, %edx
	shrl	%cl, %eax
	subl	$4, %ecx
	andl	$15, %eax
	movzbl	_hex.0(%eax), %eax
	movb	%al, -1(%edx)
	cmpl	$-4, %ecx
	jne	L2
	movb	$0, 8(%ebx)
	popl	%ebx
	.cfi_restore 3
	.cfi_def_cfa_offset 8
	popl	%esi
	.cfi_restore 6
	.cfi_def_cfa_offset 4
	ret
	.cfi_endproc
LFE116:
	.p2align 4
	.def	_sprintf;	.scl	3;	.type	32;	.endef
_sprintf:
LFB95:
	.cfi_startproc
	subl	$28, %esp
	.cfi_def_cfa_offset 32
	leal	40(%esp), %eax
	movl	%eax, 8(%esp)
	movl	36(%esp), %eax
	movl	%eax, 4(%esp)
	movl	32(%esp), %eax
	movl	%eax, (%esp)
	call	___mingw_vsprintf
	addl	$28, %esp
	.cfi_def_cfa_offset 4
	ret
	.cfi_endproc
LFE95:
	.p2align 4
	.def	_write_msg;	.scl	3;	.type	32;	.endef
_write_msg:
LFB115:
	.cfi_startproc
	cmpb	$0, (%edx)
	je	L21
	pushl	%ebp
	.cfi_def_cfa_offset 8
	.cfi_offset 5, -8
	movl	%edx, %ebp
	pushl	%edi
	.cfi_def_cfa_offset 12
	.cfi_offset 7, -12
	pushl	%esi
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movl	%eax, %esi
	pushl	%ebx
	.cfi_def_cfa_offset 20
	.cfi_offset 3, -20
	subl	$572, %esp
	.cfi_def_cfa_offset 592
	leal	48(%esp), %ebx
	leal	40(%esp), %edi
	.p2align 4,,10
	.p2align 3
L16:
	movl	$0, 44(%esp)
	movl	44(%esp), %eax
	cmpb	$0, 0(%ebp,%eax)
	jne	L10
	jmp	L13
	.p2align 4,,10
	.p2align 3
L14:
	movl	44(%esp), %eax
	addl	$1, %eax
	movl	%eax, 44(%esp)
	movl	44(%esp), %eax
	cmpb	$0, 0(%ebp,%eax)
	je	L13
L10:
	movl	44(%esp), %eax
	cmpb	$10, 0(%ebp,%eax)
	jne	L14
L13:
	movl	44(%esp), %eax
	testl	%eax, %eax
	jne	L24
	addl	$1, %ebp
L15:
	cmpb	$0, 0(%ebp)
	jne	L16
	addl	$572, %esp
	.cfi_remember_state
	.cfi_def_cfa_offset 20
	popl	%ebx
	.cfi_restore 3
	.cfi_def_cfa_offset 16
	popl	%esi
	.cfi_restore 6
	.cfi_def_cfa_offset 12
	popl	%edi
	.cfi_restore 7
	.cfi_def_cfa_offset 8
	popl	%ebp
	.cfi_restore 5
	.cfi_def_cfa_offset 4
	ret
	.p2align 4,,10
	.p2align 3
L24:
	.cfi_restore_state
	movl	44(%esp), %eax
	movl	%ebp, 4(%esp)
	movl	%ebx, (%esp)
	movl	%eax, 8(%esp)
	call	_memcpy
	movl	44(%esp), %eax
	movb	$13, 48(%esp,%eax)
	movl	44(%esp), %eax
	movb	$10, 49(%esp,%eax)
	movl	44(%esp), %eax
	movl	$0, 16(%esp)
	addl	$2, %eax
	movl	%edi, 12(%esp)
	movl	%eax, 8(%esp)
	movl	%ebx, 4(%esp)
	movl	%esi, (%esp)
	call	*__imp__WriteFile@20
	.cfi_def_cfa_offset 572
	subl	$20, %esp
	.cfi_def_cfa_offset 592
	movl	44(%esp), %eax
	leal	1(%ebp,%eax), %ebp
	jmp	L15
L21:
	.cfi_def_cfa_offset 4
	.cfi_restore 3
	.cfi_restore 5
	.cfi_restore 6
	.cfi_restore 7
	ret
	.cfi_endproc
LFE115:
	.section .rdata,"dr"
	.align 4
LC0:
	.ascii "PASS: %s preserved across syscall\12\0"
	.align 4
LC1:
	.ascii "FAIL: %s changed (before=0x%s, after=0x%s)\12\0"
	.text
	.p2align 4
	.def	_check_reg;	.scl	3;	.type	32;	.endef
_check_reg:
LFB117:
	.cfi_startproc
	pushl	%ebp
	.cfi_def_cfa_offset 8
	.cfi_offset 5, -8
	pushl	%edi
	.cfi_def_cfa_offset 12
	.cfi_offset 7, -12
	pushl	%esi
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movl	%eax, %esi
	pushl	%ebx
	.cfi_def_cfa_offset 20
	.cfi_offset 3, -20
	subl	$204, %esp
	.cfi_def_cfa_offset 224
	cmpl	%ecx, %edx
	je	L29
	leal	46(%esp), %edi
	movl	%ecx, %ebx
	leal	55(%esp), %ebp
	movl	%edi, %eax
	call	_hex_str
	movl	%ebx, %edx
	movl	%ebp, %eax
	leal	64(%esp), %ebx
	call	_hex_str
	movl	%ebp, 16(%esp)
	movl	%edi, 12(%esp)
	movl	%esi, 8(%esp)
	movl	$LC1, 4(%esp)
	movl	%ebx, (%esp)
	call	_sprintf
	movl	$1, _test_fail
L27:
	movb	$0, 64(%esp,%eax)
	movl	224(%esp), %eax
	movl	%ebx, %edx
	call	_write_msg
	addl	$204, %esp
	.cfi_remember_state
	.cfi_def_cfa_offset 20
	popl	%ebx
	.cfi_restore 3
	.cfi_def_cfa_offset 16
	popl	%esi
	.cfi_restore 6
	.cfi_def_cfa_offset 12
	popl	%edi
	.cfi_restore 7
	.cfi_def_cfa_offset 8
	popl	%ebp
	.cfi_restore 5
	.cfi_def_cfa_offset 4
	ret
L29:
	.cfi_restore_state
	leal	64(%esp), %ebx
	movl	%eax, 8(%esp)
	movl	$LC0, 4(%esp)
	movl	%ebx, (%esp)
	call	_sprintf
	jmp	L27
	.cfi_endproc
LFE117:
	.def	___main;	.scl	2;	.type	32;	.endef
	.section .rdata,"dr"
LC2:
	.ascii "RESULT: FAILED\12\0"
LC3:
	.ascii "RESULT: ALL PASSED\12\0"
	.align 4
LC4:
	.ascii "Dispatcher register preservation test (PE32)\12\0"
LC5:
	.ascii "EBP\0"
LC6:
	.ascii "EFLAGS\0"
LC7:
	.ascii "ESI\0"
LC8:
	.ascii "EDI\0"
LC9:
	.ascii "EBX\0"
	.section	.text.startup,"x"
	.p2align 4
	.globl	_main
	.def	_main;	.scl	2;	.type	32;	.endef
_main:
LFB118:
	.cfi_startproc
	leal	4(%esp), %ecx
	.cfi_def_cfa 1, 0
	andl	$-16, %esp
	pushl	-4(%ecx)
	pushl	%ebp
	movl	%esp, %ebp
	.cfi_escape 0x10,0x5,0x2,0x75,0
	pushl	%edi
	pushl	%esi
	pushl	%ebx
	pushl	%ecx
	.cfi_escape 0xf,0x3,0x75,0x70,0x6
	.cfi_escape 0x10,0x7,0x2,0x75,0x7c
	.cfi_escape 0x10,0x6,0x2,0x75,0x78
	.cfi_escape 0x10,0x3,0x2,0x75,0x74
	subl	$72, %esp
	call	___main
	movl	$-11, (%esp)
	call	*__imp__GetStdHandle@4
	movl	$LC4, %edx
	movl	$195936478, -36(%ebp)
	subl	$4, %esp
	movl	%eax, %ebx
	call	_write_msg
/APP
 # 82 "/project/samples/dispatcher_regs_32/dispatcher_regs_32.c" 1
	pushf; popl %eax
movl %esi, %ecx
movl %edi, %edx
movl %ebx, %edi

 # 0 "" 2
/NO_APP
	movl	%eax, -44(%ebp)
	leal	-32(%ebp), %eax
	movl	%edi, -56(%ebp)
	movl	%edx, -52(%ebp)
	movl	%ecx, -48(%ebp)
	movl	%eax, (%esp)
	call	*__imp__QueryPerformanceCounter@4
	subl	$4, %esp
/APP
 # 95 "/project/samples/dispatcher_regs_32/dispatcher_regs_32.c" 1
	pushf; popl %eax
movl %esi, %ecx
movl %edi, %edx
movl %ebx, %edi

 # 0 "" 2
/NO_APP
	movl	%ebx, (%esp)
	movl	%eax, %esi
	movl	$LC5, %eax
	movl	%ecx, -60(%ebp)
	movl	-36(%ebp), %ecx
	movl	%edx, -64(%ebp)
	movl	$195936478, %edx
	call	_check_reg
	movl	%ebx, (%esp)
	movl	-44(%ebp), %edx
	movl	%esi, %ecx
	movl	$LC6, %eax
	call	_check_reg
	movl	%ebx, (%esp)
	movl	-60(%ebp), %ecx
	movl	$LC7, %eax
	movl	-48(%ebp), %edx
	call	_check_reg
	movl	%ebx, (%esp)
	movl	-64(%ebp), %ecx
	movl	$LC8, %eax
	movl	-52(%ebp), %edx
	call	_check_reg
	movl	%ebx, (%esp)
	movl	-56(%ebp), %edx
	movl	%edi, %ecx
	movl	$LC9, %eax
	call	_check_reg
	movl	_test_fail, %eax
	movl	$LC2, %edx
	testl	%eax, %eax
	movl	$LC3, %eax
	cmove	%eax, %edx
	movl	%ebx, %eax
	call	_write_msg
	movl	_test_fail, %eax
	movl	%eax, (%esp)
	call	*__imp__ExitProcess@4
	.cfi_endproc
LFE118:
	.section .rdata,"dr"
	.align 4
_hex.0:
	.ascii "0123456789abcdef\0"
.lcomm _test_fail,4,4
	.ident	"GCC: (GNU) 10-win32 20220113"
	.def	___mingw_vsprintf;	.scl	2;	.type	32;	.endef
	.def	_memcpy;	.scl	2;	.type	32;	.endef
