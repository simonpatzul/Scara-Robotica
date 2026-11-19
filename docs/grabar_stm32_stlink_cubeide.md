# Tarea: grabar el STM32 con ST-Link V2 desde STM32CubeIDE (PC con Windows)

Este documento es para otro entorno (otra persona u otro asistente) que no vio la conversación.
Trae todo el contexto que hace falta para grabar el firmware nuevo del STM32 y comprobar que funciona.

## Reglas del dueño del proyecto (Simon)
- Habla en español. Pide permiso antes de crear o editar archivos.
- **Cambia una sola cosa a la vez** y prueba antes de seguir.
- No muevas el robot sin que Simon lo confirme (Home, secuencia, `mv`).
- **P (el eje prismático) nunca se baja de forma automática**: puede dañar el robot.
- Commits solo si Simon lo pide. Mensaje `actualizar` y al final
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- **Nunca subas a GitHub la contraseña de PostgreSQL**: en el repo `server.js` debe decir `TU_CONTRASENA`.

## Contexto
- Robot SCARA RRP v2. Repo: https://github.com/simonpatzul/Scara-Robotica (rama `main`).
  Versión a grabar: commit `f09bda9` o posterior.
- PCB scara_v2:
  - **Pico RP2040**: control de los 3 ejes y del home. Hablaba por USB con el PC/Raspberry (`bridge`).
  - **STM32F030C8T6**: mide las corrientes, maneja los EN de los 3 drivers IFX007T (PB12, PB13, PB14) y publica por MQTT con el módulo Wi-Fi **FC41D** (USART1, PA9/PA10).
  - **BOOT0 está a GND**: el bootloader serie no sirve. Solo se graba por **SWD** (conector **J1**).
- Todo lo que va a internet sale por el FC41 del STM32. La Raspberry/PC del robot **no** usa MQTT.
- MQTT: Mosquitto en el PC del SCADA (`DEFAULT_IP` = 172.19.133.20, puerto 1883, Wi-Fi `DEFAULT_SSID` = "Simon"; ver `firmware/stm32/Core/Src/FC41/FC41.c`).
  Tópicos: `scara/estado` y `scara/evento` (los publica el STM32), `scara/cmd` (comandos de Grafana).
- Grafana (`http://localhost:3000`, dashboard `scara-est1`): sus botones insertan en la tabla `comandos` de PostgreSQL, y `mqtt_scada/servidor/server.js` los publica en `scara/cmd`.

## Qué trae este firmware (lo nuevo)
1. **Enlace I2C con la Pico.** STM32 esclavo `0x42` en **I2C1: PB6 = SCL, PB7 = SDA** (pull-ups R3/R4 de 5.1 k en la placa).
   La Pico es el maestro por software en GP4 (SCL) y GP5 (SDA). Cada 50 ms la Pico escribe su estado (16 bytes) y lee los comandos (16 bytes).
   - Código: `firmware/stm32/Core/Src/enlace/enlace.c/.h`, aparte de `main.c` para que CubeMX no lo borre.
   - Las tramas están en `firmware/pico/config/enlace_stm32.h`.
   - **El I2C no está en el `.ioc`**: lo configura `enlace_init()`, que se llama en `main.c` dentro de `USER CODE BEGIN 2`.
2. **Comandos en `scara/cmd`** (texto plano):

   | Comando | Qué hace |
   |---|---|
   | `on` / `off` | prende / apaga los 3 EN (`on` también borra las fallas) |
   | `on1`..`on3` / `off1`..`off3` | un eje (1 = R1, 2 = R2, 3 = P) |
   | `reset` | borra las fallas sin tocar los EN |
   | `mv r1,r2,p` | ir a esas coordenadas (rad, rad, mm). Va a la Pico por I2C y la Pico se lo pasa al bridge |
   | `seq` / `stop` | correr / parar la secuencia de la caja (la corre el PC o la Raspberry de la Pico) |

