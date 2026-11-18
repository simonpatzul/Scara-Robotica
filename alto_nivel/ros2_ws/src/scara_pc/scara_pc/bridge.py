# Puente PC <-> Pico. Es el ÚNICO nodo que toca el puerto serie.
#   Escucha  /scara/target        (Float64MultiArray: [r1 rad, r2 rad, p mm])
#   Publica  /scara/joint_states  (JointState: posiciones medidas, mismas unidades)
#   Escucha  /scara/bomba         (Bool: true = prender)       -> manda "B1" / "B0"
#   Escucha  /scara/servo         (Float64: grados 0..180)     -> manda "S<grados>" (negativo: "S-1", apaga la señal)
# Manda el último objetivo a la Pico a tasa fija, así su watchdog nunca se dispara
# mientras este nodo esté vivo. Si el nodo muere o se desconecta el USB, la Pico se queda quieta
# y apaga la bomba.
# Si el USB se cae (ruido) la Pico sigue corriendo con su home hecho: el nodo vuelve a abrir el
# puerto (/dev/ttyACM*) y lleva el brazo a la posición segura, sin repetir el home.
# Si la Pico se reinició perdió el home y sus posiciones no valen: no se le manda nada hasta que se
# reinicie este nodo con Home. Solo la línea HOME arranca el home de la Pico.
# Comandos de Grafana (MQTT -> FC41 -> STM32 -> I2C -> Pico), que la Pico pasa por el USB:
#   "MV r1,r2,p"  ir a esas coordenadas (rad, rad, mm). Se ignora mientras corre la secuencia
#   "SEQ"         correr la secuencia de la caja (alto_nivel/secuencias/secuencia_caja.py, parámetro 'secuencia')
#   "STOP"        parar la secuencia: se queda donde está y apaga la bomba
# Estado 'A' de un eje = el STM32 le apagó el EN: su objetivo se iguala a la posición, así al prenderlo no salta.
# Cada 0.25 s se le dice a la Pico si la secuencia corre ("Q1" / "Q0"), para que el STM32 lo publique.
import glob
import math
import os
import signal
import subprocess
import sys
import time
import serial
import rclpy
from rclpy.node import Node
from std_msgs.msg import Bool, Float64, Float64MultiArray
from sensor_msgs.msg import JointState

JOINTS = ['joint_r1', 'joint_r2', 'joint_p']
SIN_RESPUESTA_S = 1.0   # sin respuesta de la Pico por este tiempo = USB caído, se reconecta
ESPERA_REINICIO_S = 4.0   # tras reconectar solo se escucha: una Pico reiniciada pide HOME cada segundo desde los 3 s de arrancar
AVISO_HOME = 'Escribe HOME'
P_HOME = 51.0             # mm: donde queda P al terminar el home (final de arriba, GP20; axis_config.h)
SEGURA_R1, SEGURA_R2, SEGURA_P = -1.83, -1.49, 30.0   # ESTACIONAR y P_CAJA de secuencia_caja.py
T_TRAMO_S = 2.0           # cada tramo hacia la posición segura, como T_SUAVE
SILENCIO_S = 1.0          # ya en la posición segura, /scara/target se vuelve a oír cuando deje de llegar este tiempo


