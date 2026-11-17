@echo off
setlocal
title Grabar STM32F030 con Pico debugprobe
cd /d "%~dp0"

rem === 1. Rutas ===
rem OpenOCD que instalo el core RP2040 de Arduino (busca la version que haya).
set "OCD_BASE=%LOCALAPPDATA%\Arduino15\packages\rp2040\tools\pqt-openocd"
set "OPENOCD="
for /d %%D in ("%OCD_BASE%\*") do (
    if exist "%%D\bin\openocd.exe" set "OCD_DIR=%%D"
)
if defined OCD_DIR set "OPENOCD=%OCD_DIR%\bin\openocd.exe"
set "SCRIPTS=%OCD_DIR%\share\openocd\scripts"

rem Programa compilado por STM32CubeIDE (relativo a esta carpeta).
set "ELF=%~dp0scara_stm32\Debug\scara_stm32.elf"

rem === 2. Comprobaciones ===
if not defined OPENOCD (
    echo No se encontro openocd.exe en "%OCD_BASE%".
    echo Instala el core "Raspberry Pi Pico/RP2040" en Arduino IDE.
    goto :error
)
if not exist "%ELF%" (
    echo No existe "%ELF%".
    echo Abre el proyecto en STM32CubeIDE y dale Build.
    goto :error
)

rem OpenOCD quiere las rutas con "/" en vez de "\".
set "ELF_OCD=%ELF:\=/%"
set "SCRIPTS_OCD=%SCRIPTS:\=/%"

echo Programador : Pico con debugprobe (CMSIS-DAP, SWD)
echo Archivo     : %ELF%
echo.

rem === 3. Grabar ===
rem cmsis-dap   = la Pico programadora
rem swd         = solo 2 cables de datos (SWCLK, SWDIO) + GND
rem WORKAREA 0  = no usa la RAM del STM32 para grabar (el algoritmo
rem               en RAM se colgaba); escribe directo, mas lento pero fiable
rem 100 kHz     = velocidad lenta pero segura (a 1000 kHz se cortaba);
rem               va despues del target, si no este la pisa
rem reset none  = NRST no esta conectado, se resetea por software
rem program     = borra, graba, verifica y reinicia el STM32
"%OPENOCD%" -s "%SCRIPTS_OCD%" ^
  -f interface/cmsis-dap.cfg ^
  -c "transport select swd" ^
  -c "set WORKAREASIZE 0" ^
  -f target/stm32f0x.cfg ^
  -c "adapter speed 100" ^
  -c "reset_config none" ^
  -c "program {%ELF_OCD%} verify reset exit"

rem === 4. Resultado ===
if errorlevel 1 goto :error

echo.
echo ==============================
echo          GRABADO OK
echo ==============================
pause
exit /b 0

:error
echo.
echo ==============================
echo            ERROR
echo ==============================
echo Revisa: Pico con debugprobe conectada por USB, cables
echo GP2-SWCLK, GP3-SWDIO, GND-GND y la placa alimentada (12 V).
pause
exit /b 1