3. **`scara/estado`** ahora trae datos reales de la Pico:
   - `pico_ok` y la posición de cada eje (`pos`: rad en R1/R2, mm en P).
   - Los campos `pc`, `seq` y `bomba`.
   - `est` de cada eje: `R` listo, `H` home, `A` EN apagado, `F` falla, `D` sin home.
   - `ciclo`: `LISTO`, `SIN_PICO`, `SIN_HOME`, `HOME`, `SECUENCIA`, `APAGADO` o `FALLA`.
4. **`scara/evento`** (`{"eje","falla","i"}`, se publica una alerta cada 0.5 s):
   - sobrecorriente (`sostenida` / `pico`);
   - `encoder` (la Pico detectó un encoder que no cuenta);
   - `PICO` `desconectada` / `conectada`;
   - `EST` + nombre del ciclo cada vez que cambia.
5. **Protecciones de corriente solo en R1 y R2.** P se mide y se publica, pero no entra en falla.
   `PROTECCION_CORTA_EN = 0` en `scara.c`: una falla solo avisa, no apaga los EN.
6. **Busca solo el PC con Mosquitto** (`FC41/broker.c`). No hace falta cambiar `DEFAULT_IP` cuando la red "Simon" (hotspot del celular) le da otra IP al PC.
   - Activa `AT+QGETIP` (`SKIP_GETIP 0`) para saber su IP y la puerta de enlace.
   - Orden: la última IP que funcionó → `DEFAULT_IP` → la puerta de enlace → toda su subred (x.x.x.1 a .254).
   - Cada IP espera `ESPERA_BROKER_MS` = 2.5 s; si no contesta, manda `AT+QMTCLOSE=1` y pasa a la siguiente.
   - La IP que conecta se guarda en la **última página de la flash (0x0800FC00)**. Por eso el `.ld` deja `LENGTH = 63K`; no lo regreses a 64K.
   - La primera búsqueda en una red nueva puede tardar varios minutos; después arranca directo.
   - `scara/estado` trae `"broker":"<ip>"` con la IP que está usando.
   - Si el PC con Mosquitto tiene el firewall cerrado al 1883, no lo encuentra: correr `mqtt_scada/servidor/abrir_mosquitto_red.bat`.
7. Tamaño en Debug (-O0): unos **47.6 KB de 63 KB** de flash y 4.8 KB de RAM.
   No uses `strtof` ni `printf("%f")`: con eso deja de caber. Por eso `scara.c` tiene su propio `leer_numero()`.

## Pasos para grabar con ST-Link V2 y CubeIDE
1. Bajar el repo actualizado (`git pull`, o descargar el ZIP de `main`).
2. En STM32CubeIDE: **File → Import → General → Existing Projects into Workspace**, y elegir la carpeta `firmware/stm32`.
   - El proyecto se llama `scara_stm32`. No copies el proyecto al workspace si quieres que siga ligado al repo.
   - **No regeneres el código desde el `.ioc`** (no abras el `.ioc` para "Generate Code"). El I2C no está en el `.ioc`. `enlace.c` sobreviviría, pero es un riesgo innecesario.
3. **Project → Build Project** (configuración Debug).
   - Debe terminar sin errores y el resumen debe mostrar unos 47.6 KB de FLASH (de 63 KB).
   - Si dice que `FLASH` se desborda, alguien agregó `strtof` o `%f`.
4. Cableado del ST-Link V2 al conector **J1** (SWD):

   | ST-Link V2 | Placa (J1) |
   |---|---|
   | SWDIO | SWDIO (PA13) |
   | SWCLK | SWCLK (PA14) |
   | GND | GND |
   | 3.3V | **no conectar** si la placa ya tiene su alimentación (12 V / USB de la Pico); solo como referencia si el ST-Link es original |

   - La placa debe estar alimentada.
   - El robot debe estar despejado: al arrancar, el STM32 **prende los 3 EN**. Con la Pico sin órdenes de movimiento no se mueve nada.
