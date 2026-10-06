[English](README.md) | [한국어](README_KR.md)

# NVIDIA GPU VRAM 실시간 I/O 모니터

NVIDIA GPU의 온보드 VRAM(GDDR7)과 GPU 연산 코어 간의 실제 실시간 메모리 읽기/쓰기 대역폭(I/O Throughput) 및 사용량을 측정하는 Windows 데스크톱 모니터링 도구입니다.

단순한 VRAM 할당량 표시를 넘어, NVIDIA Nsight Perf SDK의 하드웨어 주기적 성능 샘플러(Periodic Sampler)와 NVML(NVIDIA Management Library)을 직접 활용하여 실제 물리 메모리 버스에서 발생하는 실시간 트래픽을 측정합니다.

---

## 1. 주요 기능

- VRAM Read Bandwidth (GB/s): GPU 코어가 물리 VRAM에서 데이터를 읽어오는 실시간 대역폭
- VRAM Write Bandwidth (GB/s): GPU 코어가 렌더 타깃 및 연산 결과를 VRAM에 기록하는 실시간 대역폭
- Total VRAM I/O (GB/s): 실시간 총 메모리 I/O 처리량 (Read + Write)
- VRAM 사용량 및 점유율: 전체 용량 대비 현재 사용 중인 VRAM 용량 (GB 및 %)
- Memory Controller Load (%): GPU 메모리 컨트롤러의 실시간 부하율
- 레이저 정렬 ASCII 대시보드: 폰트나 터미널 환경에 구애받지 않는 고정폭 카드 UI
- 무깜빡임(Zero-Flicker) 인플레이스 갱신: 콘솔 커서를 숨기고 고정 좌표에서 갱신하여 깜빡임 없는 모니터링 제공
- settings.json 환경 설정: 샘플링 주기(ms) 조절 및 누적 카운트 표시 On/Off 지원
- 안전한 원클릭 종료: 'Q' 키 또는 ESC 키를 통해 GPU 세션을 정상 정리하고 즉시 종료

---

## 2. 측정 원리 및 아키텍처

본 도구가 측정하는 트래픽은 시스템 RAM ↔ VRAM 간의 PCIe 전송(최대 64 GB/s)이 아니라, 그래픽카드 기판 내부의 **GPU Core(L2 캐시/메모리 컨트롤러) ↔ 온보드 VRAM(GDDR7 물리 메모리)** 간의 초고속 메모리 버스 트래픽입니다.

```text
[ 시스템 영역 ]                     [ NVIDIA GeForce RTX 5090 ]
  
  CPU RAM / SSD                        GPU Core (SM / 셰이더 연산)
      │                                            │
      │                                       L1/L2 Cache
      │                                            │
      │ (PCIe 5.0 버스)                       (512-bit GDDR7 버스)
      │ [최대 ~64 GB/s]                      [최대 ~1,792 GB/s]
      │                                            │
      └────────── DMA Controller ─────────> [ VRAM (GDDR7 32GB) ]
                                                   ▲
                                                   │
                                            본 도구 측정 지점
                                     (dram__bytes_op_read / write)
```

- 측정 카운터: NVIDIA Blackwell 아키텍처(GB202)의 하드웨어 카운터인 `dram__bytes_op_read.sum` 및 `dram__bytes_op_write.sum`
- 대역폭 계산식:
  - Read GB/s = (Delta Read Bytes / Delta Time) / 1,000,000,000
  - Write GB/s = (Delta Write Bytes / Delta Time) / 1,000,000,000
- 3D 게임 구동 시 텍스처 샘플링, 지오메트리 로드, 렌더 타깃 쓰기, 레이트레이싱 가속 구조 순회 등으로 인해 수백 GB/s 이상의 부하가 실시간으로 측정됩니다.

---

## 3. 실행 환경 및 요구 사항

- 운영체제: Windows 10 22H2 / Windows 11 (64-bit)
- 대상 GPU: NVIDIA GeForce RTX 5090 (Blackwell GB202 아키텍처) 및 호환 NVIDIA GPU
- 드라이버: NVIDIA Game Ready / Studio Driver (610.62 이상 권장)
- 권한: NVIDIA 보안 정책(ERR_NVGPUCTRPERM)에 따라 GPU 하드웨어 성능 카운터 접근을 위해 **관리자 권한(Administrator)** 필수
- 빌드 도구 (소스 빌드 시):
  - CMake 3.20 이상
  - Microsoft Visual C++ (MSVC) C++17 지원 컴파일러
  - NVIDIA Nsight Perf SDK (Public Windows 패키지)

---

## 4. 디렉터리 구성

