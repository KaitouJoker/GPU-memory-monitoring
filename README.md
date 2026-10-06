[English](README.md) | [한국어](README_KR.md)

# NVIDIA GPU VRAM Real-time I/O Monitor

A real-time Windows desktop monitoring tool that measures the actual physical memory read/write bandwidth (I/O Throughput) and usage between the NVIDIA GPU on-board VRAM (GDDR7) and GPU compute cores.

Unlike standard monitoring utilities that only display static VRAM allocation, this tool directly interfaces with the NVIDIA Nsight Perf SDK hardware Periodic Sampler (`GpuPeriodicSampler`) and NVML (NVIDIA Management Library) to capture genuine bus traffic on the physical memory subsystem during gaming and compute workloads.

---

## 1. Key Features

- VRAM Read Bandwidth (GB/s): Real-time throughput of GPU cores fetching textures, geometry, and shader data from physical VRAM
- VRAM Write Bandwidth (GB/s): Real-time throughput of GPU cores writing render targets, depth buffers, and compute buffers to physical VRAM
- Total VRAM I/O (GB/s): Combined real-time memory traffic (Read + Write)
- VRAM Allocation & Usage: Current VRAM usage relative to total capacity (in GB and percentage)
- Memory Controller Load (%): Real-time workload percentage of the GPU memory controller
- Laser-Aligned ASCII Dashboard: Rigid fixed-width card layout providing clean visualization regardless of terminal font quirks
- Zero-Flicker In-Place Refresh: Hides the console cursor and updates from fixed coordinates (0, 0) without screen tearing or scrolling
- settings.json Configuration: Configurable sampling intervals (ms) and sample accumulation counter toggle (On/Off)
- One-Key Safe Exit: Instantly terminates and cleanly releases GPU hardware sessions upon pressing 'Q' or ESC

---

## 2. Measurement Principle & Architecture

The traffic measured by this tool is not host-to-device PCIe bus transfers (which peak at ~64 GB/s on PCIe 5.0 x16). Instead, it captures high-speed internal bus transfers between the **GPU Cores (L2 Cache / Memory Controller) and the physical on-board GDDR7 VRAM** on the graphics card PCB:

```text
[ Host System ]                     [ NVIDIA GeForce RTX 5090 ]
  
  CPU RAM / SSD                        GPU Cores (SMs / Compute / RT)
      │                                            │
      │                                       L1/L2 Cache
      │                                            │
      │ (PCIe 5.0 Bus)                        (512-bit GDDR7 Bus)
      │ [Peak ~64 GB/s]                      [Peak ~1,792 GB/s]
      │                                            │
      └────────── DMA Controller ─────────> [ VRAM (GDDR7 32GB) ]
                                                   ▲
                                                   │
                                          Measured by this tool
                                      (dram__bytes_op_read / write)
```

- Hardware Performance Counters: Blackwell GB202 counters `dram__bytes_op_read.sum` and `dram__bytes_op_write.sum`
- Throughput Calculation Formula:
  - Read GB/s = (Delta Read Bytes / Delta Time) / 1,000,000,000
  - Write GB/s = (Delta Write Bytes / Delta Time) / 1,000,000,000
- During 3D gaming (e.g. 4K texture sampling, G-Buffer writing, Ray Tracing BVH traversal), memory traffic surges to hundreds of GB/s, reflecting true real-time memory bus pressure.

---

## 3. System Requirements

- Operating System: Windows 10 22H2 / Windows 11 (64-bit)
- Target GPU: NVIDIA GeForce RTX 5090 (Blackwell GB202) and compatible NVIDIA architectures
- Driver: NVIDIA Game Ready / Studio Driver (610.62 or later recommended)
- Privilege: **Administrator privileges** required to access hardware profiling counters per NVIDIA security policy (ERR_NVGPUCTRPERM)
- Build Tools (when building from source):
  - CMake 3.20 or newer
  - Microsoft Visual C++ (MSVC) compiler with C++17 support
  - NVIDIA Nsight Perf SDK (Public Windows package)

---

## 4. Project Structure

```text
GPU-memory-monitoring/
├── CMakeLists.txt              # CMake build configuration
├── README.md                   # Main English documentation
├── README_KR.md                # Korean documentation
├── settings.json               # Sampling rate and counter configuration
├── run_console_monitor.bat     # Launch script for console monitor
├── enable_gpu_counters.bat     # Optional registry script for non-admin profiling
├── put_here_NVIDIA_Nsight_Perf_SDK/ # Place extracted NVIDIA Nsight Perf SDK here
└── src/
    ├── step1_device_init.cpp   # Step 1: GPU recognition and Nsight Perf SDK initialization
    ├── step2_metric_enum.cpp   # Step 2: Dynamic hardware metric enumeration
    └── step3_console_monitor.cpp # Step 3: Real-time VRAM I/O console dashboard
```

