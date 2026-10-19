#include <iostream>
#include <Windows.h>
#include <conio.h>

#define IOCTL_HV_START \
CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_READ_DATA | FILE_WRITE_DATA)

#define IOCTL_HV_STOP \
CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_READ_DATA | FILE_WRITE_DATA)

int main()
{
	DWORD ErrorNum;
	HANDLE Handle;

	Handle = CreateFile(L"\\\\.\\EIControlCenter",
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL);

	if (Handle == INVALID_HANDLE_VALUE)
	{
		ErrorNum = GetLastError();
		std::cout << "[*] CreateFile failed: " << ErrorNum << "\n";
		std::cout << "Make sure the driver service is loaded.\n";
		std::cout << "Press any key to exit...\n";
		_getch();
		return 1;
	}

	std::cout << "========================================\n";
	std::cout << "          EIHvControlCenter             \n";
	std::cout << "========================================\n";
	std::cout << "[+] Connected to EIHvCore driver successfully.\n\n";

	bool Running = true;
	while (Running)
	{
		std::cout << "----------------------------------------\n";
		std::cout << "Select Operation:\n";
		std::cout << "  [1] Start Hypervisor (IOCTL_HV_START)\n";
		std::cout << "  [2] Stop Hypervisor (IOCTL_HV_STOP / VMCALL)\n";
		std::cout << "  [0] Exit Control Center\n";
		std::cout << "Choice > ";

		char Choice = _getch();
		std::cout << Choice << "\n\n";

		if (Choice == 0 || Choice == 0xE0)
		{
			_getch();
			std::cout << "[!] Invalid option. Please enter 1, 2, or 0.\n\n";
			continue;
		}

		if (Choice == '1')
		{
			std::cout << "[*] Sending IOCTL_HV_START...\n";
			BOOL Result = DeviceIoControl(Handle, IOCTL_HV_START, NULL, 0, NULL, 0, NULL, NULL);
			if (Result)
			{
				std::cout << "[+] Hypervisor started and EPT hooks armed successfully!\n\n";
			}
			else
			{
				std::cout << "[-] IOCTL_HV_START failed. Error code: " << GetLastError() << "\n\n";
			}
		}
		else if (Choice == '2')
		{
			std::cout << "[*] Sending IOCTL_HV_STOP...\n";
			std::cout << "[*] Quiescing hooks, waiting for active threads to drain...\n";
			BOOL Result = DeviceIoControl(Handle, IOCTL_HV_STOP, NULL, 0, NULL, 0, NULL, NULL);
			if (Result)
			{
				std::cout << "[+] Hypervisor stopped and unloaded cleanly via VMCALL (0xCE prevented)!\n\n";
			}
			else
			{
				std::cout << "[-] IOCTL_HV_STOP failed. Error code: " << GetLastError() << "\n\n";
			}
		}
		else if (Choice == '0' || Choice == 27) // 27 = ESC
		{
			Running = false;
		}
		else
		{
			std::cout << "[!] Invalid option. Please enter 1, 2, or 0.\n\n";
		}
	}

	CloseHandle(Handle);
	std::cout << "[*] Handle closed. Exiting Control Center.\n";
	return 0;
}