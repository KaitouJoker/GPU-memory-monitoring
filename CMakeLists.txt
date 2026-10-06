cmake_minimum_required(VERSION 3.20)
project(NvGpuVramMonitor CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
    add_compile_options(/utf-8)
endif()

# -------------------------------------------------------------
# Locate NVIDIA Nsight Perf SDK Root
# -------------------------------------------------------------
# Detection precedence:
# 1. Project directory: put_here_NVIDIA_Nsight_Perf_SDK
# 2. CMake parameter: -DNVPERF_SDK_ROOT="<path>"
# 3. Environment variable: NVPERF_SDK_ROOT
# 4. Standard fallback locations
if (NOT DEFINED NVPERF_SDK_ROOT OR "${NVPERF_SDK_ROOT}" STREQUAL "")
    set(LOCAL_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/put_here_NVIDIA_Nsight_Perf_SDK")
    if (EXISTS "${LOCAL_SDK_DIR}/NvPerf/include/nvperf_host.h")
        set(NVPERF_SDK_ROOT "${LOCAL_SDK_DIR}")
    else()
        # In case the zip was extracted into a nested subfolder inside put_here_NVIDIA_Nsight_Perf_SDK
        file(GLOB SUBDIRS "${LOCAL_SDK_DIR}/*")
        foreach(SUBDIR ${SUBDIRS})
            if (IS_DIRECTORY "${SUBDIR}" AND EXISTS "${SUBDIR}/NvPerf/include/nvperf_host.h")
                set(NVPERF_SDK_ROOT "${SUBDIR}")
                break()
            endif()
        endforeach()
    endif()

    if ((NOT DEFINED NVPERF_SDK_ROOT OR "${NVPERF_SDK_ROOT}" STREQUAL "") AND DEFINED ENV{NVPERF_SDK_ROOT} AND NOT "$ENV{NVPERF_SDK_ROOT}" STREQUAL "")
        set(NVPERF_SDK_ROOT "$ENV{NVPERF_SDK_ROOT}")
    endif()

    if (NOT DEFINED NVPERF_SDK_ROOT OR "${NVPERF_SDK_ROOT}" STREQUAL "")
        set(DEFAULT_CANDIDATE_PATHS
            "D:/NVIDIA GPU Computing Toolkit/NVIDIA_Nsight_Perf_SDK_2026.3_Public_Windows"
            "C:/NVIDIA GPU Computing Toolkit/NVIDIA_Nsight_Perf_SDK_2026.3_Public_Windows"
            "C:/Program Files/NVIDIA Corporation/NVIDIA Nsight Perf SDK"
        )
        foreach(CANDIDATE ${DEFAULT_CANDIDATE_PATHS})
            if (EXISTS "${CANDIDATE}/NvPerf/include/nvperf_host.h")
                set(NVPERF_SDK_ROOT "${CANDIDATE}")
                break()
            endif()
        endforeach()
    endif()
endif()

if (NVPERF_SDK_ROOT)
    file(TO_CMAKE_PATH "${NVPERF_SDK_ROOT}" NVPERF_SDK_ROOT)
endif()

set(NVPERF_SDK_ROOT "${NVPERF_SDK_ROOT}" CACHE PATH "Root directory of NVIDIA Nsight Perf SDK" FORCE)

message(STATUS "Locating NVIDIA Nsight Perf SDK...")
if (NOT EXISTS "${NVPERF_SDK_ROOT}/NvPerf/include/nvperf_host.h")
    message(FATAL_ERROR
        "\n========================================================================\n"
        "[ERROR] NVIDIA Nsight Perf SDK was not found at: '${NVPERF_SDK_ROOT}'\n"
        "Please extract the NVIDIA Nsight Perf SDK into:\n"
        "  ${CMAKE_CURRENT_SOURCE_DIR}/put_here_NVIDIA_Nsight_Perf_SDK\n"
        "\n"
        "Or specify the SDK root directory via:\n"
        "  - CMake argument:   cmake -B build -DNVPERF_SDK_ROOT=\"<path-to-sdk>\"\n"
        "  - Environment var:  set NVPERF_SDK_ROOT=<path-to-sdk>\n"
        "Download SDK: https://developer.nvidia.com/nsight-perf-sdk\n"
        "========================================================================"
    )
endif()

message(STATUS "Found NVIDIA Nsight Perf SDK: ${NVPERF_SDK_ROOT}")

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
