@echo off
REM Opens inbound TCP 11434 so the CoPet device can reach a LAN Ollama server.
REM Double-click this file and approve the UAC prompt.

net session >nul 2>&1
if %errorlevel% neq 0 (
    powershell -Command "Start-Process '%~f0' -Verb RunAs"
    exit /b
)

netsh advfirewall firewall add rule name="Ollama LAN 11434" dir=in action=allow protocol=TCP localport=11434
echo.
echo Done. Inbound TCP 11434 is now allowed. You can close this window.
pause
