#!/bin/bash
# Reinicia la Pico (recargando el firmware) y arranca el bridge, que hace el home.
# Se queda corriendo; cuando diga LISTO, corre herramientas/secuencia.sh en otra terminal.
# Uso: herramientas/home.sh      (Ctrl+C para detener el bridge)
REPO="$(cd "$(dirname "$0")/.." && pwd)"
source /opt/ros/jazzy/setup.bash

# Si el bridge está corriendo, primero llevar el brazo recto (0,0,0) para que el home no choque
if pgrep -f "scara_pc/bridge" >/dev/null; then
  echo "Llevando el brazo a 0,0,0 antes del home: hombro, codo y al final P..."
  python3 - <<'EOF'
import time
import rclpy
from std_msgs.msg import Float64MultiArray
from sensor_msgs.msg import JointState
rclpy.init()
n = rclpy.create_node('antes_del_home')
m = [None]
n.create_subscription(JointState, '/scara/joint_states', lambda s: m.__setitem__(0, list(s.position)), 10)
pub = n.create_publisher(Float64MultiArray, '/scara/target', 10)
t0 = time.time()
while m[0] is None and time.time() - t0 < 3:
    rclpy.spin_once(n, timeout_sec=0.1)
if m[0] is not None:   # [R1 codo, R2 hombro, P]
    r1, _, p = m[0][:3]
    for objetivo, seg in (([r1, 0.0, p], 4), ([0.0, 0.0, p], 4), ([0.0, 0.0, 0.0], 8)):
        t0 = time.time()
        while time.time() - t0 < seg:
            pub.publish(Float64MultiArray(data=objetivo))
            rclpy.spin_once(n, timeout_sec=0.2)
            if all(abs(a - b) < 0.05 for a, b in zip(m[0], objetivo)):
                break
EOF
fi
pkill -INT -f "scara_pc/bridge" 2>/dev/null; sleep 2

# Esperar a que la Pico aparezca (como puerto serie o ya en modo de carga)
for i in $(seq 1 20); do
  { ls /dev/ttyACM* >/dev/null 2>&1 || [ -d /media/$USER/RPI-RP2 ]; } && break
  sleep 1
done

# Pasar la Pico a modo de carga (si no lo está) para recargar scara_fw: al arrancar vuelve a esperar el home
if [ ! -d /media/$USER/RPI-RP2 ]; then
  P=$(ls /dev/ttyACM* 2>/dev/null | tail -1)
  [ -z "$P" ] && { echo "No encuentro la Pico: revisa el USB"; exit 1; }
  # Abrir y cerrar el puerto a 1200 baudios = la Pico entra en modo de carga
  python3 -c "import serial; serial.Serial('$P', 1200).close()" 2>/dev/null
  for i in $(seq 1 15); do [ -f /media/$USER/RPI-RP2/INFO_UF2.TXT ] && break; sleep 1; done
fi
sleep 1
[ -d /media/$USER/RPI-RP2 ] || { echo "La Pico no entró en modo de carga (puede estar trabada): desconecta y conecta su USB y vuelve a correr home.sh"; exit 1; }
cp "$REPO/pico_firmware/build/scara_fw.uf2" /media/$USER/RPI-RP2/ && sync || { echo "No pude cargar el firmware"; exit 1; }
echo "Firmware cargado, esperando a la Pico..."
for i in $(seq 1 15); do sleep 1; ls /dev/ttyACM* >/dev/null 2>&1 && break; done
sleep 3

source "$REPO/ros2_ws/install/setup.bash"
exec ros2 run scara_pc bridge --ros-args -p port:=$(ls /dev/ttyACM* | tail -1)
