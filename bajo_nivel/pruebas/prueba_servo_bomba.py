# Prueba aparte: separa el servo y la bomba para ver cuál cuelga a la Pico. No mueve el brazo (no publica /scara/target).
# Necesita el bridge corriendo y el home hecho. El servo gira como en secuencia_caja.py: 0 -> 174 -> 0 a 22 °/s.
#   servo    solo gira el servo, la bomba queda apagada
#   bomba    solo prende y apaga la bomba, el servo queda sin señal
#   juntos   como la secuencia: bomba prendida mientras gira y se apaga justo al llegar a 0
# Mide cada cuánto llega /scara/joint_states (normal: 50 ms). Si pasa de GAP_MALO_S, la Pico dejó de responder y se detiene.
# Uso: source /opt/ros/jazzy/setup.bash && python3 bajo_nivel/pruebas/prueba_servo_bomba.py servo|bomba|juntos [veces]
import sys
import time
import rclpy
from std_msgs.msg import Bool, Float64
from sensor_msgs.msg import JointState

SERVO_MAX = 174.0    # el giro largo de las tarjetas 2 y 4
SERVO_VEL = 22.0     # °/s, igual que secuencia_caja.py
T_BOMBA = 1.0        # segundos prendida y apagada en el modo bomba
T_DESPUES = 2.0      # segundos mirando después de cada evento: la caída no fue instantánea
GAP_MALO_S = 0.5
ESPERA_DATOS_S = 5.0

modo = sys.argv[1] if len(sys.argv) > 1 else ''
if modo not in ('servo', 'bomba', 'juntos'):
    sys.exit('Uso: prueba_servo_bomba.py servo|bomba|juntos [veces]')
veces = int(sys.argv[2]) if len(sys.argv) > 2 else 5

rclpy.init()
nodo = rclpy.create_node('prueba_servo_bomba')
pub_bomba = nodo.create_publisher(Bool, '/scara/bomba', 10)
pub_servo = nodo.create_publisher(Float64, '/scara/servo', 10)
t_dato = None    # cuándo llegó el último /scara/joint_states
gap_max = 0.0


def on_js(msg):
    global t_dato, gap_max
    ahora = time.time()
    if t_dato is not None:
        gap_max = max(gap_max, ahora - t_dato)
    t_dato = ahora


nodo.create_subscription(JointState, '/scara/joint_states', on_js, 10)


class PicoCaida(Exception):
    pass


def mirar(seg):
    """Deja pasar seg segundos revisando que la Pico siga respondiendo."""
    t0 = time.time()
    while time.time() - t0 < seg:
        rclpy.spin_once(nodo, timeout_sec=0.02)
        hueco = time.time() - t_dato
        if hueco > GAP_MALO_S:
            raise PicoCaida(f'{hueco:.2f} s sin datos')


def bomba(on):
    pub_bomba.publish(Bool(data=on))
    print(f'  {time.strftime("%H:%M:%S")} bomba {"prendida" if on else "apagada"}')


def girar(desde, hasta):
    t0 = time.time()
    dur = abs(hasta - desde) / SERVO_VEL
    while True:
        s = min((time.time() - t0) / dur, 1.0)
        pub_servo.publish(Float64(data=desde + (hasta - desde) * s))
        mirar(0.05)
        if s >= 1.0:
            return


def repeticion():
    if modo == 'bomba':
        bomba(True)
        mirar(T_BOMBA)
        bomba(False)
        mirar(T_DESPUES)
        return
    if modo == 'juntos':
        bomba(True)
    girar(0.0, SERVO_MAX)
    girar(SERVO_MAX, 0.0)
    print(f'  {time.strftime("%H:%M:%S")} servo en 0')
    if modo == 'juntos':
        bomba(False)
    mirar(T_DESPUES)
    pub_servo.publish(Float64(data=-1.0))   # sin señal: el servo se suelta
    mirar(0.5)


t0 = time.time()
while t_dato is None or pub_bomba.get_subscription_count() == 0:
    rclpy.spin_once(nodo, timeout_sec=0.1)
    if time.time() - t0 > ESPERA_DATOS_S:
        sys.exit('No llegan datos: revisa que el bridge esté corriendo y que el home esté hecho')

print(f'Modo {modo}, {veces} veces')
n = 0
try:
    for n in range(1, veces + 1):
        gap_max = 0.0
        print(f'--- {n} de {veces} ---')
        repeticion()
        print(f'  bien, hueco máximo {gap_max * 1000:.0f} ms')
    print(f'RESULTADO {modo}: {veces} de {veces} bien')
except PicoCaida as e:
    print(f'RESULTADO {modo}: la Pico dejó de responder en la repetición {n} ({e})')
except KeyboardInterrupt:
    print('Detenida con Ctrl+C')
finally:
    pub_bomba.publish(Bool(data=False))
    pub_servo.publish(Float64(data=-1.0))
    time.sleep(0.2)
    nodo.destroy_node()
    rclpy.try_shutdown()
