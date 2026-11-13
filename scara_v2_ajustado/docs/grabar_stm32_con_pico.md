# Tarea: grabar el STM32F030 usando una Raspberry Pi Pico como programador (debugprobe)

## Reglas de trabajo
- Responde en **español**.
- **Pide permiso antes de crear o editar archivos.**
- El usuario (Simon) **no quiere escribir comandos en el CMD**. Todo lo que tenga que ejecutar debe quedar en un **`.bat` de doble clic**. Tú sí puedes correr comandos para probar.
- Explicaciones cortas y por bloques. El usuario tiene que poder explicárselas a su profesor.

## Contexto del proyecto
- SCADA de un robot SCARA RRP (ejes R1, R2, P).
- **Placa de potencia/control** con:
  - una **Raspberry Pi Pico** soldada que controla los motores. Ya tiene cargado el programa de prueba `scara_prueba_pines.ino` y no hay que tocarla;
  - un **STM32F030C8T6** de protección. Mide las corrientes de los drivers con el ADC, maneja los EN de los motores (PB12–PB14) y publica por MQTT con un módulo WiFi FC41D (USART1 PA9/PA10).
- El STM32 **nunca se ha grabado**. **No hay ST-Link**. Por eso se usará una **segunda Pico (de repuesto)** como programador SWD con el firmware oficial **debugprobe** (CMSIS-DAP).
- En la placa, **BOOT0 está a GND** y USART1 está ocupado por el FC41D. **No sirve el bootloader serie**: solo SWD.

## Rutas en el PC (Windows)
- Carpeta del proyecto: `C:\Users\oscar\OneDrive\Documentos\Simon\UNIVERSIDAD\IoT\scara_v2`
- Proyecto STM32CubeIDE: `scara_v2\scara_stm32\` (generado con CubeMX 6.18.1; compila con 0 errores y 0 warnings en STM32CubeIDE 2.2.0).
- Binario esperado tras compilar: `scara_v2\scara_stm32\Debug\scara_stm32.elf`. Compruébalo; si no existe, hay que compilar primero (Build en CubeIDE o `headless-build`).
- **OpenOCD ya instalado** con el core de Arduino "Raspberry Pi Pico/RP2040" 6.3.2 (Earle Philhower):
  `%LOCALAPPDATA%\Arduino15\packages\rp2040\tools\pqt-openocd\<versión>\bin\openocd.exe`
  (la instalación mostró `pqt-openocd@5.0.0-9576866`). Los scripts deberían estar en `...\share\openocd\scripts`. **Verifica las rutas reales** y que exista `target/stm32f0x.cfg`.
  - Alternativa si ese OpenOCD no trae `stm32f0x.cfg`: el OpenOCD que trae STM32CubeIDE (carpeta `plugins\com.st.stm32cube.ide.mcu.externaltools.openocd...`).

## Hardware: conexión de la Pico programadora
1. Cargar en la Pico de repuesto el firmware oficial: https://github.com/raspberrypi/debugprobe/releases
   - Pico (RP2040): `debugprobe_on_pico.uf2`
   - Pico 2 (RP2350): `debugprobe_on_pico2.uf2`
   - Se conecta con **BOOTSEL** presionado y se copia el `.uf2` a la unidad RPI-RP2. Al reiniciar aparece como dispositivo USB "Debugprobe" (CMSIS-DAP).
2. Cables a **J1** (conector SWD de la placa). Solo estos 3:

| Pico programadora | Placa J1 |
|---|---|
| GP2 (pin 4) | SWCLK (PA14) |
| GP3 (pin 5) | SWDIO (PA13) |
| GND (pin 3) | GND |

- **NO** conectar 3V3, VSYS ni VBUS de la Pico programadora a la placa. La placa se alimenta con su propia fuente (12 V).
- NRST no está conectado → usar `reset_config none` (reset por software, SYSRESETREQ).

## Lo que tienes que hacer
1. Verificar que existen el `.elf`, `openocd.exe` y los scripts (`interface/cmsis-dap.cfg`, `target/stm32f0x.cfg`).
2. **Pedir permiso** y crear `scara_v2\grabar_stm32.bat`. Debe:
   - usar rutas con `%LOCALAPPDATA%` y relativas a la carpeta del `.bat` (`%~dp0`), no rutas fijas del usuario;
   - correr algo equivalente a:
     ```
     openocd.exe -s "<scripts>" ^
       -f interface/cmsis-dap.cfg ^
       -c "transport select swd" ^
       -c "adapter speed 1000" ^
       -f target/stm32f0x.cfg ^
       -c "reset_config none" ^
       -c "program {<ruta>/scara_stm32.elf} verify reset exit"
     ```
     (En los argumentos de OpenOCD usa `/` en las rutas.)
   - mostrar al final "GRABADO OK" o "ERROR" según el código de salida, y hacer `pause` para que la ventana no se cierre.
   - tener comentarios cortos por bloque, en español.
3. Probarlo con la Pico programadora conectada. Lo esperado en la salida de OpenOCD: `** Programming Finished **`, `** Verified OK **`, `** Resetting Target **`.
4. Si falla:
   - `unable to find a matching CMSIS-DAP device` → la Pico no tiene debugprobe o falta el driver (en Windows 10/11 normalmente no hace falta).
   - `Error connecting DP` / `init mode failed` → cables SWDIO/SWCLK invertidos o flojos, falta GND, o la placa no está alimentada. Probar `adapter speed 100`.
   - Si el chip quedó en un estado raro: `-c "init; reset halt; stm32f0x mass_erase 0; exit"` y volver a grabar.

## Seguridad
- Al arrancar, el programa del STM32 **prende los EN de los 3 motores** (PB12–PB14). La Pico de la placa tiene PWM en 0, así que nada se mueve, pero el robot debe estar despejado. **No mandes comandos de movimiento** (`+`/`-`) por el monitor serie de la Pico de la placa.
- No tocar los option bytes (nada de RDP / protección de lectura).

## Pendiente antes de grabar (avisar al usuario, no inventar valores)
En `scara_stm32\Core\Src\FC41\FC41.c` siguen los valores de ejemplo:
- `DEFAULT_SSID "NOMBRE_RED"`, `DEFAULT_PASS "CLAVE_RED"` → la red WiFi del usuario (sin espacios en el SSID).
- `DEFAULT_IP "192.168.0.101"` → la IPv4 del PC donde corre Mosquitto (sacarla con `ipconfig`).

Sin eso el STM32 graba y protege igual, pero no se conecta a MQTT. Después de cambiarlos, hay que **recompilar** para regenerar el `.elf`.

## Verificación final
- La salida de OpenOCD muestra `Verified OK`.
- Con la red configurada: en MQTTX (o `mosquitto_sub -t "scara/#"`) llegan mensajes en `scara/estado` cada 500 ms.
- Probar los motores desde el monitor serie de la Pico de la placa (COM9, tecla `1` y luego `+` = 10 %, `0` para parar). Solo si el usuario lo autoriza.
