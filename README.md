# EavesdroppingInjection (EIHvCore)

基於 Intel VT-x (VMX) 與 EPT (Extended Page Tables) 技術打造的輕量級 Windows x64 核心 Hypervisor，專為無痕內核掛鉤（Stealth EPT Hooking）與動態行為監控設計。

[English](README_EN.md) | [繁體中文](README.md)

---

## 目錄
- [專案簡介](#專案簡介)
- [驅動載入與環境設置](#驅動載入與環境設置)
  - [單次開機免簽章模式（推薦）](#1-單次開機免簽章模式推薦)
  - [測試模式載入（替代方案）](#2-測試模式載入替代方案)
  - [服務建立與啟動](#3-服務建立與啟動)
- [使用方式](#使用方式)
- [功能實作狀態](#功能實作狀態)
  - [已實現功能](#已實現功能)
  - [規劃中--未實現功能](#規劃中--未實現功能)
- [專案架構與資料夾說明](#專案架構與資料夾說明)
- [授權條款](#授權條款)

---

## 專案簡介

`EavesdroppingInjection` 包含兩大子系統：
1. **EIHvCore (Kernel-Mode Driver)**：利用 Intel VT-x 虛擬化技術，在 Windows 核心下方架構一個 Type-2 Hypervisor，透過 EPT 頁面分割與讀寫/執行分離（Split-page）技術實現抗 PatchGuard（Kernel Patch Protection, KPP）的內核無痕 Pre-Hook 與 Post-Hook。
2. **EIHvControlCenter (User-Mode CLI)**：用於與核心驅動溝通並控制 Hypervisor 生命週期（載入、啟動、卸載排空）之用戶態控制程式。

---

## 驅動載入與環境設置

由於本驅動為自製之核心模式驅動程式（未具備微軟 WHQL / EV 認證簽章），載入至 64 位元 Windows 系統時需暫時放行未簽章驅動。

### 1. 單次開機免簽章模式（推薦）

> [!TIP]
> 此方法**完全不修改系統開機常駐設定**，重開機後即自動恢復正常的驅動強制簽章防護，適合快速除錯與安全性需求高的測試環境。

1. **進入進階啟動選單**：
   - 按住鍵盤上的 `Shift` 鍵，點選 Windows 開始選單中的「**重新啟動**」。
   - 或開啟終端機（以管理員身分）執行：
     ```cmd
     shutdown /r /o /f /t 00
     ```
2. **依序點選選項**：
   - 「**疑難排解 (Troubleshoot)**」 $\rightarrow$
   - 「**進階選項 (Advanced options)**」 $\rightarrow$
   - 「**啟動設定 (Startup Settings)**」 $\rightarrow$
   - 點擊「**重新啟動 (Restart)**」。
3. **選取停用簽章**：
   - 電腦重啟後會出現啟動選項清單，按下鍵盤上的 **`7`** 或 **`F7`**（**停用驅動程式強制簽章 / Disable driver signature enforcement**）。
4. 進入系統後即可直接載入自簽/未簽之 `EIHvCore.sys`。

---

### 2. 測試模式載入（替代方案）

若欲在多次重啟中保持可載入狀態，可啟用 Windows 測試簽署模式：

1. 開啟 **系統管理員身分執行的 PowerShell / CMD**：
   ```cmd
   bcdedit /set testsigning on
   ```
2. 重新啟動電腦。
3. （若日後欲關閉測試模式，執行 `bcdedit /set testsigning off` 後重開機即可）。

---

### 3. 服務建立與啟動

將編譯完成的 `EIHvCore.sys` 部署至目標機台後，使用系統內建的服務控制管理員（SCM）載入驅動：

```cmd
:: 1. 建立核心服務（請將路徑替換為實際 EIHvCore.sys 所在之完整路徑）
sc create EIHvCore type= kernel binPath= "C:\Path\To\EIHvCore.sys"

:: 2. 啟動驅動程式服務
sc start EIHvCore

:: 3. 停止驅動程式服務
sc stop EIHvCore

:: 4. 刪除驅動程式服務（卸載後清理）
sc delete EIHvCore
```

---

## 使用方式

1. **啟動驅動服務**：依上述步驟執行 `sc start EIHvCore`。
2. **執行控制台**：以系統管理員身分啟動 `EIHvControlCenter.exe`。
3. **操作選單**：
   - 輸入 `1`：發送 `IOCTL_HV_START`，初始化各核心 VCPU、建置 EPT 並發射 VMCS，啟用各項 EPT 鉤子。
   - 輸入 `2`：發送 `IOCTL_HV_STOP`，啟動安全排空（Quiescence Draining）機制，排空執行中的 Hook 執行緒，透過 VMCALL 關閉各核心 VMXON 並還原實體記憶體（杜絕 `0xCE` BSOD）。
   - 輸入 `0`：退出控制台。

---

## 功能實作狀態

### 已實現功能

- [x] **Intel VT-x Hypervisor 核心**：
  - CPUID VMX 支援度與 MSR 功能特性偵測。
  - 多處理器同步支援（透過 `KeIpiGenericCall` 跨核心初始化與卸載）。
  - 各核心獨立 VCPU 狀態維護（VMXON Region、VMCS Region、MSR Bitmap、獨立 VMM 核心堆疊）。
  - 主機與訪客（Host / Guest）狀態暫存器上下文完整保存與切換。
- [x] **擴充頁表 (EPT) 引擎**：
  - 1:1 實體記憶體恆等映射（PML4 $\rightarrow$ PDPT $\rightarrow$ PD 2MB 巨型分頁）。
  - 動態分頁分割機制（將 2MB PDE 分割為 512 個 4KB PTE）。
- [x] **隱匿性 EPT 雙頁 Hook 引擎**：
  - 記憶體雙重檢視（Split-Page）：同一實體位址分別維護唯讀原頁（Original Page）與可執行鉤子頁（Hook Page）。
  - EPT 違規（EPT Violation）與監視器陷阱旗標（Monitor Trap Flag, MTF）單步步進切換機制，完美防禦 PatchGuard 記憶體完整性檢查。
  - **Pre-Hook 支援**：在目標核心函數執行前攔截並置換參數。
  - **Post-Hook 支援**：基於呼叫堆疊 Frame 保存與安全返回攔截，支援在系統調用完成後讀取/竄改輸出結果。
- [x] **SSDT 動態解析器**：
  - 解析 Windows 核心導出結構以定位 SSDT（`KeServiceDescriptorTable`）。
  - 支援以 System Service Index 動態定位各項 `Nt*` 系統呼叫核心位址。
- [x] **高可靠性與防崩潰機制**：
  - **HookGuard 遞迴防護**：基於執行緒與核心層級的防重入保護機制，徹底防止 Hook 處理函數自身引發的死迴圈或堆疊溢位。
  - **安全卸載排空機制（Quiescence Draining）**：卸載前先阻斷新請求並等待所有仍在執行 Hook 的緒程全部撤出，徹底消除驅動卸載引發的 `0xCE` / `0x1AA` BSOD。
  - **取樣日誌（Sample Logging）**：對高頻率分配函數（如 `ExAllocatePool2`）進行倍數取樣，避免 `DbgPrintEx` 造成系統凍結與延遲。
- [x] **用戶態控制中心**：
  - 提供 CLI 互動介面，支援 Hypervisor 動態開啟與關閉。

### 規劃中 / 未實現功能

- [ ] **對加密函數掛鉤攔截**：透過 EPT Hook 機制攔截核心加密函數（如 CNG / BCrypt / KsecDD 等），在資料加密前或解密後動態擷取明文密鑰與通訊數據。
- [ ] **用戶態動態 Hook 註冊介面**：目前 Hook 目標主要於 `HookComponents.cpp` 靜態定義，計畫新增 IOCTL 介面讓用戶態可動態傳入位址/函式名稱進行即時掛鉤。
- [ ] **AMD SVM (AMD-V) 支援**：目前僅支援 Intel VT-x 架構，尚未支援 AMD 處理器硬體虛擬化。
- [ ] **巢狀虛擬化 (Nested Virtualization) 支援**：目前需在實體機或開啟巢狀虛擬化之虛擬機器中運行。
- [ ] **Windows 11 VBS / HVCI 相容適配**：在開啟核心隔離（HVCI/VBS）的環境下，Windows 系統已常駐 Hyper-V，自製 Hypervisor 需進一步適配巢狀協作。

---

## 專案架構與資料夾說明

```text
EavesdroppingInjection/
├── EavesdroppingInjection.sln   # Visual Studio 2022 方案檔
├── LICENSE                      # GNU General Public License v3.0 授權條款
├── README.md                    # 專案中文說明文件
├── README_EN.md                 # 專案英文說明文件 (English Documentation)
│
├── EIHvControlCenter/           # 用戶態控制台專案 (User-Mode Application)
│   ├── Control.cpp              # Win32 控制台原始碼 (IOCTL 呼叫與互動選單)
│   ├── EIHvControlCenter.vcxproj # 專案檔
│   └── EIHvControlCenter.vcxproj.filters
│
└── EIHvCore/                    # 核心態驅動與 VT-x 引擎專案 (Kernel-Mode Driver)
    ├── EIHvCore.vcxproj         # 驅動專案檔 (設定 WDK / KMDF 1.15)
    ├── EIHvCore.vcxproj.filters
    ├── EIHvCore.inf             # 驅動安裝資訊檔
    │
    ├── Assembly/                # MASM x64 組合語言模組
    │   ├── AsmGetSegmentRegisters.asm # 讀取段選擇器 (CS/DS/ES/SS/FS/GS/TR/GDTR/IDTR)
    │   └── AsmVmxContext.asm    # VM-exit 進入點、上下文保存與 VMLAUNCH/VMRESUME
    │
    ├── Dependency/              # 第三方依賴庫
    │   └── HDE/                 # Hacker Disassembler Engine 64-bit (指令長度解碼器)
    │       ├── hde64.c
    │       ├── hde64.h
    │       └── table64.h
    │
    ├── Header/                  # C/C++ 標頭檔目錄
    │   ├── Driver/              # 驅動定義與 IOCTL 控制碼宣告 (Driver.h, Ioctl.h)
    │   ├── Ept/                 # EPT 頁表結構與控制宣告 (Ept.h)
    │   ├── EptHook/             # EPT Hook、Pre/Post Hook 與 HookGuard 介面宣告
    │   ├── Log/                 # 核心日誌巨集封裝 (Log.h)
    │   ├── Memory/              # 實體與虛擬記憶體管理介面 (PhysicalMemory.h, VirtualMemory.h)
    │   ├── Msr/                 # MSR 暫存器常數與欄位控制 (Msr.h, MsrControlField.h)
    │   ├── Ssdt/                # SSDT 結構與符號宣告 (Ssdt.h)
    │   ├── Vcpu/                # VCPU 狀態結構 (Vcpu.h, VcpuState.h)
    │   ├── VmCall/              # Hypercall 介面宣告 (VmCall.h)
    │   ├── Vmcs/                # VMCS 欄位列舉與段暫存器宣告 (Vmcs.h, VmcsField.h)
    │   ├── VmExit/              # VM-Exit 原因、退出處理常式與例外注入宣告
    │   ├── Vmm/                 # Hypervisor 全域狀態與生命週期管理宣告 (Vmm.h, Dpc.h)
    │   ├── Vmx/                 # Intel VMX 規格支援與巨集宣告 (Vmx.h, SupportVmx.h)
    │   ├── Register.h           # 通用 CPU 暫存器結構與 RFLAGS 欄位
    │   └── util.h               # 工具函式宣告 (CR 存取、CPUID 檢測)
    │
    └── Code/                    # C/C++ 實作程式碼目錄
        ├── DriverEntry.cpp      # 驅動程式統一入口點
        ├── Driver/              # 驅動分發、裝置物件創建 (Driver.cpp, DriverDispatch.cpp)
        ├── Ept/                 # EPT 頁表構建、2MB 頁面分割 (Ept.cpp)
        ├── EptHook/             # EPT 雙頁 Hook 引擎、HookGuard 防護與組件實作
        │   ├── EptHook.cpp      # EPT 分頁掛鉤、違規處理、MTF 步進
        │   ├── HookGuard.cpp    # 遞迴防重入鎖與安全卸載排空計數器
        │   └── HookComponents.cpp # Pre-Hook / Post-Hook 業務邏輯實作
        ├── Log/                 # 核心日誌印出模組 (Log.cpp)
        ├── Memory/              # 實體/虛擬記憶體配置與對映 (PhysicalMemory.cpp, VirtualMemory.cpp)
        ├── Msr/                 # MSR 讀寫實作 (Msr.cpp)
        ├── Ssdt/                # SSDT 解析與導出表定位 (Ssdt.cpp)
        ├── Util.cpp             # CR3 搜尋、記憶體特徵碼搜尋等輔助函式
        ├── Vcpu/                # 各核心 VCPU 初始化與堆疊配置 (Vcpu.cpp)
        ├── VmCall/              # VMCALL 指令觸發與退出分發 (VmCall.cpp)
        ├── Vmcs/                # VMCS 各欄位設定與 Guest/Host 環境配置 (Vmcs.cpp)
        ├── VmExit/              # VM-Exit 分發邏輯與例外注入 (VmExit.cpp, InjectEvent.cpp)
        ├── Vmm/                 # Hypervisor 啟動、停止、跨核心調度 (Vmm.cpp)
        └── Vmx/                 # VMXON 初始化、MSR Bitmap 設定 (Vmx.cpp)
```

---

## 授權條款

本專案採用 [GNU General Public License v3.0](LICENSE) 條款開源發布。
