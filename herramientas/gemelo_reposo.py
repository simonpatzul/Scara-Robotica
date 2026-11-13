# Mantiene armado el modelo del gemelo cuando no llegan datos del robot: sin /joint_states RViz amontona las piezas en el origen.
# Publica las juntas en 0 solo mientras /scara/joint_states esté callado; apenas llega el real, se calla y manda gemelo_bridge.
# Solo es para el dibujo: no publica nada hacia el robot, y la página avisa que esa pose no es una posición medida.
import time
import rclpy
from sensor_msgs.msg import JointState

CALLADO_S = 1.0
JUNTAS = ['joint1', 'joint2', 'joint3', 'joint_servo']

rclpy.init()
nodo = rclpy.create_node('gemelo_reposo')
pub = nodo.create_publisher(JointState, '/joint_states', 10)
t_real = 0.0


def on_real(_):
    global t_real
    t_real = time.time()


def tick():
    if time.time() - t_real > CALLADO_S:
        msg = JointState(name=JUNTAS, position=[0.0] * len(JUNTAS))
        msg.header.stamp = nodo.get_clock().now().to_msg()
        pub.publish(msg)


nodo.create_subscription(JointState, '/scara/joint_states', on_real, 10)
nodo.create_timer(0.2, tick)
try:
    rclpy.spin(nodo)
except KeyboardInterrupt:
    pass
