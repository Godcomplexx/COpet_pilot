@echo off
setlocal

where idf.py >nul 2>nul
if errorlevel 1 (
    echo ERROR: idf.py is not available.
    echo Open "ESP-IDF 6.0 PowerShell" and run this file there.
    exit /b 1
)

if "%~1"=="" (
    echo Usage: start.bat COM5
    echo.
    echo Available serial ports:
    python -m serial.tools.list_ports -v
    exit /b 1
)

echo Flashing CoPet hardware test to %~1...
idf.py -p %~1 -b 115200 flash monitor
