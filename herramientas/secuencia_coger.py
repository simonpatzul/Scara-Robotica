# Secuencia coger y soltar la placa con la bomba (robot corriendo: scara_fw + bridge después del home).
# Coge en (x, y) = (-200, 160) mm y suelta con el brazo recto (articulaciones 0, 0).
# La placa mide 1.5 mm: se baja a P = 1.5 para tocarla y para dejarla. Entre puntos se viaja a P = 40.
# Cada paso espera a que /scara/joint_states llegue al objetivo; si tarda más de TIMEOUT_S se detiene.
# Uso: source /opt/ros/jazzy/setup.bash && source ros2_ws/install/setup.bash && python3 herramientas/secuencia_coger.py
import math
import time
import rclpy
from std_msgs.msg import Bool, Float64, Float64MultiArray
from sensor_msgs.msg import JointState
from scara_pc.ik import ik

P_ALTO = 40.0
P_PLACA = 1.5
TIMEOUT_S = 20.0
TOL_RAD = 0.03
TOL_MM = 1.0
SERVO_COGER = 0     # grados del servo al coger
SERVO_SOLTAR = 90   # se gira la placa mientras se lleva

q = ik(-200.0, 160.0, 0.0)
# ik da el codo hacia un lado (hombro 1.283 rad, pasa el tope físico de ~1.116). Se usa el codo al otro lado:
# q2' = -q2, q1' = 2 atan2(X, Y) - q1. Mismo orden que p2p: [R1 codo, R2 hombro]
COGER = [-q[1], -(2 * math.atan2(-200.0, 160.0) - q[0])]
SOLTAR = [0.0, 0.0]

rclpy.init()
nodo = rclpy.create_node('secuencia_coger')
pub_target = nodo.create_publisher(Float64MultiArray, '/scara/target', 10)
pub_bomba = nodo.create_publisher(Bool, '/scara/bomba', 10)
pub_servo = nodo.create_publisher(Float64, '/scara/servo', 10)
medido = None


def on_js(msg):
    global medido
    medido = list(msg.position)


nodo.create_subscription(JointState, '/scara/joint_states', on_js, 10)


def esperar(seg):
    t0 = time.time()
    while time.time() - t0 < seg:
        rclpy.spin_once(nodo, timeout_sec=0.05)


def ir(r1, r2, p, texto):
    print(f'{texto}: R1={r1:.3f} R2={r2:.3f} P={p:.1f}')
    pub_target.publish(Float64MultiArray(data=[r1, r2, p]))
    t0 = time.time()
    while time.time() - t0 < TIMEOUT_S:
        rclpy.spin_once(nodo, timeout_sec=0.05)
        if medido and abs(medido[0] - r1) < TOL_RAD and abs(medido[1] - r2) < TOL_RAD and abs(medido[2] - p) < TOL_MM:
            print(f'  llegó en {time.time() - t0:.1f} s')
            return
    raise RuntimeError(f'no llegó en {TIMEOUT_S} s, medido = {medido}')


def bomba(on):
    pub_bomba.publish(Bool(data=on))
    print('Bomba ' + ('prendida' if on else 'apagada'))


def servo(grados):
    pub_servo.publish(Float64(data=float(grados)))
    print(f'Servo a {grados} grados')
    esperar(1.0)


while pub_target.get_subscription_count() == 0 or pub_bomba.get_subscription_count() == 0:
    rclpy.spin_once(nodo, timeout_sec=0.1)   # esperar al bridge
esperar(0.5)

try:
    ir(0.0, 0.0, P_ALTO, 'Subir')
    servo(SERVO_COGER)
    ir(*COGER, P_ALTO, 'Ir arriba de (-200, 160)')
    ir(*COGER, P_PLACA, 'Bajar a la placa')
    bomba(True)
    esperar(2.0)
    ir(*COGER, P_ALTO, 'Subir con la placa')
    servo(SERVO_SOLTAR)
    ir(*SOLTAR, P_ALTO, 'Ir arriba de soltar (0, 0)')
    ir(*SOLTAR, P_PLACA, 'Bajar a soltar')
    bomba(False)
    esperar(1.0)
    ir(*SOLTAR, P_ALTO, 'Subir')
    servo(SERVO_COGER)
    print('Secuencia terminada')
except RuntimeError as e:
    print('DETENIDA:', e)
    if medido:   # quedarse donde está, para que el motor no siga empujando hacia un objetivo al que no llega
        pub_target.publish(Float64MultiArray(data=medido[:3]))
        esperar(0.5)
finally:
    nodo.destroy_node()
    rclpy.shutdown()
