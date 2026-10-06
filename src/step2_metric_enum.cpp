#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <algorithm>
#include <cctype>
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
#include <NvPerfD3D.h>
#include <NvPerfD3D12.h>

using Microsoft::WRL::ComPtr;

static std::string ToLower(const std::string& str)
{
    std::string s = str;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

static bool ContainsAnyKeyword(const std::string& text, const std::vector<std::string>& keywords)
{
    std::string lowerText = ToLower(text);
    for (const auto& kw : keywords)
    {
        if (lowerText.find(kw) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

static const char* GetMetricTypeName(NVPW_MetricType type)
{
    switch (type)
    {
    case NVPW_METRIC_TYPE_COUNTER: return "Counter";
    case NVPW_METRIC_TYPE_RATIO: return "Ratio";
    case NVPW_METRIC_TYPE_THROUGHPUT: return "Throughput";
    default: return "Unknown";
    }
}

static std::string FormatDimUnits(NVPW_MetricsEvaluator* pEvaluator, const NVPW_MetricEvalRequest& req)
{
    std::vector<NVPW_DimUnitFactor> dimUnits;
    if (!nv::perf::GetMetricDimUnits(pEvaluator, req, dimUnits) || dimUnits.empty())
    {
        return "Dimensionless / Count";
    }

    return nv::perf::ToString(dimUnits, [&](NVPW_DimUnitName dimUnit, bool plural) {
        return nv::perf::ToCString(pEvaluator, dimUnit, plural);
    });
}

int main()
{
    std::cout << "================================================================" << std::endl;
    std::cout << " NVIDIA GPU VRAM I/O Monitor - Step 2                           " << std::endl;
    std::cout << " Runtime Metric Enumeration (RTX 5090 / GB202)                  " << std::endl;
    std::cout << "================================================================" << std::endl;

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
    std::cout << "GPU: " << identifiers.pDeviceName << " (" << identifiers.pChipName << ")" << std::endl;
    std::cout << "----------------------------------------------------------------\n" << std::endl;

    // Create MetricsEvaluator
    std::vector<uint8_t> scratchBuffer;
    NVPW_MetricsEvaluator* pMetricsEvaluator = nv::perf::sampler::DeviceCreateMetricsEvaluator(scratchBuffer, identifiers.pChipName);
    if (!pMetricsEvaluator)
    {
        std::cerr << "[ERROR] Failed to create MetricsEvaluator" << std::endl;
        return 1;
    }
    nv::perf::MetricsEvaluator evaluator(pMetricsEvaluator, std::move(scratchBuffer));

    std::vector<std::string> searchKeywords = {
        "dram", "memory", "read", "write", "bandwidth", "throughput", "fbpa"
    };

    struct EnumeratedMetric {
        std::string baseName;
        std::string fullName;
        std::string type;
        std::string unit;
        std::string description;
        bool schedulable;
    };

    std::vector<EnumeratedMetric> results;

    const NVPW_MetricType metricTypes[] = {
        NVPW_METRIC_TYPE_COUNTER,
        NVPW_METRIC_TYPE_RATIO,
        NVPW_METRIC_TYPE_THROUGHPUT
    };

    std::cout << "Metric enumeration\n" << std::endl;
    std::cout << "[DRAM / Memory / Bandwidth Metrics on RTX 5090]\n" << std::endl;

    for (NVPW_MetricType mType : metricTypes)
    {
        nv::perf::MetricsEnumerator enumerator = nv::perf::EnumerateMetrics(evaluator.Get(), mType);
        std::vector<NVPW_Submetric> submetrics;
        evaluator.GetSupportedSubmetrics(mType, submetrics);

        for (const char* pBaseName : enumerator)
        {
            std::string baseName = pBaseName ? pBaseName : "";
            if (baseName.empty()) continue;

            if (!ContainsAnyKeyword(baseName, searchKeywords)) continue;

            size_t mIndex = 0;
            NVPW_MetricType outType;
            evaluator.GetMetricTypeAndIndex(baseName.c_str(), outType, mIndex);
            const char* pDesc = evaluator.GetMetricDescription(outType, mIndex);
            std::string description = pDesc ? pDesc : "";

            // Try candidate submetrics to find valid full request names
            std::vector<std::string> candidateFullNames;
            if (mType == NVPW_METRIC_TYPE_COUNTER)
            {
                candidateFullNames.push_back(baseName + ".sum");
                candidateFullNames.push_back(baseName + ".avg");
            }
            else if (mType == NVPW_METRIC_TYPE_THROUGHPUT)
            {
                candidateFullNames.push_back(baseName + ".avg.pct_of_peak_sustained_elapsed");
                candidateFullNames.push_back(baseName + ".sum.pct_of_peak_sustained_elapsed");
                candidateFullNames.push_back(baseName + ".avg.pct_of_peak_sustained_active");
            }
            else if (mType == NVPW_METRIC_TYPE_RATIO)
            {
                candidateFullNames.push_back(baseName + ".pct");
                candidateFullNames.push_back(baseName + ".ratio");
            }

            for (const auto& fullName : candidateFullNames)
            {
                NVPW_MetricEvalRequest req{};
                if (evaluator.ToMetricEvalRequest(fullName.c_str(), req))
                {
                    EnumeratedMetric em;
                    em.baseName = baseName;
                    em.fullName = fullName;
                    em.type = GetMetricTypeName(mType);
                    em.unit = FormatDimUnits(evaluator.Get(), req);
                    em.description = description;

                    // Test Schedulability with MetricsConfigBuilder
                    NVPW_RawCounterConfig* pRawCounterConfig = nv::perf::sampler::DeviceCreateRawCounterConfig(identifiers.pChipName);
                    if (pRawCounterConfig)
                    {
                        nv::perf::MetricsConfigBuilder configBuilder;
                        if (configBuilder.Initialize(evaluator.Get(), pRawCounterConfig, identifiers.pChipName))
                        {
                            em.schedulable = configBuilder.AddMetrics(&req, 1);
                        }
                        // Note: configBuilder owns pRawCounterConfig and will destroy it automatically
                    }

                    results.push_back(em);
                    break; // Pick the primary valid representation
                }
            }
        }
    }

    // Print all discovered and validated metrics
    for (const auto& m : results)
    {
        // Focus primarily on DRAM and FBPA metrics
        if (ContainsAnyKeyword(m.baseName, {"dram", "fbpa", "syslts", "gpu__dram"}))
        {
            std::cout << "Name:        " << m.fullName << "\n";
            std::cout << "Unit:        " << m.unit << "\n";
            std::cout << "Description: " << m.description << "\n";
            std::cout << "Type:        " << m.type << "\n";
            std::cout << "Supported:   " << (m.schedulable ? "YES" : "NO") << "\n";
            std::cout << "--------------------------------------------------------" << std::endl;
        }
    }

    // Test Concurrent Multi-Metric Configuration (Read + Write + Throughput in 1 pass)
    std::cout << "\n================================================================" << std::endl;
    std::cout << " Testing Multi-Metric Single-Pass Configuration (DRAM Read + Write)" << std::endl;
    std::cout << "================================================================" << std::endl;

    std::vector<std::string> comboTest = {
        "dram__bytes_op_read.sum",
        "dram__bytes_op_write.sum",
        "dram__bytes.sum"
    };

    std::vector<NVPW_MetricEvalRequest> comboRequests;
    for (const auto& name : comboTest)
    {
        NVPW_MetricEvalRequest req{};
        if (evaluator.ToMetricEvalRequest(name.c_str(), req))
        {
            comboRequests.push_back(req);
            std::cout << "[OK] Request created: " << name << std::endl;
        }
        else
        {
            std::cout << "[FAIL] Could not create request: " << name << std::endl;
        }
    }

    if (!comboRequests.empty())
    {
        NVPW_RawCounterConfig* pRawCounterConfig = nv::perf::sampler::DeviceCreateRawCounterConfig(identifiers.pChipName);
        nv::perf::MetricsConfigBuilder configBuilder;
        if (configBuilder.Initialize(evaluator.Get(), pRawCounterConfig, identifiers.pChipName))
        {
            bool addedAll = configBuilder.AddMetrics(comboRequests.data(), comboRequests.size());
            std::cout << "\nConfiguring [dram__bytes_op_read.sum + dram__bytes_op_write.sum + dram__bytes.sum] together in 1 pass:" << std::endl;
            std::cout << "AddMetrics Success: " << (addedAll ? "YES" : "NO") << std::endl;

            nv::perf::CounterConfiguration configuration;
            if (nv::perf::CreateConfiguration(configBuilder, configuration))
            {
                std::cout << "Configuration Created! Num Passes = " << configuration.numPasses << std::endl;
                std::cout << "Config Image Size = " << configuration.configImage.size() << " bytes" << std::endl;
                std::cout << "Counter Data Prefix Size = " << configuration.counterDataPrefix.size() << " bytes" << std::endl;
            }
            else
            {
                std::cout << "[FAIL] CreateConfiguration failed!" << std::endl;
            }
        }
    }

    return 0;
}
