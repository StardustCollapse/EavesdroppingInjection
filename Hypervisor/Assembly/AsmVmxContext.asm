PUBLIC AsmVmxContextSaveState
PUBLIC AsmVmxContextRestoreState
PUBLIC AsmVmExitHandler
PUBLIC AsmDisableVmxOperationVmcall
PUBLIC AsmDisableVmxOperation
PUBLIC AsmInvept
PUBLIC AsmPostHookCommonEntry
PUBLIC AsmPostHookReturnStub

EXTERN  InitializeVcpuState:PROC
EXTERN  VmExitDispatcher:PROC
EXTERN  PostHookOnEntry:PROC
EXTERN  PostHookOnReturn:PROC

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
	;the error information isn't detail
	mov rax, 1
	ret
AsmInvept ENDP
 
; ==============================================================================
; AsmPostHookCommonEntry
; Input:
;   r10 = Pointer to EPT_POST_HOOK_ENTRY
;   [rsp] = Caller's Original Return RIP
; ==============================================================================
AsmPostHookCommonEntry PROC
    pushfq
    push rax
    push rcx
    push rdx
    push r8
    push r9
    push r10
    push r11

    sub rsp, 48h
    movdqu [rsp + 00h], xmm0
    movdqu [rsp + 10h], xmm1
    movdqu [rsp + 20h], xmm2
    movdqu [rsp + 30h], xmm3

    sub rsp, 020h

    ; PEPT_POST_HOOK_ENTRY Entry
    mov rcx, r10

    ; PVOID* pCallerReturnRipSlot = &[rsp + 0A8h]
    lea rdx, [rsp + 0A8h]

    ; ULONG64 CallerRsp = [rsp + 0B0h]
    lea r8, [rsp + 0B0h]

    ; PULONG64 pSavedArgs (saved RCX at [rsp + 090h])
    lea r9, [rsp + 090h]

    ; Returns OriginalTrampoline!!!
    call PostHookOnEntry

    ; Write OriginalTrampoline into saved R11 slot at [rsp + 068h]
    mov [rsp + 068h], rax

    add rsp, 020h

    movdqu xmm0, [rsp + 00h]
    movdqu xmm1, [rsp + 10h]
    movdqu xmm2, [rsp + 20h]
    movdqu xmm3, [rsp + 30h]
    add rsp, 048h

    pop r11 ; r11 now contains OriginalTrampoline!!!
    pop r10
    pop r9
    pop r8
    pop rdx
    pop rcx
    pop rax
    popfq

    ; Jump to original function trampoline!
    ; [rsp] is now AsmPostHookReturnStub!!!
    jmp r11
AsmPostHookCommonEntry ENDP

; ==============================================================================
; AsmPostHookReturnStub
; Landed here via 'ret' from TargetFunction!
; ==============================================================================
AsmPostHookReturnStub PROC
    pushfq      ; [CallerRsp - 08h]
    push rax    ; [CallerRsp - 10h] -> ReturnValue!
    push rcx    ; [CallerRsp - 18h]
    push rdx    ; [CallerRsp - 20h]
    push r8     ; [CallerRsp - 28h]
    push r9     ; [CallerRsp - 30h]
    push r10    ; [CallerRsp - 38h]
    push r11    ; [CallerRsp - 40h] -> saved r11 slot!
    push rbx    ; [CallerRsp - 48h]

    sub rsp, 068h
    movdqu [rsp + 00h], xmm0
    movdqu [rsp + 10h], xmm1
    movdqu [rsp + 20h], xmm2
    movdqu [rsp + 30h], xmm3
    movdqu [rsp + 40h], xmm4
    movdqu [rsp + 50h], xmm5

    ; Allocate local variable for OutOriginalReturnRip + shadow space:
    sub rsp, 030h

    ; ULONG64 ReturnValue -> [rsp + 0D0h]
    mov rcx, [rsp + 0D0h]

    ; ULONG64 CurrentRsp -> [rsp + 0E0h]
    lea rdx, [rsp + 0E0h]

    ; PULONG64 pOutOriginalReturnRip
    lea r8, [rsp + 020h]

    ; Reserved / Context pointer
    xor r9, r9

    call PostHookOnReturn

    ; Update RAX
    mov [rsp + 0D0h], rax

    ; Read OutOriginalReturnRip into RAX
    mov rax, [rsp + 020h]

    ; Write OutOriginalReturnRip into saved R11 slot -> [rsp + 0A0h]
    mov [rsp + 0A0h], rax

    add rsp, 30h

    movdqu xmm0, [rsp + 00h]
    movdqu xmm1, [rsp + 10h]
    movdqu xmm2, [rsp + 20h]
    movdqu xmm3, [rsp + 30h]
    movdqu xmm4, [rsp + 40h]
    movdqu xmm5, [rsp + 50h]
    add rsp, 68h

    pop rbx
    pop r11 ; r11 now holds OutOriginalReturnRip!
    pop r10
    pop r9
    pop r8
    pop rdx
    pop rcx
    pop rax ; rax holds final ReturnValue!
    popfq

    ; Jump to caller's original return address!
    jmp r11
AsmPostHookReturnStub ENDP

END
