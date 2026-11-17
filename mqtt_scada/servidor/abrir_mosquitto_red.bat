@echo off
setlocal
title Abrir Mosquitto a la red

rem === 1. Necesita administrador ===
rem (clic derecho -> Ejecutar como administrador)
net session >nul 2>&1
if errorlevel 1 (
    echo Este archivo necesita permisos de administrador.
    echo Clic derecho sobre el .bat -^> "Ejecutar como administrador".
    goto :error
)

rem === 2. Rutas ===
set "CONF=C:\Program Files\mosquitto\mosquitto.conf"
if not exist "%CONF%" (
    echo No se encontro "%CONF%".
    goto :error
)

rem === 3. Copia de seguridad (solo la primera vez) ===
if not exist "%CONF%.original" copy "%CONF%" "%CONF%.original" >nul
echo Copia de seguridad: %CONF%.original

rem === 4. Agregar el listener si todavia no esta ===
rem listener 0.0.0.0 = acepta conexiones de la red (el STM32 por Wi-Fi)
rem allow_anonymous  = sin usuario/clave (el STM32 no usa login MQTT)
findstr /r /c:"^listener 1883 0.0.0.0" "%CONF%" >nul
if errorlevel 1 (
    >>"%CONF%" echo.
    >>"%CONF%" echo # SCARA: aceptar conexiones de la red local
    >>"%CONF%" echo listener 1883 0.0.0.0
    >>"%CONF%" echo allow_anonymous true
    echo Configuracion agregada.
) else (
    echo La configuracion ya estaba agregada.
)

rem === 5. Reiniciar Mosquitto para que lea la configuracion ===
net stop mosquitto >nul 2>&1
net start mosquitto
if errorlevel 1 (
    echo Mosquitto no arranco. Revisa "%CONF%".
    goto :error
)

rem === 6. Comprobar: debe aparecer 0.0.0.0:1883 LISTENING ===
echo.
netstat -an | findstr ":1883" | findstr "LISTENING"

echo.
echo ==============================
echo    MOSQUITTO ABIERTO A LA RED
echo ==============================
echo Falta: regla de Firewall de entrada para TCP 1883 (red privada).
pause
exit /b 0

:error
echo.
echo ==============================
echo            ERROR
echo ==============================
pause
exit /b 1
