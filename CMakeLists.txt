cmake_minimum_required(VERSION 3.20)
project(NvGpuVramMonitor CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
    add_compile_options(/utf-8)
endif()


set(NVPERF_SDK_ROOT "D:/NVIDIA GPU Computing Toolkit/NVIDIA_Nsight_Perf_SDK_2026.3_Public_Windows")

include_directories(
    "${NVPERF_SDK_ROOT}/NvPerf/include"
    "${NVPERF_SDK_ROOT}/NvPerf/include/windows-desktop-x64"
    "${NVPERF_SDK_ROOT}/Samples/NvPerfUtility/include"
)

link_directories(
    "${NVPERF_SDK_ROOT}/NvPerf/bin/x64"
)

add_executable(step1_device_init src/step1_device_init.cpp)
target_link_libraries(step1_device_init PRIVATE nvperf_grfx_host dxgi d3d12 d3d11)

add_executable(step2_metric_enum src/step2_metric_enum.cpp)
target_link_libraries(step2_metric_enum PRIVATE nvperf_grfx_host dxgi d3d12 d3d11)

add_executable(step3_console_monitor src/step3_console_monitor.cpp)
target_link_libraries(step3_console_monitor PRIVATE nvperf_grfx_host dxgi d3d12 d3d11)

# Copy dll to output directory
add_custom_command(TARGET step1_device_init POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${NVPERF_SDK_ROOT}/NvPerf/bin/x64/nvperf_grfx_host.dll"
    $<TARGET_FILE_DIR:step1_device_init>
)
add_custom_command(TARGET step2_metric_enum POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${NVPERF_SDK_ROOT}/NvPerf/bin/x64/nvperf_grfx_host.dll"
    $<TARGET_FILE_DIR:step2_metric_enum>
)
add_custom_command(TARGET step3_console_monitor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${NVPERF_SDK_ROOT}/NvPerf/bin/x64/nvperf_grfx_host.dll"
    $<TARGET_FILE_DIR:step3_console_monitor>
)


