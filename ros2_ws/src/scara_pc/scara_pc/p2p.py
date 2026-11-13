# Punto a punto: consigna (x, y) + p -> IK -> ángulos -> /scara/target.
# Se escribe "x y p" en la terminal (mm). El bridge sigue reenviando el último objetivo a la Pico.
#   Publica  /scara/target  ([q1 rad, q2 rad, p mm])
import math
import rclpy                                   # la librería de ROS 2 para Python
from rclpy.node import Node                    # la clase base de todo nodo
from std_msgs.msg import Float64MultiArray     # el tipo de mensaje que espera el bridge
from scara_pc.ik import ik, a1, a2             # tu cinemática inversa

ALCANCE = a1 + a2 - 0.5   # mm: un poco antes del brazo estirado, para que el redondeo no lo deje fuera de alcance
P_MAX = 50.0              # mm: el máximo mecánico, el mismo tope de P de axis_config.h
R1_LIM = (-1.81, 1.885)   # rad: los rangos de axis_config.h. Fuera de ellos el firmware recorta cada eje por su lado
R2_LIM = (-1.46, 1.5708)  # y el brazo termina en otro punto


def en_limites(r1, r2):
    return R1_LIM[0] <= r1 <= R1_LIM[1] and R2_LIM[0] <= r2 <= R2_LIM[1]


def alcanzable(x, y, p):
    """[r1, r2, p] de un punto que el brazo sí alcanza (primero con R1 positivo), o None."""
    q = ik(x, y, p)
    if q is None:
        return None
    q1, q2, z = q
    for a, b in ((q1, q2), (2 * math.atan2(x, y) - q1, -q2)):
        r2 = math.atan2(math.sin(-a), math.cos(-a))
        if en_limites(b, r2):
            return [b, r2, z]
    return None


class P2P(Node):                               # nuestro nodo ES un Node (hereda de él)
    def __init__(self):
        super().__init__('p2p')                # nombre del nodo (sale en `ros2 node list`)
        # publisher: (tipo de mensaje, nombre del topic, tamaño de la cola)
        self.pub = self.create_publisher(Float64MultiArray, '/scara/target', 10)

    def send(self, q1, q2, p):
        self.pub.publish(Float64MultiArray(data=[q1, q2, p]))
        self.get_logger().info(f'Objetivo enviado: q1={q1:.4f} rad, q2={q2:.4f} rad, p={p:.2f} mm')


def main():
    rclpy.init()                               # arranca la comunicación de ROS
    node = P2P()

    # Esperar a que el bridge esté escuchando; si no, el primer mensaje se pierde
    while node.pub.get_subscription_count() == 0:
        rclpy.spin_once(node, timeout_sec=0.1)

    print('Escribe "x y p" en mm (Ctrl+C para salir)')
    try:
        while True:
            line = input('> ')
            try:
                x, y, p = (float(v) for v in line.split())   # "100 150 0" -> 100.0, 150.0, 0.0
            except ValueError:
                print('  Formato: x y p')
                continue

            r = math.hypot(x, y)
            if r > ALCANCE:                    # más lejos de lo que llega: va al máximo en esa misma dirección
                x, y = x * ALCANCE / r, y * ALCANCE / r
                print(f'  Limitado al alcance: x={x:.1f} y={y:.1f}')
            if not 0 <= p <= P_MAX:
                p = min(max(p, 0.0), P_MAX)
                print(f'  Limitado: p={p:.1f}')

            q = alcanzable(x, y, p)
            if q is None:
                print('  Fuera de alcance')
                continue
            node.send(*q)
    except (KeyboardInterrupt, EOFError):
        pass

    node.destroy_node()
    rclpy.try_shutdown()                       # cierra ROS (sin error si Ctrl+C ya lo cerró)