```text
GPU-memory-monitoring/
├── CMakeLists.txt              # CMake 빌드 설정 파일
├── README.md                   # 영문 메인 문서
├── README_KR.md                # 한국어 문서
├── settings.json               # 샘플링 주기 및 누적 카운트 설정 파일
├── run_console_monitor.bat     # 콘솔 모니터 실행 런처
├── enable_gpu_counters.bat     # 비관리자 계정용 레지스트리 권한 설정 도구 (선택 사항)
├── put_here_NVIDIA_Nsight_Perf_SDK/ # NVIDIA Nsight Perf SDK 압축 해제 위치
└── src/
    ├── step1_device_init.cpp   # 1단계: GPU 인식 및 Nsight Perf SDK 연동 테스트
    ├── step2_metric_enum.cpp   # 2단계: RTX 5090 지원 하드웨어 메트릭 동적 열거
    └── step3_console_monitor.cpp # 3단계: 실시간 VRAM I/O 콘솔 모니터 핵심 소스
```

---

## 5. 빌드 방법

### 사전 준비
1. [NVIDIA Nsight Perf SDK](https://developer.nvidia.com/nsight-perf-sdk) (Windows 패키지)를 다운로드합니다.
2. 다운로드한 SDK 압축 파일의 내용물을 프로젝트 루트의 `put_here_NVIDIA_Nsight_Perf_SDK/` 폴더에 압축 해제합니다:
   ```text
   GPU-memory-monitoring/
   └── put_here_NVIDIA_Nsight_Perf_SDK/
       ├── NvPerf/
       ├── redist/
       └── Samples/
   ```
   *(참고: `put_here_NVIDIA_Nsight_Perf_SDK/` 폴더는 `.gitkeep` 파일을 통해 git에 등록되어 있으나, 내부 SDK 내용물은 `.gitignore`에 의해 GitHub에 업로드되지 않으므로 사용자가 직접 SDK를 받아 채워 넣어야 합니다.)*

> [!NOTE]
> **시스템 PATH 환경 변수 등록 불필요:**
> SDK의 bin 디렉터리를 시스템 `PATH` 환경 변수에 등록할 필요가 **없습니다**. CMake 빌드 완료 후 후처리 명령(`POST_BUILD`)에 의해 `nvperf_grfx_host.dll` 파일이 실행 파일과 동일한 디렉터리(`build/Release/`)로 자동 복사됩니다. 또한 GPU 제어 라이브러리인 `nvml.dll` 역시 Windows 드라이버 기본 저장소(`System32`)에서 런타임에 동적으로 로드됩니다.

### SDK 경로 탐색 우선순위
CMake는 다음 순서로 Nsight Perf SDK 설치 경로를 자동 감지합니다:
1. 로컬 프로젝트 폴더: `put_here_NVIDIA_Nsight_Perf_SDK/` (기본값 및 권장 방식)
2. CMake 명령행 인자: `-DNVPERF_SDK_ROOT="<SDK 설치 경로>"`
3. 시스템 환경 변수: `NVPERF_SDK_ROOT`
4. 기본 후보 경로 (예: `D:/NVIDIA GPU Computing Toolkit/...` 또는 `C:/NVIDIA GPU Computing Toolkit/...`)

### 빌드 명령

```cmd
# 1. 빌드 디렉터리 생성 및 설정 (put_here_NVIDIA_Nsight_Perf_SDK 자동 감지)
cmake -B build -G "Visual Studio 17 2022" -A x64

# 2. Release 타깃 빌드
cmake --build build --config Release --target step3_console_monitor
```

빌드가 완료되면 `build/Release/step3_console_monitor.exe` 실행 파일 및 필수 DLL이 생성됩니다.

---

## 6. 사용 방법

### 실행하기
1. 파일 탐색기에서 `run_console_monitor.bat` 파일을 마우스 우클릭합니다.
2. **[관리자 권한으로 실행]** 을 선택합니다.
3. 콘솔 창이 열리며 실시간 모니터링이 시작됩니다.
4. 모니터링을 종료하려면 키보드 **`Q`** 키 또는 **`ESC`** 키를 누릅니다.

### 설정 변경 (settings.json)
`settings.json` 파일을 편집하여 측정 주기 및 카운터 누적 동작을 조정할 수 있습니다.

```json
{
  "sampling_interval_ms": 100,
  "enable_sample_accumulation": true
}
```

- `sampling_interval_ms`: 샘플링 간격(밀리초). 기본값은 100(초당 10회). 권장값: 50, 100, 250, 500, 1000
- `enable_sample_accumulation`: 샘플 누적 카운트 활성화 여부. false로 설정 시 장시간 구동 시에도 카운터가 증가하지 않고 `OFF (Accumulation Disabled)`로 표시됩니다.

---

## 7. 화면 예시

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

## 8. 메모리 안전성 및 리소스 관리

- 본 모니터링 도구는 과거 샘플 데이터를 메모리에 누적 저장하지 않는 고정 메모리 구조(O(1))로 동작합니다.
- 매 샘플링 주기마다 GPU 드라이버 레코드 버퍼를 즉시 확인 및 승인(AcknowledgeRecordBuffer)하여 반환하므로, 장시간 연속 실행 시에도 메모리 누수 없이 약 15~20MB 내외의 일정한 RAM 점유율을 유지합니다.
- 프로그램 종료 시 Nsight Perf SDK 샘플러 세션과 NVML 라이브러리를 안전하게 해제(Graceful Shutdown)합니다.
