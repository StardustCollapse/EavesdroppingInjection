#include <intrin.h>
#include "VmExit/InjectEvent.h"
#include "VmExit/VmExit.h"
#include "Vmcs/Vmcs.h"

VOID
InjectEvent::EventInjectInterruption(INTERRUPT_TYPE InterruptionType, EXCEPTION_VECTORS Vector, BOOLEAN DeliverErrorCode, ULONG32 ErrorCode)
{
	INTERRUPT_INFORMATION InjectException = { 0 };
	InjectException.Fields.Valid = TRUE;
	InjectException.Fields.InterruptionType = InterruptionType;
	InjectException.Fields.Vector = Vector;
	InjectException.Fields.DeliverErrorCode = DeliverErrorCode;
	__vmx_vmwrite(Control_Fields::VM_Entry_Interruption_Information_Field, InjectException.Flags);

	if (DeliverErrorCode) {
		__vmx_vmwrite(Control_Fields::VM_Entry_Exception_Error_Code, ErrorCode);
	}
	return;
}

VOID
InjectEvent::EventInjectGeneralProtection()
{
	InjectEvent::EventInjectInterruption(INTERRUPTION_TYPE_HARDWARE_EXCEPTION, EXCEPTION_VECTOR_GENERAL_PROTECTION, TRUE, 0);
	ULONG64 ExitInstrLength;
	__vmx_vmread(Read_Only_Data_Fields::VM_Exit_Instruction_Length, &ExitInstrLength);
	__vmx_vmwrite(Control_Fields::VM_Entry_Instruction_Length, ExitInstrLength);
}