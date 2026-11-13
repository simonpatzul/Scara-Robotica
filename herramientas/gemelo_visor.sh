#!/bin/bash
# Abre el RViz del gemelo solo para mirar: mismo modelo y misma vista que gemelo.launch.py, pero sin MoveIt, así nada planea
# ni manda trayectorias. gemelo_bridge convierte /scara/joint_states a las juntas del modelo. scada_web.py captura esta ventana.
# No cierra nada que esté corriendo, a diferencia de ver_gemelo.sh, que mataría el bridge.
AQUI="$(cd "$(dirname "$0")" && pwd)"
unset AMENT_PREFIX_PATH CMAKE_PREFIX_PATH COLCON_PREFIX_PATH   # que no se mezcle con SCARA_Robotica/ros2_ws
source /opt/ros/jazzy/setup.bash
source ~/scara_ws/gemelo_ws/install/setup.bash
PARAMS=$(mktemp --suffix=.yaml)
xacro "$(ros2 pkg prefix scara_description)/share/scara_description/urdf/scara.urdf.xacro" use_gazebo:=false | python3 -c '
import sys, yaml
yaml.safe_dump({"robot_state_publisher": {"ros__parameters": {"robot_description": sys.stdin.read()}}}, open(sys.argv[1], "w"))' "$PARAMS"
ros2 run scara_gemelo gemelo_bridge &
PUENTE=$!
python3 "$AQUI/gemelo_reposo.py" &
REPOSO=$!
ros2 run robot_state_publisher robot_state_publisher --ros-args --params-file "$PARAMS" &
RSP=$!
trap 'kill -INT $PUENTE $REPOSO $RSP 2>/dev/null; rm -f "$PARAMS"' EXIT
rviz2 -d "$AQUI/gemelo_visor.rviz"
