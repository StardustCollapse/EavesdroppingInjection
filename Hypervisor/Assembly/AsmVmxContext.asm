PUBLIC AsmVmxContextSaveState
PUBLIC AsmVmxContextRestoreState
PUBLIC AsmVmExitHandler
PUBLIC AsmDisableVmxOperationVmcall
PUBLIC AsmDisableVmxOperation
PUBLIC AsmInvept

EXTERN  InitializeVcpuState:PROC
EXTERN  VmExitDispatcher:PROC

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

	mov rcx, rsp

	sub rsp, 028h

	call InitializeVcpuState

	; int 3

	jmp AsmVmxContextRestoreState

AsmVmxContextSaveState ENDP

AsmVmxContextRestoreState PROC
	
	;add rsp, 028h

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

AsmDisableVmxOperationVmcall PROC

	mov rcx, 053544F505F4856h ; VMCALL_STOP_HV
	vmcall
	xor rax, rax
	ret

AsmDisableVmxOperationVmcall ENDP

AsmVmExitHandler PROC

	sub rsp, 28h
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

	mov rcx, rsp
	sub rsp, 20h
	call VmExitDispatcher
	add rsp, 20h

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
	add rsp, 28h

	vmresume

	; If vmresume fails:
	int 3

AsmVmExitHandler ENDP

AsmDisableVmxOperation PROC

	vmxoff

	mov cr0, rcx
	mov cr3, rdx
	mov cr4, r8
	mov rsp, r9

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

	; [rsp + 20h] = GuestSs
	; [rsp + 18h] = GuestRsp
	; [rsp + 10h] = GuestRflags
	; [rsp + 08h] = GuestCs
	; [rsp + 00h] = GuestRip

	iretq

AsmDisableVmxOperation ENDP

AsmInvept PROC
	invept rcx, oword ptr [rdx]
	jz Error
	jc Error
	xor rax, rax
	ret
Error:
	;the error information not detail
	mov rax, 1
	ret
AsmInvept ENDP

END
