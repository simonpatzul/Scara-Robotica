# Modo Control de la página: el mando mueve la punta por el plano. No lo usa el modo Teleop, que corre scara_pc teleop.
#   Escucha  /scara/joint_states  (posición medida) y /scara/servo
#   Publica  /scara/target ([r1 rad, r2 rad, p mm]), /scara/servo (grados) y /scara/bomba
# Entra por una terminal virtual que abre scada_web.py:
#   "v vx vy vp" y Enter (de -1 a 1): un punto recorre su propia línea recta a esa velocidad y la punta lo sigue. Si el
#   robot se atrasa el punto lo espera, sin escaparse. "v 0 0 0", o dejar de mandarlo, lo detiene.
#   Z / X = servo - / + mientras llegue     B / N = prender / apagar la bomba
import math
import os
import select
import sys
import termios
import time
import tty
import rclpy
from rclpy.node import Node
from std_msgs.msg import Bool, Float64, Float64MultiArray
from sensor_msgs.msg import JointState
from scara_pc.ik import fk, ik
from scara_pc.p2p import ALCANCE, P_MAX, en_limites

TICK_S = 0.05
DT_MAX = 0.1        # un ciclo atrasado no debe dar un salto
VEL_XY = 180.0      # mm/s del punto con el stick a fondo
VEL_P = 12.0        # mm/s: más de lo que P da a 12 V, para que el objetivo se adelante y le llegue más voltaje
RAMPA_S = 0.4       # s de reposo a stick a fondo: un arranque de golpe pide un pico de corriente. Frenar no tiene rampa
LEAD_XY = 45.0      # mm: lo más que el punto se adelanta a la posición medida
LEAD_P = 5.0        # mm: con 3 mm ya satura en simulación; el resto es margen por la fricción real
PLANO_S = 0.25      # sin noticias de la página por este tiempo = stick soltado
STOP_S = 0.15       # al soltar, el objetivo sigue al robot este tiempo mientras frena y luego se fija
SALTO_MAX = 0.6     # rad: un objetivo que pida más que esto de golpe no se manda
SERVO_KEYS = {'z': -1, 'x': +1}
SERVO_S = 0.15      # sin repetir la tecla por este tiempo = soltada
SERVO_VEL = 22.0    # °/s, igual que secuencia_caja.py
SERVO_MAX = 174.0


