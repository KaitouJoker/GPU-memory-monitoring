@echo off
setlocal
cd /d "%~dp0"

:: Check administrator privileges
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo ========================================================
    echo  [NOTICE] Administrator privileges required.
    echo ========================================================
    echo.
    echo  Right-click 'run_console_monitor.bat' -^> [Run as Administrator]
    echo ========================================================
    echo.
    pause
    exit /b 1
)

"build\Release\step3_console_monitor.exe" %*