class Bridge(Node):
    def __init__(self):
        super().__init__('bridge')
        port = self.declare_parameter('port', '/dev/ttyACM0').value
        rate = self.declare_parameter('rate_hz', 20.0).value
        self.ruta_secuencia = self.declare_parameter(
            'secuencia', os.path.expanduser('~/scara_ws/SCARA_Robotica/alto_nivel/secuencias/secuencia_caja.py')).value
        self.secuencia = None              # proceso de la secuencia, si se lanzó desde Grafana

        self.ser = serial.Serial(port, 115200, timeout=1.0)
        self.home()

        # Después del home R1 y R2 quedan en 0 y P arriba, en su final (51 mm): mandarle 0 lo bajaría hasta el fondo
        self.target = [0.0, 0.0, P_HOME]
        self.ult_resp = time.monotonic()   # última respuesta válida de la Pico
        self.medido = None                 # última posición que contestó la Pico
        self.t_reconexion = None           # cuándo se reabrió el puerto, mientras se escucha sin mandar nada
        self.buf = b''                     # lo que dijo la Pico tras reconectar
        self.sin_home = False              # la Pico se reinició
        self.a_segura = False              # el USB se cayó: se ignoran objetivos hasta terminar de ir a la posición segura
        self.ruta = []                     # tramos que faltan hacia la posición segura
        self.t_tramo = 0.0
        self.desde = None
        self.ult_target = 0.0              # último mensaje de /scara/target
        self.bomba = False                 # estado pedido de la bomba; se reenvía seguido (ver tick)
        self.bomba_pico = None             # último BOMBA= que confirmó la Pico
        self.n_tick = 0
        self.pub = self.create_publisher(JointState, '/scara/joint_states', 10)
        self.create_subscription(Float64MultiArray, '/scara/target', self.on_target, 10)
        self.create_subscription(Bool, '/scara/bomba', self.on_bomba, 10)
        self.create_subscription(Float64, '/scara/servo', self.on_servo, 10)
        self.create_timer(1.0 / rate, self.tick)

    def home(self):
        # La Pico espera la línea HOME para hacer el home y al terminar imprime LISTO
        self.get_logger().info('Haciendo home...')
        self.ser.write(b'HOME\n')
        while True:
            line = self.ser.readline().decode(errors='ignore').strip()
            if line:
                self.get_logger().info(f'Pico: {line}')
            if line.startswith(AVISO_HOME):   # todavía no le llegó: pudo arrancar después de abrir el puerto
                self.ser.write(b'HOME\n')
            if line == 'LISTO':
                break
        self.ser.timeout = 0   # de aquí en adelante las lecturas no bloquean

    def on_target(self, msg):
        self.ult_target = time.monotonic()
        if len(msg.data) == 3 and not self.a_segura:
            self.target = list(msg.data)

    def on_bomba(self, msg):
        if msg.data and self.a_segura:
            return
        self.bomba = msg.data
        self.send(b'B1\n' if msg.data else b'B0\n')
        self.get_logger().info(f'Bomba {"prendida" if msg.data else "apagada"}')

    def on_servo(self, msg):
        if msg.data < 0:   # negativo = apagar la señal: el servo se suelta y no consume corriente
            self.send(b'S-1\n')
            return
        grados = int(round(min(msg.data, 180.0)))
        self.send(f'S{grados}\n'.encode())

    def corre_secuencia(self):
        return self.secuencia is not None and self.secuencia.poll() is None

    def mover(self, texto):
        """MV de Grafana: el objetivo pasa a esas coordenadas (la Pico las recorta a los límites de cada eje)."""
        try:
            v = [float(x) for x in texto.split(',')]
        except ValueError:
            v = []
        if len(v) != 3 or not all(math.isfinite(x) for x in v):
            self.get_logger().warning(f'MV no válido: {texto}')
        elif self.corre_secuencia() or self.a_segura or self.ruta:
            self.get_logger().warning(f'MV {texto} ignorado: está corriendo la secuencia o yendo a la posición segura')
        else:
            self.get_logger().info(f'MV desde Grafana: {texto}')
            self.target = v

    def iniciar_secuencia(self):
        if self.corre_secuencia():
            self.get_logger().info('SEQ: la secuencia ya está corriendo')
            return
        if self.a_segura or self.ruta:
            self.get_logger().warning('SEQ ignorado: el brazo va a la posición segura')
            return
        self.get_logger().info(f'SEQ desde Grafana: {self.ruta_secuencia}')
        self.secuencia = subprocess.Popen([sys.executable, self.ruta_secuencia])

    def parar_secuencia(self):
        if self.corre_secuencia():
            self.secuencia.send_signal(signal.SIGINT)
            self.get_logger().warning('STOP desde Grafana: secuencia detenida')
        self.bomba = False
        self.send(b'B0\n')
        if self.medido:
            self.target = list(self.medido)

    def send(self, data):
        if self.ser is None or self.t_reconexion is not None or self.sin_home:
            return
        try:
            self.ser.write(data)
        except (serial.SerialException, OSError) as e:
            self.desconectado(e)

    def desconectado(self, e):
        self.get_logger().error(f'Se cayó el USB ({e}); reconectando...')
        try:
            self.ser.close()
        except Exception:
            pass
        self.ser = None
        self.a_segura = True
        self.ruta = []

    def reconectar(self):
        for port in sorted(glob.glob('/dev/ttyACM*')):
            try:
                self.ser = serial.Serial(port, 115200, timeout=0)
            except (serial.SerialException, OSError):
                continue
            self.ult_resp = self.t_reconexion = time.monotonic()
            self.buf = b''
            self.get_logger().info(f'Reconectado en {port}')
            return

    def escuchar(self):
        """Tras reconectar: decide, sin mandar nada, si la Pico se reinició o sigue con su home."""
        try:
            self.buf += self.ser.read(self.ser.in_waiting)
        except (serial.SerialException, OSError) as e:
            self.t_reconexion = None
            self.desconectado(e)
            return
        if AVISO_HOME.encode() in self.buf:
            self.reinicio()
        elif time.monotonic() - self.t_reconexion > ESPERA_REINICIO_S:
            self.t_reconexion = None
            self.ir_a_segura()

    def reinicio(self):
        self.t_reconexion = None
        self.sin_home = True
        self.get_logger().error('La Pico se reinició y perdió el home: no se le manda nada. Haz Home')

    def ir_a_segura(self):
        """Primero sube P, luego el hombro y al final el codo, como al estacionar en secuencia_caja.py."""
        r1, r2, p = self.medido or self.target
        p = max(p, SEGURA_P)
        self.get_logger().warning('USB de vuelta: la bomba se apaga y el brazo va a la posición segura')
        self.bomba = False
        self.send(b'B0\n')
        self.desde = list(self.medido or self.target)
        self.ruta = [[r1, r2, p], [r1, SEGURA_R2, p], [SEGURA_R1, SEGURA_R2, p]]
        self.t_tramo = time.monotonic()

    def avanzar_ruta(self):
        ahora = time.monotonic()
        s = min((ahora - self.t_tramo) / T_TRAMO_S, 1.0)
        k = (1 - math.cos(math.pi * s)) / 2
        self.target = [a + (b - a) * k for a, b in zip(self.desde, self.ruta[0])]
        if s >= 1.0:
            self.desde = self.ruta.pop(0)
            self.t_tramo = ahora

    def tick(self):
        if self.ser is None:
            self.reconectar()
            if self.ser is None:
                return
        if self.sin_home:
            return
        if self.t_reconexion is not None:
            self.escuchar()
            return
        if self.ruta:
            self.avanzar_ruta()
        elif self.a_segura and time.monotonic() - self.ult_target > SILENCIO_S:
            self.a_segura = False
        r1, r2, p = self.target
        self.send(f'{r1:.4f},{r2:.4f},{p:.3f}\n'.encode())
        # Si el watchdog de la Pico se dispara (más de 200 ms sin órdenes) apaga la bomba y no la vuelve a prender:
        # por eso el estado de la bomba se reenvía cada 5 ciclos (0.25 s a 20 Hz).
        self.n_tick += 1
        if self.n_tick % 5 == 0:
            self.send(b'B1\n' if self.bomba else b'B0\n')
            self.send(b'Q1\n' if self.corre_secuencia() else b'Q0\n')

        # Leer todas las respuestas que hayan llegado y quedarse con la última válida
        last = None
        try:
            while self.ser is not None and self.ser.in_waiting:
                line = self.ser.readline().decode(errors='ignore').strip()
                parts = line.split(',')
                if len(parts) == 4:
                    last = parts
                elif line.startswith('BOMBA='):   # la Pico confirma la salida de la válvula (GP22); solo se muestra si cambió
                    if line != self.bomba_pico:
                        self.get_logger().info(f'Pico: {line}')
                        self.bomba_pico = line
                elif line.startswith(AVISO_HOME):   # la Pico sí se reinició: perdió el home
                    self.reinicio()
                elif line.startswith('MV '):
                    self.mover(line[3:])
                elif line == 'SEQ':
                    self.iniciar_secuencia()
                elif line == 'STOP':
                    self.parar_secuencia()
        except (serial.SerialException, OSError) as e:
            self.desconectado(e)
        if last is None:
            # Si el USB se cayó a veces no hay error: el puerto viejo solo deja de contestar
            if self.ser is not None and time.monotonic() - self.ult_resp > SIN_RESPUESTA_S:
                self.desconectado(f'sin respuesta en {SIN_RESPUESTA_S} s')
            return
        self.ult_resp = time.monotonic()
        self.medido = [float(v) for v in last[:3]]
        for k, e in enumerate(last[3][:3]):
            if e == 'A':   # EN apagado por el STM32: el objetivo sigue al eje, así al prenderlo no salta
                self.target[k] = self.medido[k]

        msg = JointState()
        msg.header.stamp = self.get_clock().now().to_msg()
        msg.name = JOINTS
        msg.position = [float(v) for v in last[:3]]
        self.pub.publish(msg)
        if 'F' in last[3]:
            self.get_logger().error(f'La Pico reporta falla: {last[3]}')


def main():
    rclpy.init()
    rclpy.spin(Bridge())


if __name__ == '__main__':
    main()