5. **Run → Debug Configurations… → STM32 C/C++ Application → scara_stm32 Debug**, pestaña **Debugger**:
   - Debug probe: **ST-LINK (ST-LINK GDB server)**, o **ST-LINK (OpenOCD)** si el ST-Link es un clon y el primero falla.
   - Interface **SWD**, frecuencia baja (por ejemplo 1–4 MHz; si falla, 480 kHz).
   - Si no conecta: Reset behaviour = **Connect under reset**, y si es clon, cablear también NRST.
   - Si CubeIDE ofrece **actualizar el firmware del ST-Link**: con un clon chino di que **no**, porque puede dejarlo inservible.
6. **Run** (o Debug y luego Resume). CubeIDE graba y reinicia el STM32.
   Alternativa sin depurar: **STM32CubeProgrammer** → ST-LINK → SWD → Connect → abrir `Debug/scara_stm32.elf` → Download.

## Cómo comprobar que quedó bien (sin mover el robot)
En el PC del SCADA, con Mosquitto corriendo:
```
mosquitto_sub -h 172.19.133.20 -t "scara/#" -v
```
- **Al arrancar** deben salir eventos `{"eje":"EST","falla":"SIN_PICO",...}` (o `SIN_HOME` si la Pico ya tiene el firmware nuevo y contesta por I2C) y `scara/estado` cada 0.5 s.
- **Con la Pico vieja** (sin I2C): `pico_ok:false` y ciclo `SIN_PICO`. Es lo esperado. El robot funciona igual que antes, porque sin STM32 que conteste, la Pico supone los EN prendidos.
- **Con la Pico nueva y el Home hecho:** `pico_ok:true`, posiciones reales y ciclo `LISTO`.
- **Prueba de EN:** `mosquitto_pub -h 172.19.133.20 -t scara/cmd -m off2`.
  En `scara/estado`, R2 pasa a `"est":"A"`; con la Pico nueva, el hombro deja de empujar y no da tirón al volver.
  Luego `-m on2` para prenderlo otra vez.
- **No mandes `mv`, `seq` ni `stop` sin que Simon lo confirme**: mueven el robot.

## La otra mitad: firmware nuevo de la Pico
El I2C solo funciona si la Pico también tiene el firmware nuevo (`firmware/pico`, commit `f09bda9` o posterior, `scara_fw.uf2`).
- **Compilar** (Linux, con el pico-sdk): `cd firmware/pico/build && cmake .. && make scara_fw`.
- **Cargar en la laptop de Linux:** `bajo_nivel/home.sh` recarga el firmware y hace el Home.
- **Cargar en la Raspberry Pi 5:** `home.sh` no logra pasar la Pico a modo de carga. Hay que mantener **BOOTSEL** al conectar el USB y copiar `scara_fw.uf2` a la unidad `RPI-RP2`. Después, Home con `ros2 run scara_pc bridge --ros-args -p port:=/dev/ttyACM0`.
- La Raspberry tiene una copia del repo en `~/scara_ws/SCARA_Robotica`: hay que hacer `git pull` y `colcon build` en `alto_nivel/ros2_ws` para tener el bridge nuevo (`MV`, `SEQ`, `STOP`, ejes con EN apagado).

## Si algo falla
- **`pico_ok` sigue en false con las dos placas nuevas:**
  - revisar que PB6/PB7 lleguen a GP4/GP5 y que R3/R4 estén soldadas;
  - medir con osciloscopio SCL (GP4/PB6): debe haber ráfagas cada 50 ms.
- **El STM32 se queda colgado al arrancar el FC41:** no es por este cambio. Revisar `FC41.c` y el Wi-Fi "Simon".
- **Para volver a la versión anterior:** grabar el commit `bb304d7`, que no tiene I2C.
