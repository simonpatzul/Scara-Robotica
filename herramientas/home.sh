#!/bin/bash
# Reinicia la Pico (recargando el firmware) y arranca el bridge, que hace el home.
# Uso: herramientas/home.sh      (Ctrl+C para detener el bridge)
REPO="$(cd "$(dirname "$0")/.." && pwd)"
# Si el bridge está corriendo, primero llevar el brazo recto (0,0,0) para que el home no choque
if pgrep -x bridge >/dev/null; then
  source /opt/ros/jazzy/setup.bash
  echo "Llevando el brazo a 0,0,0 antes del home..."
  ros2 topic pub --once -w 1 /scara/target std_msgs/msg/Float64MultiArray "{data: [0.0, 0.0, 0.0]}" >/dev/null 2>&1
  sleep 8
fi
pkill -INT -x bridge 2>/dev/null; sleep 2
python3 -c "
import serial
s = serial.Serial('/dev/ttyACM0', 115200)
s.baudrate = 1200            # 1200 baudios = la Pico entra en modo de carga
try: s.baudrate = 115200     # se deja el puerto en 115200 para el próximo uso
except Exception: pass
try: s.close()
except Exception: pass
"
for i in $(seq 1 15); do [ -f /media/$USER/RPI-RP2/INFO_UF2.TXT ] && break; sleep 1; done
cp "$REPO/pico_firmware/build/scara_fw.uf2" /media/$USER/RPI-RP2/ && sync
for i in $(seq 1 15); do sleep 1; [ -e /dev/ttyACM0 ] && break; done
sleep 4
source /opt/ros/jazzy/setup.bash && source "$REPO/ros2_ws/install/setup.bash"
ros2 run scara_pc bridge
