# EavesdroppingInjection (EIHvCore)

A lightweight Windows x64 kernel-mode hypervisor built on Intel VT-x (VMX) and Extended Page Tables (EPT), designed for stealth kernel-level hooking (Stealth EPT Hooking) and dynamic behavior monitoring.

[English](README_EN.md) | [繁體中文](README.md)

---

## Table of Contents
- [Project Overview](#project-overview)
- [Driver Installation and Environment Setup](#driver-installation-and-environment-setup)
  - [One-Time Boot with Driver Signature Enforcement Disabled (Recommended)](#1-one-time-boot-with-driver-signature-enforcement-disabled-recommended)
  - [Test-Signing Mode (Alternative)](#2-test-signing-mode-alternative)
  - [Service Creation and Management](#3-service-creation-and-management)
- [Usage Instructions](#usage-instructions)
- [Feature Implementation Status](#feature-implementation-status)
  - [Implemented Features](#implemented-features)
  - [Planned / Unimplemented Features](#planned--unimplemented-features)
- [Project Architecture and Directory Structure](#project-architecture-and-directory-structure)
- [License](#license)

---

## Project Overview

`EavesdroppingInjection` consists of two main components:
1. **EIHvCore (Kernel-Mode Driver)**: Leverages Intel VT-x hardware virtualization to establish a Type-2 hypervisor beneath the Windows kernel. By employing EPT page splitting and read/write vs. execute separation (Split-page), it achieves PatchGuard (Kernel Patch Protection, KPP) immune Pre-Hook and Post-Hook capabilities.
2. **EIHvControlCenter (User-Mode CLI)**: A user-space console utility communicating with the kernel driver to govern the Hypervisor lifecycle (loading, launching, and quiescing/unloading).

---

## Driver Installation and Environment Setup

Because this driver is a custom kernel-mode driver without a Microsoft WHQL / EV digital certificate, 64-bit Windows requires temporarily bypassing driver signature verification.

### 1. One-Time Boot with Driver Signature Enforcement Disabled (Recommended)

> [!TIP]
> This method **does not alter persistent boot settings**. Upon the next reboot, normal driver signature verification is automatically restored, making it ideal for safe debugging and testing.

1. **Access Advanced Startup Menu**:
   - Hold down the `Shift` key and click **Restart** in the Windows Start menu.
   - Alternatively, open an elevated Command Prompt/PowerShell and execute:
     ```cmd
     shutdown /r /o /f /t 00
     ```
2. **Navigate the Options**:
   - Select **Troubleshoot** $\rightarrow$
   - Select **Advanced options** $\rightarrow$
   - Select **Startup Settings** $\rightarrow$
   - Click **Restart**.
3. **Disable Signature Enforcement**:
   - Upon reboot, a list of startup settings will appear. Press **`7`** or **`F7`** (**Disable driver signature enforcement**).
4. After logging in, you can directly load the test-signed/unsigned `EIHvCore.sys` driver.

---

### 2. Test-Signing Mode (Alternative)

If you need the driver to remain loadable across multiple reboots:

1. Open **PowerShell / CMD as Administrator**:
   ```cmd
   bcdedit /set testsigning on
   ```
2. Restart the computer.
3. *(To disable test mode later, run `bcdedit /set testsigning off` and reboot).*

---

### 3. Service Creation and Management

After deploying the compiled `EIHvCore.sys` to the target machine, use the Windows Service Control Manager (`sc.exe`):

```cmd
:: 1. Create the kernel driver service (replace with the absolute path to your EIHvCore.sys)
sc create EIHvCore type= kernel binPath= "C:\Path\To\EIHvCore.sys"

:: 2. Start the driver service
sc start EIHvCore

:: 3. Stop the driver service
sc stop EIHvCore

:: 4. Delete the driver service
sc delete EIHvCore
```

---

## Usage Instructions

1. **Start the Driver Service**: Execute `sc start EIHvCore` as described above.
2. **Launch the Control Center**: Run `EIHvControlCenter.exe` with Administrator privileges.
3. **Menu Operations**:
   - Press `1`: Sends `IOCTL_HV_START` to initialize per-core VCPU states, build EPT structures, launch VMCS across all logical processors, and arm EPT hooks.
   - Press `2`: Sends `IOCTL_HV_STOP` to initiate hook quiescence draining (waiting for active hook threads to exit safely), disabling VMX via VMCALL on all cores and releasing physical memory (preventing `0xCE` / `0x1AA` BSOD).
   - Press `0`: Exits the Control Center.

---

## Feature Implementation Status

### Implemented Features

- [x] **Intel VT-x Hypervisor Core**:
  - CPUID VMX capability and feature MSR discovery.
  - Multi-processor synchronization via `KeIpiGenericCall` for synchronized per-core initialization and unloading.
  - Per-core VCPU state encapsulation (VMXON Region, VMCS Region, MSR Bitmap, and dedicated VMM execution stack).
  - Complete Host and Guest register context preservation and restoration.
- [x] **Extended Page Tables (EPT) Engine**:
  - 1:1 physical memory identity mapping (PML4 $\rightarrow$ PDPT $\rightarrow$ PD 2MB large pages).
  - Dynamic page-splitting subsystem (splitting 2MB PDE into 512 individual 4KB PTEs).
- [x] **Stealth EPT Split-Page Hooking Engine**:
  - Dual memory views (Split-Page): maintains an Execute-only Hook Page alongside a Read/Write Original Page for the same physical address.
  - EPT Violation and Monitor Trap Flag (MTF) single-stepping transitions to defend against PatchGuard integrity scans.
  - **Pre-Hook Support**: Intercepts target kernel functions and substitutes parameters prior to original execution.
  - **Post-Hook Support**: Intercepts function completion through call frame saving and return RIP substitution to inspect or tamper with return values and output buffers.
- [x] **SSDT Dynamic Parser**:
  - Resolves kernel export tables to dynamically locate `KeServiceDescriptorTable`.
  - Supports locating `Nt*` system service routine addresses via System Service Index.
- [x] **Robustness and Anti-Crash Guarding**:
  - **HookGuard Reentrancy Protection**: Thread- and core-level recursion guard eliminating infinite hook recursion loops and kernel stack overflows.
  - **Quiescence Draining Unload Mechanism**: Refuses new hook requests during unload and waits for in-flight hook threads to exit, preventing `0xCE` / `0x1AA` driver unload BSODs.
  - **Sampled Logging**: Sampling rate limiting for ultra-high-frequency functions (e.g. `ExAllocatePool2`) to prevent `DbgPrintEx` pipeline saturation and kernel latency spikes.
- [x] **User-Mode Control Center**:
  - Interactive CLI utility supporting dynamic hypervisor startup and shutdown.

### Planned / Unimplemented Features

- [ ] **Cryptographic Function Hooking and Interception**: Utilizing the EPT Hook engine to intercept kernel cryptography routines (e.g., CNG / BCrypt / KsecDD) to capture plaintext keys and sensitive payload buffers before encryption or after decryption.
- [ ] **User-Mode Dynamic Hook Registration API**: Extending the IOCTL interface to allow user-mode utilities to register arbitrary hook routines dynamically at runtime without driver recompilation.
- [ ] **AMD SVM (AMD-V) Support**: Extending hypervisor support to AMD hardware-assisted virtualization.
- [ ] **Nested Virtualization Support**: Support running inside virtual machines without requiring bare-metal host privileges.
- [ ] **Windows 11 VBS / HVCI Compatibility**: Enhancing coexistence and nested operation when Virtualization-Based Security (VBS) or Hypervisor-Protected Code Integrity (HVCI) is active.

---

## Project Architecture and Directory Structure

```text
EavesdroppingInjection/
├── EavesdroppingInjection.sln   # Visual Studio 2022 Solution File
├── LICENSE                      # GNU General Public License v3.0
├── README.md                    # Traditional Chinese Documentation
├── README_EN.md                 # English Documentation
│
├── EIHvControlCenter/           # User-Mode Console Application
│   ├── Control.cpp              # Win32 CLI source code (DeviceIoControl & interactive menu)
│   ├── EIHvControlCenter.vcxproj # Project file
│   └── EIHvControlCenter.vcxproj.filters
│
└── EIHvCore/                    # Kernel-Mode VT-x Hypervisor Driver Project
    ├── EIHvCore.vcxproj         # Driver project file (WDK / KMDF 1.15)
    ├── EIHvCore.vcxproj.filters
    ├── EIHvCore.inf             # Driver installation INF
    │
    ├── Assembly/                # MASM x64 Assembly Modules
    │   ├── AsmGetSegmentRegisters.asm # Segment registers access (CS/DS/ES/SS/FS/GS/TR/GDTR/IDTR)
    │   └── AsmVmxContext.asm    # VM-exit entrypoint, context save/restore & VMLAUNCH/VMRESUME
    │
    ├── Dependency/              # Third-Party Dependencies
    │   └── HDE/                 # Hacker Disassembler Engine 64-bit (x64 length disassembler)
    │       ├── hde64.c
    │       ├── hde64.h
    │       └── table64.h
    │
    ├── Header/                  # C/C++ Header Files
    │   ├── Driver/              # Driver definitions & IOCTL control codes (Driver.h, Ioctl.h)
    │   ├── Ept/                 # EPT page structures and controls (Ept.h)
    │   ├── EptHook/             # EPT Hook, Pre/Post Hook & HookGuard interfaces
    │   ├── Log/                 # Kernel debug logging macros (Log.h)
    │   ├── Memory/              # Physical & virtual memory management (PhysicalMemory.h, VirtualMemory.h)
    │   ├── Msr/                 # MSR constants and control fields (Msr.h, MsrControlField.h)
    │   ├── Ssdt/                # SSDT structure and function definitions (Ssdt.h)
    │   ├── Vcpu/                # VCPU state definitions (Vcpu.h, VcpuState.h)
    │   ├── VmCall/              # Hypercall interfaces (VmCall.h)
    │   ├── Vmcs/                # VMCS fields and segment registers (Vmcs.h, VmcsField.h)
    │   ├── VmExit/              # VM-Exit reasons, exit handlers & exception injection
    │   ├── Vmm/                 # Hypervisor global state & lifecycle management (Vmm.h, Dpc.h)
    │   ├── Vmx/                 # Intel VMX specifications and macros (Vmx.h, SupportVmx.h)
    │   ├── Register.h           # General CPU registers and RFLAGS definitions
    │   └── util.h               # Utility prototypes (CR access)
    │
    └── Code/                    # C/C++ Implementation Files
        ├── DriverEntry.cpp      # Driver main entry point
        ├── Driver/              # Driver dispatch & device creation (Driver.cpp, DriverDispatch.cpp)
        ├── Ept/                 # EPT setup & 2MB page splitting (Ept.cpp)
        ├── EptHook/             # EPT dual-page hook engine, HookGuard & hook components
        │   ├── EptHook.cpp      # Page hooking, EPT violation & MTF stepping
        │   ├── HookGuard.cpp    # Recursion lock & quiescence draining counter
        │   └── HookComponents.cpp # Pre-Hook / Post-Hook implementation logic
        ├── Log/                 # Kernel logging implementation (Log.cpp)
        ├── Memory/              # Memory allocation & mapping (PhysicalMemory.cpp, VirtualMemory.cpp)
        ├── Msr/                 # MSR read/write operations (Msr.cpp)
        ├── Ssdt/                # SSDT parsing and kernel export resolution (Ssdt.cpp)
        ├── Util.cpp             # System Directory Table Base (CR3) search & signature scanning
        ├── Vcpu/                # Per-core VCPU allocation and stack configuration (Vcpu.cpp)
        ├── VmCall/              # VMCALL execution & exit dispatching (VmCall.cpp)
        ├── Vmcs/                # VMCS configuration for Guest/Host environments (Vmcs.cpp)
        ├── VmExit/              # VM-Exit dispatcher & event injection (VmExit.cpp, InjectEvent.cpp)
        ├── Vmm/                 # Hypervisor start, stop & cross-core dispatch (Vmm.cpp)
        └── Vmx/                 # VMXON initialization & MSR Bitmap setup (Vmx.cpp)
```

---

## License

This project is licensed under the [GNU General Public License v3.0](LICENSE).
