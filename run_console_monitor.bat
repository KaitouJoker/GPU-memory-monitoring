@echo off
setlocal
cd /d "%~dp0"

:: 관리자 권한 확인
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo ========================================================
    echo  [안내] 관리자 권한이 필요합니다.
    echo ========================================================
    echo.
    echo  'run_console_monitor.bat' 우클릭 -^> [관리자 권한으로 실행]
    echo ========================================================
    echo.
    pause
    exit /b 1
)

"build\Release\step3_console_monitor.exe" %*
