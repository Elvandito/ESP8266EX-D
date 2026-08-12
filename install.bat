@echo off
setlocal EnableDelayedExpansion
chcp 65001 >nul
title Deauther Installer (Windows)

REM ============ CONFIG ============
set "PATCH_FILE=%~dp0libnet80211.a.patched"
set "PLATFORMIO=espressif8266@2.6.3"
set "BAUD=115200"
set "FLASH_ADDR=0x0"
REM ================================

echo ============================================
echo   ESP8266 Deauther + Beacon Spam - Installer
echo ============================================

REM ---------- Step 1: Check Python ----------
echo.
echo [1/5] Checking Python 3...
set "PY_CMD="
py --version >nul 2>nul && set "PY_CMD=py -3"
if not defined PY_CMD (
  python --version >nul 2>nul && set "PY_CMD=python"
)
if not defined PY_CMD (
  echo [ERROR] Python 3 not found.
  echo Download it from https://www.python.org/downloads/ and tick "Add Python to PATH".
  echo Then run this script again.
  pause
  exit /b 1
)
echo Using: %PY_CMD%
%PY_CMD% --version

REM ---------- Step 2: Install packages ----------
echo.
echo [2/5] Installing PlatformIO and esptool...
%PY_CMD% -m pip install -U platformio esptool
if errorlevel 1 (
  echo [ERROR] pip install failed. Check your internet connection.
  pause
  exit /b 1
)
echo Packages installed.

REM ---------- Step 3: SDK patch ----------
echo.
echo [3/5] Patching ESP8266 SDK (libnet80211.a)...
set "SDK_LIB="
for /d %%G in ("%USERPROFILE%\.platformio\packages\framework-arduinoespressif8266*") do (
  if exist "%%G\tools\sdk\lib\NONOSDK22x_190703\libnet80211.a" set "SDK_LIB=%%G\tools\sdk\lib\NONOSDK22x_190703\libnet80211.a"
)
if not defined SDK_LIB (
  echo [ERROR] ESP8266 framework not found. Run this once first:
  echo   %PY_CMD% -m platformio pkg install -g -p %PLATFORMIO%
  pause
  exit /b 1
)
if not exist "%SDK_LIB%.stock.bak" (
  copy /y "%SDK_LIB%" "%SDK_LIB%.stock.bak" >nul
  echo Backup created: libnet80211.a.stock.bak
) else (
  echo Backup already exists, skipping.
)
if not exist "%PATCH_FILE%" (
  echo [ERROR] Patched library not found: %PATCH_FILE%
  echo Download it from:
  echo   https://raw.githubusercontent.com/SpacehuhnTech/arduino-esp8266/deauther/tools/sdk/lib/NONOSDK22x_190703/libnet80211.a
  echo and save it as libnet80211.a.patched next to this script.
  pause
  exit /b 1
)
REM Sanity check: the patched lib is ~336 KB
for %%F in ("%PATCH_FILE%") do if %%~zF LSS 100000 (
  echo [ERROR] %PATCH_FILE% looks wrong - size %%~zF bytes.
  pause
  exit /b 1
)
copy /y "%PATCH_FILE%" "%SDK_LIB%" >nul
echo SDK patched: %SDK_LIB%

REM ---------- Step 4: Build ----------
echo.
echo [4/5] Building firmware...
set "BUILD_DIR=%TEMP%\deauther_build"
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
robocopy "%~dp0src" "%BUILD_DIR%\src" /E /NFL /NDL /NJH /NJS >nul
copy /y "%~dp0platformio.ini" "%BUILD_DIR%\platformio.ini" >nul
pushd "%BUILD_DIR%"
%PY_CMD% -m platformio run
set "BUILD_RC=%ERRORLEVEL%"
popd
if not "%BUILD_RC%"=="0" (
  echo.
  echo [ERROR] Build failed. Check the errors above.
  pause
  exit /b 1
)
copy /y "%BUILD_DIR%\.pio\build\d1mini\firmware.bin" "%~dp0firmware.bin" >nul
echo Build complete: firmware.bin

set "FIRMWARE=%~dp0firmware.bin"

REM ---------- Step 5: Flash ----------
echo.
echo [5/5] Flashing firmware to device...
echo.
echo Detecting serial ports...
powershell -NoProfile -Command "Get-WmiObject Win32_SerialPort | ForEach-Object { $_.DeviceID }" > "%TEMP%\comlist.txt" 2>nul
set "COMLIST="
set /p COMLIST=<"%TEMP%\comlist.txt"
if defined COMLIST (
  echo Found: %COMLIST%
  echo.
  echo Available ports:
  type "%TEMP%\comlist.txt"
)
set "PORT="
set /p "PORT=Enter COM port (e.g. COM6): "
if not defined PORT (
  echo [ERROR] No port given.
  pause
  exit /b 1
)
set /p "CONFIRM=Flash to %PORT%? [Y/n]: "
if /i not "!CONFIRM!"=="Y" if not "!CONFIRM!"=="" (
  echo Aborted.
  pause
  exit /b 0
)
%PY_CMD% -m esptool --port %PORT% --baud %BAUD% write_flash %FLASH_ADDR% %FIRMWARE%
if errorlevel 1 (
  echo.
  echo [ERROR] Flash failed. Hold FLASH on the board and retry.
  pause
  exit /b 1
)
echo.
echo ============================================
echo   Done! Open a serial monitor at 115200 baud.
echo   Commands: /scan  /status  /help
echo ============================================
pause