@echo off
chcp 65001 >nul
setlocal
cd /d "%~dp0"

echo ========================================================
echo  NVIDIA GPU 성능 카운터 권한 활성화 도구 [선택 사항]
echo  [일반 사용자 권한에서도 VRAM I/O 측정이 가능하도록 설정]
echo ========================================================
echo.

:: 관리자 권한 여부 확인
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo [오류] 레지스트리 수정을 위해 관리자 권한이 필요합니다.
    echo 이 파일을 우클릭한 후 [관리자 권한으로 실행]을 선택해 주십시오.
    echo.
    pause
    exit /b 1
)

echo GPU 성능 카운터 접근 권한 레지스트리를 설정합니다...
echo.

reg add "HKLM\SYSTEM\CurrentControlSet\Services\nvlddmkm" /v RmProfilingAdminOnly /t REG_DWORD /d 0 /f
if %errorlevel% equ 0 (
    echo [성공] HKLM\SYSTEM\CurrentControlSet\Services\nvlddmkm\RmProfilingAdminOnly = 0 등록 완료
) else (
    echo [경고] nvlddmkm 키 설정 실패
)

reg add "HKLM\SYSTEM\CurrentControlSet\Control\GraphicsDrivers" /v RmProfilingAdminOnly /t REG_DWORD /d 0 /f
if %errorlevel% equ 0 (
    echo [성공] HKLM\SYSTEM\CurrentControlSet\Control\GraphicsDrivers\RmProfilingAdminOnly = 0 등록 완료
) else (
    echo [경고] GraphicsDrivers 키 설정 실패
)

echo.
echo ========================================================
echo  NVIDIA GPU 성능 카운터 권한 설정이 완료되었습니다.
echo ========================================================
echo.
pause
