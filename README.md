# SCARA Robótica

SCARA RRP: firmware de la Raspberry Pi Pico + workspace de ROS 2 Jazzy.

- `pico_firmware/` – firmware de la Pico (basado en jer0valencia/SCARA_RRP_)
- `ros2_ws/` – paquetes `scara_description` y `scara_pc` (basado en jer0valencia/SCARA_RRP)
- `herramientas/` – `home.sh` (reinicia la Pico y hace el home), pruebas de finales de carrera y de ejes

## Compilar
Firmware (necesita Pico SDK y el compilador ARM):
```bash
export PICO_SDK_PATH=~/pico/pico-sdk PICO_TOOLCHAIN_PATH=~/pico/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi
cmake -S pico_firmware -B pico_firmware/build && cmake --build pico_firmware/build -j
```
Cargar: conectar la Pico con BOOTSEL y copiar `pico_firmware/build/scara_fw.uf2` a `RPI-RP2`.

ROS 2:
```bash
cd ros2_ws && source /opt/ros/jazzy/setup.bash && colcon build
```

## Usar
1. Home (deja el brazo recto antes si el bridge está corriendo):
```bash
herramientas/home.sh
```
2. En otra terminal:
```bash
source /opt/ros/jazzy/setup.bash && source ros2_ws/install/setup.bash
ros2 run scara_pc p2p      # escribir: x y p (mm)
ros2 run scara_pc teleop   # Q/A codo, W/S hombro, E/D prismático
```

## Pines (baquela)
| Eje | PWM (L,R) | EN (L,R) | Encoder | Final de carrera |
|---|---|---|---|---|
| R1 codo | GP4, GP5 | GP7, GP6 | GP11, GP10 | GP19 |
| R2 hombro | GP0, GP1 | GP3, GP2 | GP12, GP13 | GP21 |
| P | GP14, GP15 | GP17, GP16 | GP8, GP9 | GP20 |

Finales de carrera NO a GND con pull-up. Home: hombro → codo → P.
