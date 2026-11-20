# SCARA Robótica

SCARA RRP v2 (PCB con Pico RP2040 + STM32 + drivers IFX007T): firmware, ROS 2 Jazzy y SCADA por MQTT.

## Carpetas
| Carpeta | Qué hay |
|---|---|
| `firmware/pico/` | firmware de la Pico (`scara_fw`): `src/` y `config/` (control RST, home, encoders, finales) |
| `firmware/stm32/` | firmware del STM32: todo en `Core/Src/scara_simple.c` (corriente, MQTT, PARAR, alertas). Se graba con ST-Link desde STM32CubeIDE |
| `bajo_nivel/` | `home.sh`: recarga la Pico y hace el Home |
| `alto_nivel/` | `ros2_ws/` (bridge, ik, p2p, teleop) y `secuencias/` (caja → bandeja) |
| `mqtt_scada/` | `servidor/` (server.js: MQTT ↔ PostgreSQL) y `grafana/` (dashboard: corriente, PARAR, alertas) |
| `docs/` | `sistema_simple.md`: cómo funciona el monitoreo de corriente, la parada y las alertas |

## Compilar
Firmware (necesita Pico SDK y el compilador ARM):
```bash
export PICO_SDK_PATH=~/pico/pico-sdk PICO_TOOLCHAIN_PATH=~/pico/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi
cmake -S firmware/pico -B firmware/pico/build && cmake --build firmware/pico/build -j
```
`bajo_nivel/home.sh` carga `firmware/pico/build/scara_fw.uf2` solo (sin BOOTSEL).

ROS 2:
```bash
cd alto_nivel/ros2_ws && source /opt/ros/jazzy/setup.bash && colcon build
```

## Usar
1. Home (recarga el firmware y deja corriendo el bridge):
```bash
bajo_nivel/home.sh
```
2. En otra terminal, cuando diga LISTO:
```bash
source /opt/ros/jazzy/setup.bash && source alto_nivel/ros2_ws/install/setup.bash
ros2 run scara_pc p2p      # escribir: x y p (mm)
ros2 run scara_pc teleop   # Q/A codo, W/S hombro, E/D prismático
alto_nivel/secuencias/secuencia.sh   # caja -> bandeja
```
Corriente, PARAR y alertas: Grafana en http://localhost:3000 (ver `docs/sistema_simple.md`).

## Pines (PCB v2)
| Eje | IN (A,B) | Encoder (A,B) | Final de carrera |
|---|---|---|---|
| R1 codo | GP3, GP2 | GP12, GP13 | GP18 |
| R2 hombro | GP1, GP0 | GP10, GP11 | GP19 |
| P | GP7, GP6 | GP14, GP15 | GP20 y GP17 (abajo) |

Servo GP8, bomba GP9. Los EN de los drivers los maneja el STM32. Finales activos en bajo con pull-up.
Home: P sube 25 mm → hombro → codo → P.
