# Comandos del SCADA (Grafana) por MQTT -> robot. Corre en el PC junto al bridge.
#   Escucha  scara/cmd  (MQTT, texto sin comillas, lo manda server.js al apretar un botón de Grafana):
#     mover,x,y,p   mm  -> IK -> /scara/target   (solo si el robot está prendido)
#     bomba,1 / bomba,0  -> /scara/bomba
#     servo,grados       -> /scara/servo
#     parar              -> el STM32 apaga los EN; aquí el objetivo se queda en la posición medida
#     prender            -> el STM32 prende los EN; el objetivo sigue en la posición medida (no salta)
#   Publica  scara/respuesta (MQTT, texto): qué se hizo o por qué se rechazó
#   Publica  scara/pos       (MQTT, JSON cada 0.5 s): r1, r2 (rad), p, x, y (mm), para las gráficas de Grafana
#   Escucha  /scara/joint_states (lo publica el bridge)
# El broker es el Mosquitto del PC del SCADA, el mismo al que se conecta el STM32 (DEFAULT_IP de FC41.c).
# Uso (con el bridge corriendo y el Home hecho):
#   ros2 run scara_pc mqtt_cmd --ros-args -p broker:=172.19.133.20
import math
import time
import rclpy
from rclpy.node import Node
from std_msgs.msg import Bool, Float64, Float64MultiArray
from sensor_msgs.msg import JointState
import paho.mqtt.client as mqtt
from scara_pc.ik import fk
from scara_pc.p2p import alcanzable, ALCANCE, P_MAX

JOINTS = ['joint_r1', 'joint_r2', 'joint_p']
SIN_DATOS_S = 1.0     # sin /scara/joint_states por este tiempo = el bridge o la Pico no responden
PERIODO_POS_S = 0.5


class MqttCmd(Node):
    def __init__(self):
        super().__init__('mqtt_cmd')
        broker = self.declare_parameter('broker', '172.19.133.20').value
        self.medido = None          # [r1, r2, p] de la Pico
        self.t_medido = 0.0
        self.parado = False

        self.pub_target = self.create_publisher(Float64MultiArray, '/scara/target', 10)
        self.pub_bomba = self.create_publisher(Bool, '/scara/bomba', 10)
        self.pub_servo = self.create_publisher(Float64, '/scara/servo', 10)
        self.create_subscription(JointState, '/scara/joint_states', self.on_joints, 10)

        try:   # paho-mqtt 2.x
            self.mq = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f'scara-cmd-{int(time.time())}')
        except AttributeError:   # paho-mqtt 1.x
            self.mq = mqtt.Client(client_id=f'scara-cmd-{int(time.time())}')
        self.mq.on_connect = lambda c, u, f, rc, p=None: c.subscribe('scara/cmd', qos=1)
        self.mq.on_message = self.on_mqtt
        self.mq.connect_async(broker, 1883, keepalive=30)
        self.mq.loop_start()   # reconecta solo
        self.get_logger().info(f'Escuchando scara/cmd en {broker}:1883')
        self.create_timer(PERIODO_POS_S, self.publicar_pos)

    def on_joints(self, msg):
        pos = dict(zip(msg.name, msg.position))
        if all(j in pos for j in JOINTS):
            self.medido = [pos[j] for j in JOINTS]
            self.t_medido = time.monotonic()

    def responder(self, texto):
        self.get_logger().info(texto)
        self.mq.publish('scara/respuesta', f'pc: {texto}', qos=0)

    def hay_robot(self):
        return self.medido is not None and time.monotonic() - self.t_medido < SIN_DATOS_S

    def quedarse_quieto(self):
        """Objetivo = donde está ahora: el control no acumula error con los EN apagados ni salta al prender."""
        if self.hay_robot():
            self.pub_target.publish(Float64MultiArray(data=list(self.medido)))

    # Llega en el hilo de paho: solo se publica en ROS, que es seguro desde otro hilo
    def on_mqtt(self, client, userdata, msg):
        texto = msg.payload.decode(errors='ignore').strip()
        partes = [s.strip() for s in texto.split(',')]
        cmd = partes[0].lower()
        try:
            if cmd == 'parar':
                self.parado = True
                self.quedarse_quieto()
                self.responder('parado: el objetivo se quedó en la posición actual')
            elif cmd == 'prender':
                self.parado = False
                self.quedarse_quieto()
                self.responder('prendido')
            elif cmd == 'mover':
                self.mover(*(float(v) for v in partes[1:4]))
            elif cmd == 'bomba':
                prender = partes[1] == '1'
                self.pub_bomba.publish(Bool(data=prender))
                self.responder(f'bomba {"prendida" if prender else "apagada"}')
            elif cmd == 'servo':
                g = min(max(float(partes[1]), 0.0), 180.0)
                self.pub_servo.publish(Float64(data=g))
                self.responder(f'servo a {g:.0f}°')
            else:
                self.responder(f'comando desconocido: {texto}')
        except (IndexError, ValueError, TypeError):
            self.responder(f'formato no válido: {texto}')

    def mover(self, x, y, p):
        if self.parado:
            self.responder('rechazado: el robot está parado, aprieta Prender')
            return
        if not self.hay_robot():
            self.responder('rechazado: no llegan posiciones del bridge (¿Home hecho?)')
            return
        if math.hypot(x, y) > ALCANCE:
            self.responder(f'rechazado: ({x:.0f}, {y:.0f}) está fuera del alcance ({ALCANCE:.0f} mm)')
            return
        if not 0.0 <= p <= P_MAX:
            self.responder(f'rechazado: p debe estar entre 0 y {P_MAX:.0f} mm')
            return
        q = alcanzable(x, y, p)
        if q is None:
            self.responder(f'rechazado: ({x:.0f}, {y:.0f}) fuera de los límites de los ejes')
            return
        self.pub_target.publish(Float64MultiArray(data=q))
        self.responder(f'moviendo a x={x:.1f} y={y:.1f} p={p:.1f}')

    def publicar_pos(self):
        if not self.hay_robot():
            return
        r1, r2, p = self.medido
        x, y, _ = fk(-r2, r1, p)   # mismo cambio de ángulos que alcanzable() en p2p.py
        self.mq.publish('scara/pos', '{"r1":%.4f,"r2":%.4f,"p":%.2f,"x":%.1f,"y":%.1f,"parado":%s}'
                        % (r1, r2, p, x, y, 'true' if self.parado else 'false'), qos=0)


def main():
    rclpy.init()
    node = MqttCmd()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    node.mq.loop_stop()
    node.destroy_node()
    rclpy.try_shutdown()
