#include <iostream>
#include <iomanip>
#include <vector>
#include <windows.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <wrl/client.h>

// Nsight Perf SDK headers
#include <NvPerfInit.h>
#include <NvPerfDeviceProperties.h>
#include <NvPerfPeriodicSamplerGpu.h>
#include <NvPerfD3D.h>
#include <NvPerfD3D12.h>

using Microsoft::WRL::ComPtr;

// Simple dynamic loader for NVML to get Driver version and VRAM capacity
typedef int (*nvmlInit_t)();
typedef int (*nvmlShutdown_t)();
typedef int (*nvmlSystemGetDriverVersion_t)(char*, unsigned int);
typedef int (*nvmlDeviceGetHandleByIndex_t)(unsigned int, void**);
typedef struct {
    unsigned long long total;
    unsigned long long free;
    unsigned long long used;
} nvmlMemory_t;
typedef int (*nvmlDeviceGetMemoryInfo_t)(void*, nvmlMemory_t*);

int main()
{
    std::cout << "========================================" << std::endl;
    std::cout << " NVIDIA GPU VRAM I/O Monitor - Step 1   " << std::endl;
    std::cout << " Environment & Device Initialization    " << std::endl;
    std::cout << "========================================" << std::endl;

    // 1. Initialize Nsight Perf SDK
    if (!nv::perf::InitializeNvPerf())
    {
        std::cerr << "[ERROR] Failed to initialize Nsight Perf SDK!" << std::endl;
        return 1;
    }
    std::cout << "Nsight Perf SDK Initialization: OK" << std::endl;

    // 2. Load D3D12 Driver for Nsight Perf
    if (!nv::perf::D3D12LoadDriver())
    {
        std::cerr << "[WARNING] D3D12LoadDriver returned false" << std::endl;
    }
    else
    {
        std::cout << "D3D12 Driver Loaded: OK" << std::endl;
    }

    // 3. Find NVIDIA DXGI Adapter & create D3D12 Device
    ComPtr<IDXGIFactory4> pFactory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&pFactory));
    if (FAILED(hr))
    {
        std::cerr << "[ERROR] CreateDXGIFactory1 failed: " << std::hex << hr << std::endl;
        return 1;
    }

    ComPtr<IDXGIAdapter1> pNvidiaAdapter;
    for (UINT i = 0; ; ++i)
    {
        ComPtr<IDXGIAdapter1> pAdapter;
        if (pFactory->EnumAdapters1(i, &pAdapter) == DXGI_ERROR_NOT_FOUND)
        {
            break;
        }

        DXGI_ADAPTER_DESC1 desc{};
        pAdapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
        {
            continue;
        }

        if (desc.VendorId == 0x10DE) // NVIDIA
        {
            pNvidiaAdapter = pAdapter;
            std::wcout << L"Found NVIDIA Adapter: " << desc.Description << std::endl;
            break;
        }
    }

    if (!pNvidiaAdapter)
    {
        std::cerr << "[ERROR] NVIDIA GPU not found via DXGI" << std::endl;
        return 1;
    }

    // Create D3D12 Device
    ComPtr<ID3D12Device> pD3D12Device;
    hr = D3D12CreateDevice(pNvidiaAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&pD3D12Device));
    if (FAILED(hr))
    {
        std::cerr << "[ERROR] D3D12CreateDevice failed: " << std::hex << hr << std::endl;
        return 1;
    }
    std::cout << "D3D12 Device created: OK" << std::endl;

    // 4. Query NvPerf Device Index
    size_t nvperfDeviceIndex = nv::perf::D3D12GetNvperfDeviceIndex(pD3D12Device.Get());
    if (nvperfDeviceIndex == ~size_t(0))
    {
        std::cout << "Trying D3DGetNvperfDeviceIndex with DXGI adapter..." << std::endl;
        nvperfDeviceIndex = nv::perf::D3DGetNvperfDeviceIndex(pNvidiaAdapter.Get());
    }

    std::cout << "NvPerf Device Index: " << nvperfDeviceIndex << std::endl;

    if (nvperfDeviceIndex != ~size_t(0))
    {
        nv::perf::DeviceIdentifiers identifiers = nv::perf::GetDeviceIdentifiers(nvperfDeviceIndex);
        std::cout << "GPU Name: " << (identifiers.pDeviceName ? identifiers.pDeviceName : "Unknown") << std::endl;
        std::cout << "GPU Chip/Architecture: " << (identifiers.pChipName ? identifiers.pChipName : "Unknown") << std::endl;

        bool isPeriodicSupported = nv::perf::sampler::GpuPeriodicSamplerIsGpuSupported(nvperfDeviceIndex);
        std::cout << "GpuPeriodicSampler Supported: " << (isPeriodicSupported ? "YES" : "NO") << std::endl;
    }

    // 5. Query Driver Version and VRAM via NVML
    HMODULE hNvml = LoadLibraryA("nvml.dll");
    if (hNvml)
    {
        auto pNvmlInit = (nvmlInit_t)GetProcAddress(hNvml, "nvmlInit_v2");
        if (!pNvmlInit) pNvmlInit = (nvmlInit_t)GetProcAddress(hNvml, "nvmlInit");

        auto pNvmlShutdown = (nvmlShutdown_t)GetProcAddress(hNvml, "nvmlShutdown");
        auto pNvmlGetDriverVersion = (nvmlSystemGetDriverVersion_t)GetProcAddress(hNvml, "nvmlSystemGetDriverVersion");
        auto pNvmlGetHandle = (nvmlDeviceGetHandleByIndex_t)GetProcAddress(hNvml, "nvmlDeviceGetHandleByIndex_v2");
        if (!pNvmlGetHandle) pNvmlGetHandle = (nvmlDeviceGetHandleByIndex_t)GetProcAddress(hNvml, "nvmlDeviceGetHandleByIndex");
        auto pNvmlGetMem = (nvmlDeviceGetMemoryInfo_t)GetProcAddress(hNvml, "nvmlDeviceGetMemoryInfo");

        if (pNvmlInit && pNvmlInit() == 0)
        {
            char driverVersion[64] = {0};
            if (pNvmlGetDriverVersion && pNvmlGetDriverVersion(driverVersion, sizeof(driverVersion)) == 0)
            {
                std::cout << "Driver: " << driverVersion << std::endl;
            }

            void* devHandle = nullptr;
            if (pNvmlGetHandle && pNvmlGetHandle(0, &devHandle) == 0 && pNvmlGetMem)
            {
                nvmlMemory_t mem{};
                if (pNvmlGetMem(devHandle, &mem) == 0)
                {
                    double totalGB = (double)mem.total / (1024.0 * 1024.0 * 1024.0);
                    double usedGB = (double)mem.used / (1024.0 * 1024.0 * 1024.0);
                    std::cout << "VRAM Capacity: " << std::fixed << std::setprecision(1) << totalGB << " GB (Current Used: " << usedGB << " GB)" << std::endl;
                }
            }
            if (pNvmlShutdown) pNvmlShutdown();
        }
        FreeLibrary(hNvml);
    }

    std::cout << "Device initialization: OK" << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}
