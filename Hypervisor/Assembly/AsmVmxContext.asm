PUBLIC AsmVmxContextSaveState
PUBLIC AsmVmxContextRestoreState

EXTERN  InitializeVcpuState:PROC

.code

AsmVmxContextSaveState PROC
	pushfq

	push rax
	push rcx
	push rdx
	push rbx
	push rbp
	push rsi
	push rdi
	push r8
	push r9
	push r10
	push r11
	push r12
	push r13
	push r14
	push r15

	mov rbp, rsp

	and rsp, 0FFFFFFFFFFFFFFF0h
	
	sub rsp, 0100h

	mov rcx, rbp
	
	call InitializeVcpuState

	mov rsp, rbp

	int 3

	jmp AsmVmxContextRestoreState

AsmVmxContextSaveState ENDP

AsmVmxContextRestoreState PROC
	
	;add rsp, 0100h

	pop r15
	pop r14
	pop r13
	pop r12
	pop r11
	pop r10
	pop r9
	pop r8
	pop rdi
	pop rsi
	pop rbp
	pop rbx
	pop rdx
	pop rcx
	pop rax
	
	popfq

	ret
	
AsmVmxContextRestoreState ENDP

END
