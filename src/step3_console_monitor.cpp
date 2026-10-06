#include <iostream>
#include <iomanip>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <atomic>
#include <csignal>
#include <conio.h>
#include <windows.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <wrl/client.h>

// Nsight Perf SDK headers
#include <NvPerfInit.h>
#include <NvPerfDeviceProperties.h>
#include <NvPerfPeriodicSamplerGpu.h>
#include <NvPerfPeriodicSamplerCommon.h>
#include <NvPerfMetricsEvaluator.h>
#include <NvPerfMetricsConfigBuilder.h>
#include <NvPerfCounterConfiguration.h>
#include <NvPerfCounterData.h>
#include <NvPerfD3D.h>
#include <NvPerfD3D12.h>

using Microsoft::WRL::ComPtr;

static std::atomic<bool> g_running{ true };

static BOOL WINAPI ConsoleCtrlHandler(DWORD fdwCtrlType)
{
    switch (fdwCtrlType)
    {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        g_running = false;
        return TRUE;
    default:
        return FALSE;
    }
}

// Check if running as administrator
static bool IsRunAsAdmin()
{
    BOOL isAdmin = FALSE;
    PSID adminGroup = nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup))
    {
        CheckTokenMembership(nullptr, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin == TRUE;
}

// NVML function prototypes
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
typedef struct {
    unsigned int gpu;
    unsigned int memory;
} nvmlUtilization_t;
typedef int (*nvmlDeviceGetUtilizationRates_t)(void*, nvmlUtilization_t*);

struct NvmlContext {
    HMODULE hNvml = nullptr;
    nvmlInit_t pInit = nullptr;
    nvmlShutdown_t pShutdown = nullptr;
    nvmlSystemGetDriverVersion_t pGetDriver = nullptr;
    nvmlDeviceGetHandleByIndex_t pGetHandle = nullptr;
    nvmlDeviceGetMemoryInfo_t pGetMem = nullptr;
    nvmlDeviceGetUtilizationRates_t pGetUtil = nullptr;
    void* devHandle = nullptr;
    bool available = false;

    bool Initialize()
    {
        hNvml = LoadLibraryA("nvml.dll");
        if (!hNvml) return false;

        pInit = (nvmlInit_t)GetProcAddress(hNvml, "nvmlInit_v2");
        if (!pInit) pInit = (nvmlInit_t)GetProcAddress(hNvml, "nvmlInit");
        pShutdown = (nvmlShutdown_t)GetProcAddress(hNvml, "nvmlShutdown");
        pGetDriver = (nvmlSystemGetDriverVersion_t)GetProcAddress(hNvml, "nvmlSystemGetDriverVersion");
        pGetHandle = (nvmlDeviceGetHandleByIndex_t)GetProcAddress(hNvml, "nvmlDeviceGetHandleByIndex_v2");
        if (!pGetHandle) pGetHandle = (nvmlDeviceGetHandleByIndex_t)GetProcAddress(hNvml, "nvmlDeviceGetHandleByIndex");
        pGetMem = (nvmlDeviceGetMemoryInfo_t)GetProcAddress(hNvml, "nvmlDeviceGetMemoryInfo");
        pGetUtil = (nvmlDeviceGetUtilizationRates_t)GetProcAddress(hNvml, "nvmlDeviceGetUtilizationRates");

        if (pInit && pInit() == 0 && pGetHandle && pGetHandle(0, &devHandle) == 0)
        {
            available = true;
            return true;
        }
        return false;
    }

    void Shutdown()
    {
        if (available && pShutdown)
        {
            pShutdown();
            available = false;
        }
        if (hNvml)
        {
            FreeLibrary(hNvml);
            hNvml = nullptr;
        }
    }

    bool GetMemoryUsage(double& usedGB, double& totalGB)
    {
        if (!available || !pGetMem || !devHandle) return false;
        nvmlMemory_t mem{};
        if (pGetMem(devHandle, &mem) == 0)
        {
            usedGB = (double)mem.used / (1024.0 * 1024.0 * 1024.0);
            totalGB = (double)mem.total / (1024.0 * 1024.0 * 1024.0);
            return true;
        }
        return false;
    }

    bool GetMemoryControllerUtil(unsigned int& memUtilPercent)
    {
        if (!available || !pGetUtil || !devHandle) return false;
        nvmlUtilization_t util{};
        if (pGetUtil(devHandle, &util) == 0)
        {
            memUtilPercent = util.memory;
            return true;
        }
        return false;
    }

    std::string GetDriverVersion()
    {
        if (!available || !pGetDriver) return "Unknown";
        char buf[64] = {0};
        if (pGetDriver(buf, sizeof(buf)) == 0)
        {
            return std::string(buf);
        }
        return "Unknown";
    }
};

static const int BOX_WIDTH = 64;

static std::string MakeBorder()
{
    return "+" + std::string(BOX_WIDTH - 2, '-') + "+\n";
}

static std::string MakeRow(const std::string& text)
{
    std::string row = "| " + text;
    if (row.size() < (size_t)(BOX_WIDTH - 1))
    {
        row.append((size_t)(BOX_WIDTH - 1) - row.size(), ' ');
    }
    row += "|\n";
    return row;
}

static std::string MakeBar(double gbps, double maxGbps = 1000.0, int barWidth = 18)
{
    if (gbps <= 0.0) return std::string(barWidth, ' ');
    int filled = (int)((gbps / maxGbps) * barWidth);
    if (filled > barWidth) filled = barWidth;
    if (filled == 0 && gbps > 0.01)
    {
        std::string s = "|";
        s.append(barWidth - 1, ' ');
        return s;
    }
    std::string s(filled, '=');
    s.append(barWidth - filled, ' ');
    return s;
}

struct AppSettings {
    int samplingIntervalMs = 100;
    bool enableSampleAccumulation = true;
};

static std::string GetSettingsFilePath()
{
    // 1. Current working directory
    if (GetFileAttributesA("settings.json") != INVALID_FILE_ATTRIBUTES)
    {
        return "settings.json";
    }

    // 2. Next to executable or project root
    char exePath[MAX_PATH] = { 0 };
    if (GetModuleFileNameA(NULL, exePath, MAX_PATH))
    {
        char* lastSlash = strrchr(exePath, '\\');
        if (lastSlash)
        {
            *(lastSlash + 1) = '\0';
            std::string pathNextToExe = std::string(exePath) + "settings.json";
            if (GetFileAttributesA(pathNextToExe.c_str()) != INVALID_FILE_ATTRIBUTES)
            {
                return pathNextToExe;
            }
            std::string parentSettings = std::string(exePath) + "..\\..\\settings.json";
            if (GetFileAttributesA(parentSettings.c_str()) != INVALID_FILE_ATTRIBUTES)
            {
                return parentSettings;
            }
        }
    }

    return "settings.json";
}

static AppSettings LoadSettings(const std::string& filepath)
{
    AppSettings settings;
    std::ifstream file(filepath);
    if (!file.is_open())
    {
        // Auto-create default settings.json
        std::ofstream out(filepath);
        if (out.is_open())
        {
            out << "{\n"
                << "  \"sampling_interval_ms\": 100,\n"
                << "  \"enable_sample_accumulation\": true\n"
                << "}\n";
            out.close();
        }
        return settings;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    // Parse sampling_interval_ms
    size_t posInterval = content.find("sampling_interval_ms");
    if (posInterval != std::string::npos)
    {
        size_t colon = content.find(':', posInterval);
        if (colon != std::string::npos)
        {
            int val = std::atoi(content.c_str() + colon + 1);
            if (val >= 10 && val <= 10000)
            {
                settings.samplingIntervalMs = val;
            }
        }
    }

    // Parse enable_sample_accumulation (case-insensitive, handles false/False/FALSE/0/off)
    size_t posAccum = content.find("enable_sample_accumulation");
    if (posAccum != std::string::npos)
    {
        size_t colon = content.find(':', posAccum);
        if (colon != std::string::npos)
        {
            std::string sub = content.substr(colon + 1, 32);
            for (char& c : sub) c = (char)std::tolower((unsigned char)c);
            if (sub.find("false") != std::string::npos || sub.find("0") != std::string::npos || sub.find("off") != std::string::npos)
            {
                settings.enableSampleAccumulation = false;
            }
            else if (sub.find("true") != std::string::npos || sub.find("1") != std::string::npos || sub.find("on") != std::string::npos)
            {
                settings.enableSampleAccumulation = true;
            }
        }
    }

    return settings;
}

int main(int argc, char* argv[])
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    // Enable ANSI escape codes for in-place console refresh
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);

    DWORD dwMode = 0;
    if (GetConsoleMode(hOut, &dwMode))
    {
        SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }

    // Load configuration from settings.json
    std::string settingsPath = GetSettingsFilePath();
    AppSettings settings = LoadSettings(settingsPath);

    int intervalMs = settings.samplingIntervalMs;
    bool enableAccumulation = settings.enableSampleAccumulation;
    int durationSec = 0; // 0 = continuous monitoring

    // CLI arguments override if explicitly provided
    if (argc >= 2)
    {
        int cliVal = std::atoi(argv[1]);
        if (cliVal >= 10) intervalMs = cliVal;
    }
    if (argc >= 3)
    {
        durationSec = std::atoi(argv[2]);
        if (durationSec < 0) durationSec = 0;
    }

    std::cout << "================================================================" << std::endl;
    std::cout << " NVIDIA GPU VRAM I/O Monitor - Realtime Console                 " << std::endl;
    std::cout << "================================================================" << std::endl;

    if (!IsRunAsAdmin())
    {
        std::cout << "[WARNING] Running without administrator privileges.\n"
                  << "If GPU hardware performance counter access fails,\n"
                  << "right-click 'run_console_monitor.bat' -> [Run as Administrator].\n"
                  << "----------------------------------------------------------------" << std::endl;
    }

    // 1. Initialize NVML
    NvmlContext nvml;
    nvml.Initialize();

    // 2. Initialize Nsight Perf SDK
    if (!nv::perf::InitializeNvPerf())
    {
        std::cerr << "[ERROR] Failed to initialize Nsight Perf SDK!" << std::endl;
        return 1;
    }
    nv::perf::D3D12LoadDriver();

    ComPtr<IDXGIFactory4> pFactory;
    CreateDXGIFactory1(IID_PPV_ARGS(&pFactory));
    ComPtr<IDXGIAdapter1> pNvidiaAdapter;
    for (UINT i = 0; ; ++i)
    {
        ComPtr<IDXGIAdapter1> pAdapter;
        if (pFactory->EnumAdapters1(i, &pAdapter) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 desc{};
        pAdapter->GetDesc1(&desc);
        if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 && desc.VendorId == 0x10DE)
        {
            pNvidiaAdapter = pAdapter;
            break;
        }
    }

    if (!pNvidiaAdapter)
    {
        std::cerr << "[ERROR] NVIDIA GPU not found" << std::endl;
        return 1;
    }

    ComPtr<ID3D12Device> pD3D12Device;
    D3D12CreateDevice(pNvidiaAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&pD3D12Device));
    size_t deviceIndex = nv::perf::D3D12GetNvperfDeviceIndex(pD3D12Device.Get());
    if (deviceIndex == ~size_t(0))
    {
        deviceIndex = nv::perf::D3DGetNvperfDeviceIndex(pNvidiaAdapter.Get());
    }

    nv::perf::DeviceIdentifiers identifiers = nv::perf::GetDeviceIdentifiers(deviceIndex);
    std::string driverVersion = nvml.GetDriverVersion();

    std::cout << "GPU:    " << identifiers.pDeviceName << " (" << identifiers.pChipName << ")" << std::endl;
    std::cout << "Driver: " << driverVersion << std::endl;
    std::cout << "Rate:   " << intervalMs << " ms (" << (1000 / intervalMs) << " samples/sec)" << std::endl;
    std::cout << "Exit:   Press [Q] or [ESC] to safely stop monitoring." << std::endl;
    std::cout << "----------------------------------------------------------------\n" << std::endl;

    // 3. Create MetricsEvaluator
    std::vector<uint8_t> scratchBuffer;
    NVPW_MetricsEvaluator* pMetricsEvaluator = nv::perf::sampler::DeviceCreateMetricsEvaluator(scratchBuffer, identifiers.pChipName);
    if (!pMetricsEvaluator)
    {
        std::cerr << "[ERROR] Failed to create MetricsEvaluator" << std::endl;
        return 1;
    }
    nv::perf::MetricsEvaluator evaluator(pMetricsEvaluator, std::move(scratchBuffer));

    // 4. Configure DRAM Read and Write Metrics
    const std::vector<std::string> metricNames = {
        "dram__bytes_op_read.sum",
        "dram__bytes_op_write.sum"
    };

    std::vector<NVPW_MetricEvalRequest> metricRequests;
    for (const auto& name : metricNames)
    {
        NVPW_MetricEvalRequest req{};
        if (!evaluator.ToMetricEvalRequest(name.c_str(), req))
        {
            std::cerr << "[ERROR] Failed to create eval request for: " << name << std::endl;
            return 1;
        }
        metricRequests.push_back(req);
    }

    nv::perf::CounterConfiguration configuration;
    {
        NVPW_RawCounterConfig* pRawCounterConfig = nv::perf::sampler::DeviceCreateRawCounterConfig(identifiers.pChipName);
        if (!pRawCounterConfig)
        {
            std::cerr << "[ERROR] Failed to create RawCounterConfig" << std::endl;
            return 1;
        }
        nv::perf::MetricsConfigBuilder configBuilder;
        if (!configBuilder.Initialize(evaluator.Get(), pRawCounterConfig, identifiers.pChipName) ||
            !configBuilder.AddMetrics(metricRequests.data(), metricRequests.size()) ||
            !nv::perf::CreateConfiguration(configBuilder, configuration))
        {
            std::cerr << "[ERROR] Failed to create counter configuration" << std::endl;
            return 1;
        }
    }

    // 5. Initialize GpuPeriodicSampler
    const size_t MaxNumUndecodedSamplingRanges = 1;
    const size_t MaxNumUndecodedSamples = 2048;
    size_t recordBufferSize = 0;
    if (!nv::perf::sampler::GpuPeriodicSamplerCalculateRecordBufferSize(deviceIndex, configuration.configImage, MaxNumUndecodedSamples, recordBufferSize))
    {
        std::cerr << "[ERROR] GpuPeriodicSamplerCalculateRecordBufferSize failed" << std::endl;
        return 1;
    }

    std::vector<uint8_t> counterDataImage;
    if (!nv::perf::sampler::GpuPeriodicSamplerCreateCounterData(
            deviceIndex,
            configuration.counterDataPrefix.data(),
            configuration.counterDataPrefix.size(),
            MaxNumUndecodedSamples,
            NVPW_PERIODIC_SAMPLER_COUNTER_DATA_APPEND_MODE_LINEAR,
            counterDataImage))
    {
        std::cerr << "[ERROR] GpuPeriodicSamplerCreateCounterData failed" << std::endl;
        return 1;
    }

    nv::perf::sampler::GpuPeriodicSampler sampler;
    if (!sampler.Initialize(deviceIndex))
    {
        std::cerr << "[ERROR] sampler.Initialize failed" << std::endl;
        return 1;
    }

    if (!sampler.BeginSession(
            recordBufferSize,
            MaxNumUndecodedSamplingRanges,
            { NVPW_GPU_PERIODIC_SAMPLER_TRIGGER_SOURCE_CPU_SYSCALL },
            0))
    {
        std::cerr << "\n================================================================" << std::endl;
        std::cerr << "[PERMISSION ERROR] NVIDIA GPU Performance Counter Access Required" << std::endl;
        std::cerr << "(NVIDIA Security Policy ERR_NVGPUCTRPERM: Administrator Privileges Required)" << std::endl;
        std::cerr << "================================================================" << std::endl;
        std::cerr << "How to run:" << std::endl;
        std::cerr << "  Right-click 'run_console_monitor.bat' and select" << std::endl;
        std::cerr << "  [Run as Administrator] to start the monitor." << std::endl;
        std::cerr << "================================================================\n" << std::endl;
        return 1;
    }

    if (!sampler.SetConfig(configuration.configImage, 0))
    {
        std::cerr << "[ERROR] sampler.SetConfig failed" << std::endl;
        sampler.EndSession();
        return 1;
    }

    if (!sampler.StartSampling())
    {
        std::cerr << "[ERROR] sampler.StartSampling failed" << std::endl;
        sampler.EndSession();
        return 1;
    }

    std::cout << "[START] Initializing real-time VRAM I/O monitoring...\n" << std::endl;

    // Clear console screen once for dedicated dashboard view
    system("cls");

    // Hide console cursor to prevent flickering
    CONSOLE_CURSOR_INFO cci;
    GetConsoleCursorInfo(hOut, &cci);
    BOOL originalCursorVis = cci.bVisible;
    cci.bVisible = FALSE;
    SetConsoleCursorInfo(hOut, &cci);

    double prevReadBytes = 0.0;
    double prevWriteBytes = 0.0;
    auto prevTime = std::chrono::steady_clock::now();
    bool hasFirstSample = false;
    uint64_t sampleCount = 0;
    auto startTime = std::chrono::steady_clock::now();

    while (g_running)
    {
        // Check for keyboard press (Q / ESC) to quit cleanly without triggering CMD batch job prompt
        if (_kbhit())
        {
            int ch = _getch();
            if (ch == 'q' || ch == 'Q' || ch == 27) // 27 = ESC
            {
                g_running = false;
                break;
            }
        }

        if (durationSec > 0)
        {
            auto now = std::chrono::steady_clock::now();
            double totalElapsed = std::chrono::duration<double>(now - startTime).count();
            if (totalElapsed >= durationSec)
            {
                break;
            }
        }

        // 1. Trigger sample
        if (!sampler.CpuTrigger())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
            continue;
        }

        // Wait sampling interval
        std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
        auto currTime = std::chrono::steady_clock::now();
        double elapsedSec = std::chrono::duration<double>(currTime - prevTime).count();
        prevTime = currTime;

        // 2. Query record buffer status
        nv::perf::sampler::GpuPeriodicSampler::GetRecordBufferStatusParams bufStatus{};
        bufStatus.queryNumUnreadBytes = true;
        bufStatus.queryOverflow = true;
        if (!sampler.GetRecordBufferStatus(bufStatus) || bufStatus.numUnreadBytes == 0)
        {
            continue;
        }

        // 3. Reset and decode into counterDataImage
        nv::perf::sampler::GpuPeriodicSamplerCreateCounterData(
            deviceIndex,
            configuration.counterDataPrefix.data(),
            configuration.counterDataPrefix.size(),
            MaxNumUndecodedSamples,
            NVPW_PERIODIC_SAMPLER_COUNTER_DATA_APPEND_MODE_LINEAR,
            counterDataImage);

        NVPW_GPU_PeriodicSampler_DecodeStopReason stopReason = NVPW_GPU_PERIODIC_SAMPLER_DECODE_STOP_REASON_OTHER;
        size_t numSamplesMerged = 0;
        size_t numBytesConsumed = 0;
        if (!sampler.DecodeCounters(counterDataImage, bufStatus.numUnreadBytes, stopReason, numSamplesMerged, numBytesConsumed))
        {
            continue;
        }

        sampler.AcknowledgeRecordBuffer(numBytesConsumed);

        size_t numRanges = nv::perf::CounterDataGetNumRanges(counterDataImage.data());
        if (numRanges == 0) continue;

        nv::perf::MetricsEvaluatorSetDeviceAttributes(evaluator.Get(), counterDataImage.data(), counterDataImage.size());

        size_t rangeIndex = numRanges - 1;
        std::vector<double> metricValues(metricRequests.size(), 0.0);
        if (!nv::perf::EvaluateToGpuValues(
                evaluator.Get(),
                counterDataImage.data(),
                counterDataImage.size(),
                rangeIndex,
                metricRequests.size(),
                metricRequests.data(),
                metricValues.data()))
        {
            continue;
        }

        double currReadBytes = metricValues[0];
        double currWriteBytes = metricValues[1];

        // Query VRAM & Memory Controller Load via NVML
        double usedVramGB = 0.0, totalVramGB = 0.0;
        unsigned int memCtrlPct = 0;
        nvml.GetMemoryUsage(usedVramGB, totalVramGB);
        nvml.GetMemoryControllerUtil(memCtrlPct);

        if (!hasFirstSample)
        {
            prevReadBytes = currReadBytes;
            prevWriteBytes = currWriteBytes;
            hasFirstSample = true;
            continue;
        }

        double deltaRead = (currReadBytes >= prevReadBytes) ? (currReadBytes - prevReadBytes) : currReadBytes;
        double deltaWrite = (currWriteBytes >= prevWriteBytes) ? (currWriteBytes - prevWriteBytes) : currWriteBytes;
        prevReadBytes = currReadBytes;
        prevWriteBytes = currWriteBytes;

        // Bandwidth calculation: (Bytes / dt) / 10^9 = GB/s
        double readGBps = (elapsedSec > 0.0) ? (deltaRead / elapsedSec) / 1.0e9 : 0.0;
        double writeGBps = (elapsedSec > 0.0) ? (deltaWrite / elapsedSec) / 1.0e9 : 0.0;
        double totalGBps = readGBps + writeGBps;

        if (enableAccumulation)
        {
            sampleCount++;
        }

        // Reset cursor to (0, 0) for stable in-place refresh
        SetConsoleCursorPosition(hOut, COORD{ 0, 0 });

        std::string frame;
        frame.reserve(2048);
        frame += MakeBorder();
        frame += MakeRow("            NVIDIA GPU VRAM Real-time I/O Monitor");
        frame += MakeBorder();

        char buf[128];
        snprintf(buf, sizeof(buf), "GPU Name            : %s", identifiers.pDeviceName);
        frame += MakeRow(buf);

        snprintf(buf, sizeof(buf), "Architecture        : Blackwell (%s)", identifiers.pChipName);
        frame += MakeRow(buf);

        snprintf(buf, sizeof(buf), "Driver Version      : %s", driverVersion.c_str());
        frame += MakeRow(buf);

        frame += MakeBorder();

        double vramPct = (totalVramGB > 0.0) ? (usedVramGB / totalVramGB * 100.0) : 0.0;
        snprintf(buf, sizeof(buf), "VRAM Usage          : %4.1f / %4.1f GB  (%4.1f %%)", usedVramGB, totalVramGB, vramPct);
        frame += MakeRow(buf);

        snprintf(buf, sizeof(buf), "Memory Controller   : %3u %%", memCtrlPct);
        frame += MakeRow(buf);

        frame += MakeBorder();

        snprintf(buf, sizeof(buf), "VRAM Read           : %6.1f GB/s  [%s]", readGBps, MakeBar(readGBps).c_str());
        frame += MakeRow(buf);

        snprintf(buf, sizeof(buf), "VRAM Write          : %6.1f GB/s  [%s]", writeGBps, MakeBar(writeGBps).c_str());
        frame += MakeRow(buf);

        snprintf(buf, sizeof(buf), "Total VRAM I/O      : %6.1f GB/s", totalGBps);
        frame += MakeRow(buf);

        frame += MakeBorder();

        snprintf(buf, sizeof(buf), "Sampling Rate       : %d ms (%d samples/sec)", intervalMs, (intervalMs > 0 ? (1000 / intervalMs) : 0));
        frame += MakeRow(buf);

        if (enableAccumulation)
        {
            snprintf(buf, sizeof(buf), "Samples Collected   : %llu", (unsigned long long)sampleCount);
        }
        else
        {
            snprintf(buf, sizeof(buf), "Samples Collected   : OFF (Accumulation Disabled)");
        }
        frame += MakeRow(buf);

        frame += MakeBorder();
        frame += MakeRow("Exit                : Press [Q] or [ESC] to Quit");
        frame += MakeBorder();
        frame += "\n  >> [Instructions] Press 'Q' or ESC to safely stop monitoring.\n";

        std::cout << frame << std::flush;
    }

    // Restore cursor visibility
    cci.bVisible = originalCursorVis;
    SetConsoleCursorInfo(hOut, &cci);

    std::cout << "\n================================================================\n";
    std::cout << " [INFO] VRAM I/O monitoring session stopped cleanly.\n";
    std::cout << " GPU hardware profiling counters and NVML session released.\n";
    std::cout << "================================================================\n" << std::endl;

    // Graceful cleanup
    sampler.StopSampling();
    sampler.EndSession();
    sampler.Reset();
    nvml.Shutdown();

    return 0;
}
