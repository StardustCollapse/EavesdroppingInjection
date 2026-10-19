#include "Msr/Msr.h"
#include "Util.h"

BOOLEAN
Msr::SetMsrBitmap(ULONG_PTR MsrBitmap, ULONG64 Msr, BOOLEAN Read, BOOLEAN Write)
{

	if (!Read && !Write)
	{
		// Invalid Command
		return FALSE;
	}

	if (Msr <= 0x00001FFF)
	{
		if (Read)
		{
			Util::SetBit((PUCHAR)MsrBitmap, Msr);
		}
		if (Write)
		{
			Util::SetBit((PUCHAR)MsrBitmap + 2048, Msr);
		}
	}
	else if ((0xC0000000 <= Msr) && (Msr <= 0xC0001FFF))
	{
		if (Read)
		{
			Util::SetBit((PUCHAR)MsrBitmap + 1024, Msr - 0xC0000000);
		}
		if (Write)
		{
			Util::SetBit((PUCHAR)MsrBitmap + 3072, Msr - 0xC0000000);
		}
	}
	else
	{
		return FALSE;
	}
	return TRUE;
}