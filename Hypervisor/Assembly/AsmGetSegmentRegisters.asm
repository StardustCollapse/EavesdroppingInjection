PUBLIC AsmGetCs
PUBLIC AsmGetDs
PUBLIC AsmGetEs
PUBLIC AsmGetSs
PUBLIC AsmGetFs
PUBLIC AsmGetGs
PUBLIC AsmGetLdtr
PUBLIC AsmGetTr
PUBLIC AsmGetGdtBase
PUBLIC AsmGetIdtBase
PUBLIC AsmGetGdtLimit
PUBLIC AsmGetIdtLimit

PUBLIC AsmGetRflags

.code

AsmGetCs PROC
	xor rax, rax
	mov	ax, cs
	ret
AsmGetCs ENDP

AsmGetDs PROC
	xor rax, rax
	mov	ax, ds
	ret
AsmGetDs ENDP

AsmGetEs PROC
	xor rax, rax
	mov	ax, es
	ret
AsmGetEs ENDP

AsmGetSs PROC
	xor rax, rax
	mov	ax, ss
	ret
AsmGetSs ENDP

AsmGetFs PROC
	xor rax, rax
	mov	ax, fs
	ret
AsmGetFs ENDP

AsmGetGs PROC
	xor rax, rax
	mov	ax, gs
	ret
AsmGetGs ENDP

AsmGetLdtr PROC
	xor rax, rax
	sldt ax
	ret
AsmGetLdtr ENDP

AsmGetTr PROC
	xor rax, rax
	str	ax
	ret
AsmGetTr ENDP

AsmGetGdtBase PROC

	LOCAL gdtr[10]:BYTE
	sgdt gdtr
	mov	rax, QWORD PTR gdtr[2]
	ret
AsmGetGdtBase ENDP

AsmGetGdtLimit PROC

	LOCAL gdtr[10]:BYTE

	sgdt gdtr
	xor rax, rax
	mov	ax, WORD PTR gdtr[0]
	ret

AsmGetGdtLimit ENDP

AsmGetIdtBase PROC

	LOCAL idtr[10]:BYTE
	
	sidt idtr
	mov	rax, QWORD PTR idtr[2]
	ret

AsmGetIdtBase ENDP

AsmGetIdtLimit PROC
	LOCAL idtr[10]:BYTE
	
	sidt idtr
	xor rax, rax
	mov	ax, WORD PTR idtr[0]
	ret

AsmGetIdtLimit ENDP

AsmGetRflags PROC
	pushfq
	pop rax
	ret
AsmGetRflags ENDP

END