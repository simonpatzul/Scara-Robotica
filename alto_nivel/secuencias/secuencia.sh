#!/bin/bash
# Corre la secuencia de la caja a la bandeja. Necesita home.sh corriendo en otra terminal (ya en LISTO).
# Uso: alto_nivel/secuencias/secuencia.sh
cd "$(dirname "$0")/../.." || exit 1
source /opt/ros/jazzy/setup.bash
source alto_nivel/ros2_ws/install/setup.bash
exec python3 alto_nivel/secuencias/secuencia_caja.py