class Control(Node):
    def __init__(self):
        super().__init__('control_mando')
        self.meas = None        # última posición medida
        self.target = None
        self.linea = None       # "v ..." a medio llegar
        self.plano = None       # [vx, vy, vp] mientras la punta se mueve
        self.punto = None       # [x, y, p] que sigue la punta; None = parte de donde esté
        self.vel = [0.0, 0.0, 0.0]   # velocidad ya con la rampa
        self.t_mover = None     # el timer no siempre llega a tiempo: se integra con el tiempo real
        self.codo_neg = False   # el codo se queda del lado donde arrancó
        self.t_plano = 0.0
        self.stop_until = None  # fin de la fase de frenado
        self.tecla = None       # z o x sostenida
        self.t_tecla = 0.0
        self.servo = 90.0       # último ángulo pedido: no hay medida del servo
        self.pub = self.create_publisher(Float64MultiArray, '/scara/target', 10)
        self.pub_servo = self.create_publisher(Float64, '/scara/servo', 10)
        self.pub_bomba = self.create_publisher(Bool, '/scara/bomba', 10)
        self.create_subscription(JointState, '/scara/joint_states', self.on_state, 10)
        self.create_subscription(Float64, '/scara/servo', self.on_servo, 10)
        self.create_timer(TICK_S, self.tick)

    def on_state(self, msg):
        self.meas = list(msg.position)

    def on_servo(self, msg):
        if msg.data >= 0:   # así arranca desde donde lo dejó la secuencia
            self.servo = msg.data

    def publish(self):
        self.pub.publish(Float64MultiArray(data=self.target))

    def leer(self, c):
        if self.linea is not None:
            if c in '\r\n':
                self.leer_plano(self.linea)
                self.linea = None
            else:
                self.linea += c
        elif c == 'v':
            self.linea = ''
        elif c in ('b', 'n'):
            self.pub_bomba.publish(Bool(data=c == 'b'))
        elif c in SERVO_KEYS:
            self.tecla, self.t_tecla = c, time.monotonic()

    def leer_plano(self, texto):
        try:
            v = [min(max(float(x), -1.0), 1.0) for x in texto.split()]
        except ValueError:
            return
        if len(v) != 3 or not all(math.isfinite(x) for x in v):
            return
        if any(v):
            self.plano, self.t_plano = v, time.monotonic()
            self.stop_until = None
        elif self.plano is not None:
            self.frenar()

    def frenar(self):
        self.plano = None
        self.punto = None
        self.vel = [0.0, 0.0, 0.0]
        self.t_mover = None

    def rampa(self, dt):
        """Sube de a poco hacia lo que pide el stick. Si pide menos o el sentido contrario, baja de una vez."""
        paso = dt / RAMPA_S
        for i, pedido in enumerate(self.plano):
            actual = self.vel[i]
            if pedido * actual <= 0:
                actual = 0.0
            if abs(pedido) <= abs(actual):
                actual = pedido
            else:
                actual += math.copysign(paso, pedido)
                if abs(actual) > abs(pedido):
                    actual = pedido
            self.vel[i] = actual
        self.stop_until = time.monotonic() + STOP_S

    def angulos(self, x, y, z):
        """[r1, r2, p] para ese punto, o None si no se puede o pide un salto."""
        r1, r2, _ = self.meas
        q = ik(x, y, z)
        if q is None:   # muy cerca de la base
            return None
        q1, q2, _ = q
        if self.codo_neg:   # ik da el codo con R1 positivo: seguir del lado donde arrancó, sin dar la vuelta
            q1, q2 = 2 * math.atan2(x, y) - q1, -q2
        if abs(q2 - r1) > SALTO_MAX or abs(-q1 - r2) > SALTO_MAX or not en_limites(q2, -q1):
            return None   # fuera de límites el firmware recortaría cada eje y la punta iría a otro lado
        return [q2, -q1, z]

    def mover(self):
        r1, r2, p = self.meas
        mx, my, _ = fk(-r2, r1, p)   # p2p manda (q2, -q1)
        if self.punto is None:
            self.punto = [mx, my, p]
            self.codo_neg = r1 < 0
        ahora = time.monotonic()
        dt = min(ahora - self.t_mover, DT_MAX) if self.t_mover is not None else TICK_S
        self.t_mover = ahora
        self.rampa(dt)
        vx, vy, vp = self.vel
        x, y, z = self.punto
        nx, ny = x + vx * VEL_XY * dt, y + vy * VEL_XY * dt
        d = math.hypot(nx, ny)
        if d > ALCANCE:
            nx, ny = nx * ALCANCE / d, ny * ALCANCE / d
        nz = min(max(z + vp * VEL_P * dt, 0.0), P_MAX)
        # El punto espera al robot en vez de rehacer la línea desde la medida: así la dirección no se tuerce
        if math.hypot(nx - mx, ny - my) > LEAD_XY:
            nx, ny = x, y
        if abs(nz - p) > LEAD_P:
            nz = z
        for candidato in ((nx, ny, nz), (x, y, nz), (nx, ny, z)):
            objetivo = self.angulos(*candidato)
            if objetivo is not None:
                self.punto = list(candidato)
                self.target = objetivo
                self.publish()
                return

    def tick(self):
        while select.select([sys.stdin], [], [], 0)[0]:
            datos = os.read(sys.stdin.fileno(), 1024).decode(errors='ignore').lower()
            if not datos:
                break
            for c in datos:
                self.leer(c)

        now = time.monotonic()
        if self.tecla is not None:
            if now - self.t_tecla > SERVO_S:
                self.tecla = None
            else:
                self.servo = min(max(self.servo + SERVO_KEYS[self.tecla] * SERVO_VEL * TICK_S, 0.0), SERVO_MAX)
                self.pub_servo.publish(Float64(data=self.servo))

        if self.meas is None:
            return
        if self.stop_until is not None:   # frenando: el objetivo sigue al robot, así no se devuelve
            self.target = list(self.meas)
            self.publish()
            if now > self.stop_until:
                self.stop_until = None
        elif self.plano is not None:
            if now - self.t_plano > PLANO_S:
                self.frenar()
            else:
                self.mover()


def main():
    rclpy.init()
    old = termios.tcgetattr(sys.stdin)
    tty.setcbreak(sys.stdin)
    try:
        rclpy.spin(Control())
    except KeyboardInterrupt:
        pass
    finally:
        termios.tcsetattr(sys.stdin, termios.TCSADRAIN, old)


if __name__ == '__main__':
    main()
