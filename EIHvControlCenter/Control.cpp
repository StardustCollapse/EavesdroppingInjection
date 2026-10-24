#include <iostream>
#include <Windows.h>
#include <winioctl.h>
#include <conio.h>
#include "RdtscBenchmark.h"
#include "../EIHvCore/Header/Driver/Ioctl.h"

static void LaunchPythonVisualizer()
{
	std::cout << "[*] Launching Python Visualizer...\n";
	if (GetFileAttributesW(L"visualize_benchmark.py") != INVALID_FILE_ATTRIBUTES)
	{
		system("python visualize_benchmark.py");
	}
	else if (GetFileAttributesW(L"EIHvControlCenter\\visualize_benchmark.py") != INVALID_FILE_ATTRIBUTES)
	{
		system("python EIHvControlCenter\\visualize_benchmark.py");
	}
	else if (GetFileAttributesW(L"..\\EIHvControlCenter\\visualize_benchmark.py") != INVALID_FILE_ATTRIBUTES)
	{
		system("python ..\\EIHvControlCenter\\visualize_benchmark.py");
	}
	else if (GetFileAttributesW(L"..\\visualize_benchmark.py") != INVALID_FILE_ATTRIBUTES)
	{
		system("python ..\\visualize_benchmark.py");
	}
	else if (GetFileAttributesW(L"..\\..\\visualize_benchmark.py") != INVALID_FILE_ATTRIBUTES)
	{
		system("python ..\\..\\visualize_benchmark.py");
	}
	else if (GetFileAttributesW(L"..\\..\\EIHvControlCenter\\visualize_benchmark.py") != INVALID_FILE_ATTRIBUTES)
	{
		system("python ..\\..\\EIHvControlCenter\\visualize_benchmark.py");
	}
	else
	{
		std::cout << "[-] visualize_benchmark.py not found.\n";
	}
}

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

	std::cout << "========================================\n";
	std::cout << "          EIHvControlCenter             \n";
	std::cout << "========================================\n";

	if (Handle == INVALID_HANDLE_VALUE)
	{
		ErrorNum = GetLastError();
		std::cout << "[!] Notice: CreateFile failed (" << ErrorNum << "). Driver service not connected.\n";
		std::cout << "    [1, 2] Hypervisor controls require driver service.\n";
		std::cout << "    [3] RDTSC Benchmark is fully operational in standalone mode.\n\n";
	}
	else
	{
		std::cout << "[+] Connected to EIHvCore driver successfully.\n\n";
	}

	bool Running = true;
	while (Running)
	{
		std::cout << "----------------------------------------\n";
		std::cout << "Select Operation:\n";
		std::cout << "  [1] Start Hypervisor (IOCTL_HV_START)\n";
		std::cout << "  [2] Stop Hypervisor (IOCTL_HV_STOP / VMCALL)\n";
		std::cout << "  [3] Run RDTSC Benchmark (CPUID / MSR / EPT Syscall)\n";
		std::cout << "  [4] Generate Visual Charts (Python Matplotlib)\n";
		std::cout << "  [0] Exit Control Center\n";
		std::cout << "Choice > ";

		char Choice = _getch();
		std::cout << Choice << "\n\n";

		if (Choice == 0 || Choice == 0xE0)
		{
			_getch();
			std::cout << "[!] Invalid option. Please enter 1, 2, 3, or 0.\n\n";
			continue;
		}

		if (Choice == '1')
		{
			if (Handle == INVALID_HANDLE_VALUE)
			{
				std::cout << "[-] Driver is not loaded. Cannot send IOCTL_HV_START.\n\n";
				continue;
			}
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
			if (Handle == INVALID_HANDLE_VALUE)
			{
				std::cout << "[-] Driver is not loaded. Cannot send IOCTL_HV_STOP.\n\n";
				continue;
			}
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
		else if (Choice == '3')
		{
			std::cout << "----------------------------------------\n";
			std::cout << "RDTSC High-Frequency Benchmark Configuration:\n";
			std::cout << "  [1] Standard Benchmark (10,000 iterations, Core #0)\n";
			std::cout << "  [2] Deep Benchmark (100,000 iterations, Core #0)\n";
			std::cout << "  [3] Quick Benchmark (1,000 iterations, Core #0)\n";
			std::cout << "Choice [Default=1] > ";

			char SubChoice = _getch();
			std::cout << SubChoice << "\n\n";

			uint32_t iterations = 10000;
			if (SubChoice == '2') iterations = 100000;
			else if (SubChoice == '3') iterations = 1000;

			RdtscBenchmark::RunAll(Handle, iterations, 0);

			std::cout << "[*] Generate visual chart with Python? (Y/N) > ";
			char VisChoice = _getch();
			std::cout << VisChoice << "\n\n";
			if (VisChoice == 'Y' || VisChoice == 'y')
			{
				LaunchPythonVisualizer();
			}
		}
		else if (Choice == '4')
		{
			LaunchPythonVisualizer();
			std::cout << "\n";
		}
		else if (Choice == '0' || Choice == 27) // 27 = ESC
		{
			Running = false;
		}
		else
		{
			std::cout << "[!] Invalid option. Please enter 1, 2, 3, or 0.\n\n";
		}
	}

	if (Handle != INVALID_HANDLE_VALUE)
	{
		CloseHandle(Handle);
		std::cout << "[*] Handle closed.\n";
	}
	std::cout << "[*] Exiting Control Center.\n";
	return 0;
}
