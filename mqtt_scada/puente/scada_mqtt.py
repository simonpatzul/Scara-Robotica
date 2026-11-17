# Puente ROS 2 -> MQTT para la página SCADA Estación 1. Solo lee: no manda nada al robot ni toca el bridge.
#   Escucha  /scara/joint_states  (lo publica el bridge)       -> scara/estado  (ejes R1, R2, P)
#   Escucha  /scara/target        (lo que manda p2p o la secuencia)  -> scara/estado  (campo obj de cada eje)
#   Escucha  /scara/bomba         (Bool)                        -> scara/est1 {"vacio": ...}
#   Escucha  /scara/est1          (String con JSON, opcional)   -> scara/est1 tal cual (tarjeta, fase, t_ciclo...)
# La página se conecta al mismo broker por WebSocket (wss://broker.hivemq.com:8884/mqtt, prefijo "scara").
# Solo manda datos reales: lo que el bridge no publica (corriente, voltaje, temperaturas, estado de cada eje) no se manda
# y la página no lo muestra. Si dejan de llegar /scara/joint_states deja de mandar scara/estado.
# Uso (con el bridge corriendo): pip install paho-mqtt   (o: sudo apt install python3-paho-mqtt)
#   source /opt/ros/jazzy/setup.bash && source alto_nivel/ros2_ws/install/setup.bash && python3 mqtt_scada/puente/scada_mqtt.py
import json
import time
import rclpy
from rclpy.node import Node
from std_msgs.msg import Bool, Float64MultiArray, String
from sensor_msgs.msg import JointState
import paho.mqtt.client as mqtt

BROKER = 'broker.hivemq.com'
PUERTO = 1883
PREFIJO = 'scara'          # el mismo que se pone en la página
PERIODO_S = 0.1            # 10 datos/s: a 2 no se alcanza a ver la diferencia entre controladores
SIN_DATOS_S = 1.0          # sin /scara/joint_states por este tiempo = el bridge o la Pico no responden
EJES = [('R1', 'joint_r1'), ('R2', 'joint_r2'), ('P', 'joint_p')]


class ScadaMqtt(Node):
    def __init__(self):
        super().__init__('scada_mqtt')
        self.pos = None            # última posición medida {joint: valor}
        self.t_pos = 0.0
        self.target = None         # último objetivo [r1 rad, r2 rad, p mm]; no se manda hasta que alguien publique uno
        self.bomba = False
        self.est1 = None           # último /scara/est1 y cuándo llegó: t_ciclo se sigue contando entre fases
        self.t_est1 = 0.0

        try:   # paho-mqtt 2.x
            self.mq = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f'scara-pc-{int(time.time())}')
        except AttributeError:   # paho-mqtt 1.x (el de apt en Ubuntu 24.04)
            self.mq = mqtt.Client(client_id=f'scara-pc-{int(time.time())}')
        self.mq.connect_async(BROKER, PUERTO, keepalive=30)
        self.mq.loop_start()   # reconecta solo si se cae el internet
        self.get_logger().info(f'Publicando en {BROKER}:{PUERTO} con prefijo "{PREFIJO}"')

        self.create_subscription(JointState, '/scara/joint_states', self.on_joints, 10)
        self.create_subscription(Float64MultiArray, '/scara/target', self.on_target, 10)
        self.create_subscription(Bool, '/scara/bomba', self.on_bomba, 10)
        self.create_subscription(String, '/scara/est1', self.on_est1, 10)
        self.create_timer(PERIODO_S, self.tick)

    def on_joints(self, msg):
        self.pos = dict(zip(msg.name, msg.position))
        self.t_pos = time.monotonic()

    def on_target(self, msg):
        if len(msg.data) == 3:
            self.target = list(msg.data)

    def on_bomba(self, msg):
        self.bomba = msg.data
        self.publicar('est1', dict(self.est1 or {}, vacio=self.bomba))   # sin borrar tarjeta y fase

    def on_est1(self, msg):
        try:
            st = json.loads(msg.data)
        except ValueError:
            return
        st.setdefault('vacio', self.bomba)
        self.est1, self.t_est1 = st, time.monotonic()
        self.publicar('est1', st)

    def publicar(self, sub, datos):
        self.mq.publish(f'{PREFIJO}/{sub}', json.dumps(datos), qos=0)

    def tick(self):
        ahora = time.monotonic()
        if self.est1 and self.est1.get('fase') != 'ESPERA_PILA' and 't_ciclo' in self.est1:
            st = dict(self.est1, vacio=self.bomba)
            st['t_ciclo'] = round(self.est1['t_ciclo'] + ahora - self.t_est1, 1)
            self.publicar('est1', st)
        if self.pos is None or ahora - self.t_pos >= SIN_DATOS_S:
            return
        # R1/R2 en rad, P en mm (como el bridge)
        ejes = [{'n': n, 'pos': self.pos.get(joint, 0.0)} for n, joint in EJES]
        if self.target:
            for eje, obj in zip(ejes, self.target):
                eje['obj'] = obj
        self.publicar('estado', {'ejes': ejes})


def main():
    rclpy.init()
    rclpy.spin(ScadaMqtt())


if __name__ == '__main__':
    main()
