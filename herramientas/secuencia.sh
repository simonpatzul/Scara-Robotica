#!/bin/bash
# Corre la secuencia de la caja a la bandeja. Necesita home.sh corriendo en otra terminal (ya en LISTO).
# Uso: herramientas/secuencia.sh
cd "$(dirname "$0")/.." || exit 1
source /opt/ros/jazzy/setup.bash
source ros2_ws/install/setup.bash
exec python3 herramientas/secuencia_caja.py