---

## 5. Building from Source

### Prerequisites
1. Download the [NVIDIA Nsight Perf SDK](https://developer.nvidia.com/nsight-perf-sdk) (Windows version).
2. Extract the contents of the SDK zip archive directly into the `put_here_NVIDIA_Nsight_Perf_SDK/` directory in the project root:
   ```text
   GPU-memory-monitoring/
   └── put_here_NVIDIA_Nsight_Perf_SDK/
       ├── NvPerf/
       ├── redist/
       └── Samples/
   ```
   *(Note: The `put_here_NVIDIA_Nsight_Perf_SDK/` directory is tracked by git with a `.gitkeep` file, but its contents are ignored via `.gitignore` to comply with NVIDIA licensing and prevent large binary uploads. Users must download the SDK and populate this directory themselves.)*

> [!NOTE]
> **PATH Environment Variable is NOT required:**
> You do **not** need to add the SDK `bin` folder to your system `PATH`. CMake automatically copies `nvperf_grfx_host.dll` into the output executable directory (`build/Release/`) during the post-build step. In addition, `nvml.dll` is dynamically loaded at runtime from the default Windows system driver store (`C:\Windows\System32`).

### SDK Path Resolution
CMake resolves the Nsight Perf SDK location using the following priority order:
1. Local project folder: `put_here_NVIDIA_Nsight_Perf_SDK/` (Default & Recommended)
2. CMake parameter: `-DNVPERF_SDK_ROOT="<path-to-sdk>"`
3. Environment variable: `NVPERF_SDK_ROOT`
4. Default installation locations (e.g. `D:/NVIDIA GPU Computing Toolkit/...` or `C:/NVIDIA GPU Computing Toolkit/...`)

### Build Commands

```cmd
# 1. Generate build files (automatically detects put_here_NVIDIA_Nsight_Perf_SDK)
cmake -B build -G "Visual Studio 17 2022" -A x64

# 2. Compile Release target
cmake --build build --config Release --target step3_console_monitor
```

The executable and required DLL will be generated at `build/Release/step3_console_monitor.exe`.

---

## 6. Usage & Configuration

### Running the Monitor
1. In File Explorer, right-click `run_console_monitor.bat`.
2. Select **[Run as Administrator]**.
3. The console dashboard will launch and begin real-time measurement.
4. Press **`Q`** or **`ESC`** at any time to gracefully stop monitoring and exit.

### Configuring Settings (settings.json)
Edit `settings.json` to customize the sampling interval and sample accumulation behavior:

```json
{
  "sampling_interval_ms": 100,
  "enable_sample_accumulation": true
}
```

- `sampling_interval_ms`: Sampling interval in milliseconds. Default is 100 (10 samples/sec). Recommended: 50, 100, 250, 500, 1000.
- `enable_sample_accumulation`: Toggle for sample count tracking. If set to `false`, the counter will not increment and displays `OFF (Accumulation Disabled)` for long-term monitoring sessions.

---

## 7. Dashboard Preview

```text
+--------------------------------------------------------------+
|            NVIDIA GPU VRAM Real-time I/O Monitor             |
+--------------------------------------------------------------+
| GPU Name            : NVIDIA GeForce RTX 5090                |
| Architecture        : Blackwell (GB202)                      |
| Driver Version      : 616.56                                 |
+--------------------------------------------------------------+
| VRAM Usage          :  2.4 / 31.8 GB  (  7.5 %)              |
| Memory Controller   :   6 %                                  |
+--------------------------------------------------------------+
| VRAM Read           :    0.3 GB/s  [|                   ]    |
| VRAM Write          :    0.0 GB/s  [                    ]    |
| Total VRAM I/O      :    0.3 GB/s                            |
+--------------------------------------------------------------+
| Sampling Rate       : 100 ms (10 samples/sec)                |
| Samples Collected   : 89                                     |
+--------------------------------------------------------------+
| Exit                : Press [Q] or [ESC] to Quit             |
+--------------------------------------------------------------+

  >> [Instructions] Press 'Q' or ESC to safely stop monitoring.
```

---

## 8. Memory Safety & Resource Management

- Constant O(1) Memory Footprint: Past sample values are not appended to unbounded arrays or lists.
- Deterministic Driver Buffer Flushing: Driver record buffers are promptly decoded and acknowledged (`AcknowledgeRecordBuffer`) on each sampling tick, maintaining a steady RAM usage of ~15-20 MB even across days of operation.
- Graceful Shutdown: The GpuPeriodicSampler session and NVML context are cleanly detached and terminated upon exit.
