# Saca las tarjetas apiladas de una caja y las deja en línea recta (robot corriendo: scara_fw + bridge después del home).
# Caja en (-200, 160): piso de 3 mm, placas de 1.5 mm. La de arriba se coge a altura = A0 + 1.5 * (N - i).
# Se sueltan en el suelo (P = 1.5) en un cuadro de 2 x 2 (BANDEJA), fila por fila.
# Pila en cruz: la tarjeta 1 (arriba) tiene H1 abajo-izquierda, como debe quedar; la 2 está girada GIRO_PILA, la 3 igual
# que la 1, etc. La ventosa gira con el brazo, así que el servo (horario al subir de grados, igual que el brazo) gira:
#   corrección = -giro_de_la_tarjeta_en_la_pila - (ángulo_brazo_soltar - ángulo_brazo_coger)   en (-180, 180]
# H1 hace que la tarjeta no sea simétrica: no se toma módulo 180. Para que quepa en 0..180 del servo:
#   corrección >= 0 -> coger en 0 y soltar en la corrección;  corrección < 0 -> coger en -corrección y soltar en 0
# Rápida (20–50 s): viaja bajo (P_CAJA, P_APROX), el servo gira mientras el brazo viaja y P baja en el mismo movimiento.
# Uso: source /opt/ros/jazzy/setup.bash && source alto_nivel/ros2_ws/install/setup.bash && python3 alto_nivel/secuencias/secuencia_caja.py
import json
import math
import time
import rclpy
from std_msgs.msg import Bool, Float64, Float64MultiArray, String
from sensor_msgs.msg import JointState
from scara_pc.ik import ik

N_PLACAS = 4
A0 = 3.0             # piso de la caja (mm)
ESPESOR = 1.5        # cada placa (mm)
CAJA = (-200.0, 145.0)   # punto de la caja calibrado en el robot (2026-11-03)
# Cuadro de 2 x 2, centro en (25, 225). Tarjetas de 38 x 38 mm a 50 mm: quedan 12 mm entre una y otra
BANDEJA = [(1.5, 195.5), (50.5, 195.5), (1.5, 241.5), (50.5, 241.5)]
P_CAJA = 30.0        # altura para entrar y salir de la caja (piso 3 + 4 tarjetas = 9 mm; subir si las paredes son más altas)
P_APROX = 25.0       # altura para llegar y salir del punto de soltar en el suelo
BAJAR_EXTRA = 1.5     # al coger, baja esto más que la altura calculada de la tarjeta (si toca antes, se detiene ahí)
P_SUELO = 6.0        # suelta la tarjeta a 6 mm, sobre la bandeja (no baja hasta el piso del hueco)
SUBIR_TARJETA = 15.0  # con la tarjeta: sube esto sobre la altura donde la cogió y sale hacia el punto de soltar
P_SALIR_SUELO = 12.0  # después de soltar: sale hacia la caja cuando P pasa esta altura (en el suelo solo hay tarjetas de 1.5 mm)
P_PASO = 25.0        # al subir, el brazo arranca hacia el siguiente punto cuando P pasa esta altura y sigue subiendo mientras viaja
GIRO_PILA = 90       # la tarjeta 2 está girada 90° en horario respecto a la 1 (-90 si es antihorario)
SIGNO_SERVO = 1      # cambiar a -1 si la tarjeta queda girada al revés
TIMEOUT_S = 20.0
TOL_RAD = 0.03       # al bajar a coger o soltar
TOL_MM = 1.0
TOL_RAD_VIAJE = 0.06  # en los puntos de paso no hace falta esperar a que se asiente
TOL_MM_VIAJE = 2.0
LIM_R1 = (-1.72, 1.95)
LIM_R2 = (-1.40, 1.5708)   # hombro hasta 90°
# Al final: (R1 codo, R2 hombro) un poco más allá de sus finales de carrera (en -1.81 y -1.467), para que los toquen.
# El firmware limita el objetivo a -1.83 / -1.49 (axis_config.h).
ESTACIONAR = (-1.83, -1.49)
SERVO_VEL = 45.0     # °/s: el servo gira poco a poco (de golpe pide mucha corriente y reinicia la Pico). Era 22 con los 5 V a 6.4 V
T_SUAVE = 2.0       # segundos de cada movimiento suave al estacionar


def articulaciones(x, y):
    """(x, y) -> (R1 codo, R2 hombro, ángulo del brazo). Elige el lado del codo que cabe en los límites."""
    q = ik(x, y, 0.0)
    if q is None:
        raise ValueError(f'({x}, {y}) fuera de alcance')
    a = math.atan2(x, y)
    for q1, q2 in [(q[0], q[1]), (2 * a - q[0], -q[1])]:   # los dos lados del codo
        r1, r2 = q2, -q1                                   # mismo orden que p2p
        if LIM_R1[0] <= r1 <= LIM_R1[1] and LIM_R2[0] <= r2 <= LIM_R2[1]:
            return r1, r2, q1 + q2
    raise ValueError(f'({x}, {y}) no cabe en los límites de las articulaciones')


rclpy.init()
nodo = rclpy.create_node('secuencia_caja')
pub_target = nodo.create_publisher(Float64MultiArray, '/scara/target', 10)
pub_bomba = nodo.create_publisher(Bool, '/scara/bomba', 10)
pub_servo = nodo.create_publisher(Float64, '/scara/servo', 10)
pub_est1 = nodo.create_publisher(String, '/scara/est1', 10)   # solo para la página SCADA (mqtt_scada/puente/scada_mqtt.py)
medido = None


def on_js(msg):
    global medido
    medido = list(msg.position)


nodo.create_subscription(JointState, '/scara/joint_states', on_js, 10)


servo_act = 0.0      # ángulo que ya se le mandó al servo (la secuencia anterior lo dejó en 0)
servo_obj = 0.0
t_servo = time.time()
t_llego = None       # cuándo llegó el servo a servo_obj (para apagar su señal después)
SERVO_SOLTAR_S = 0.3  # tiempo para que se asiente antes de apagar la señal


def servo_paso():
    """Acerca el servo a servo_obj a SERVO_VEL. Se llama en cada vuelta de esperar() e ir()."""
    global servo_act, t_servo, t_llego
    ahora = time.time()
    if ahora - t_servo < 0.05:
        return
    paso = SERVO_VEL * min(ahora - t_servo, 0.1)   # sin saltos si pasó mucho tiempo sin llamarla
    t_servo = ahora
    if servo_act == servo_obj:
        # Ya llegó: al rato se apaga la señal (S-1) para que no pida corriente sosteniéndose quieto
        if t_llego is not None and ahora - t_llego > SERVO_SOLTAR_S:
            pub_servo.publish(Float64(data=-1.0))
            t_llego = None
        return
    servo_act = min(servo_act + paso, servo_obj) if servo_obj > servo_act else max(servo_act - paso, servo_obj)
    pub_servo.publish(Float64(data=float(servo_act)))
    if servo_act == servo_obj:
        t_llego = ahora


def esperar(seg):
    t0 = time.time()
    while time.time() - t0 < seg:
        rclpy.spin_once(nodo, timeout_sec=0.05)
        servo_paso()


def ir(r1, r2, p, texto, viaje=False, toca=False, hasta=None, ventana=3.0):
    """viaje: tolerancia amplia (punto de paso). toca: también vale si P se detuvo cerca del objetivo
    (la ventosa apoyada en la tarjeta o la tarjeta en el suelo no deja bajar el último milímetro).
    hasta: al subir, vuelve en cuanto P pasa esa altura (P sigue subiendo hacia p).
    ventana: con toca, qué tan lejos del objetivo puede detenerse P y contar como apoyado (mm)."""
    tol_rad, tol_mm = (TOL_RAD_VIAJE, TOL_MM_VIAJE) if viaje else (TOL_RAD, TOL_MM)
    print(f'{texto}: R1={r1:.3f} R2={r2:.3f} P={p:.1f}')
    t0 = time.time()
    t_envio = 0.0
    historia = []   # (t, P) para ver si P se detuvo
    while time.time() - t0 < TIMEOUT_S:
        if time.time() - t_envio > 0.5:   # se reenvía por si un mensaje se pierde
            pub_target.publish(Float64MultiArray(data=[r1, r2, p]))
            t_envio = time.time()
        rclpy.spin_once(nodo, timeout_sec=0.05)
        servo_paso()
        if not medido:
            continue
        ok_r = abs(medido[0] - r1) < tol_rad and abs(medido[1] - r2) < tol_rad
        if ok_r and (abs(medido[2] - p) < tol_mm or (hasta is not None and medido[2] >= hasta)):
            print(f'  llegó en {time.time() - t0:.1f} s (P={medido[2]:.1f})')
            return
        ahora = time.time()
        historia.append((ahora, medido[2]))
        historia = [(t, v) for t, v in historia if ahora - t < 0.4]
        if (toca and ok_r and ahora - t0 > 0.5 and abs(medido[2] - p) < ventana
                and max(v for _, v in historia) - min(v for _, v in historia) < 0.1):
            print(f'  apoyó en P={medido[2]:.2f} en {time.time() - t0:.1f} s')
            return
    raise RuntimeError(f'no llegó en {TIMEOUT_S} s, medido = {medido}')


def bomba(on):
    pub_bomba.publish(Bool(data=on))
    print('Bomba ' + ('prendida' if on else 'apagada'))


def est1(fase, tarjeta=None, colocadas=0, **extra):
    """Avisa a la página SCADA en qué fase va la secuencia. No cambia nada del movimiento."""
    st = {'fase': fase, 'tarjeta': tarjeta, 'colocadas': colocadas, 't_ciclo': round(time.time() - T_INICIO, 1)}
    st.update(extra)
    pub_est1.publish(String(data=json.dumps(st)))


def ir_suave(r1, r2, p, texto, dur=T_SUAVE):
    """Lleva el objetivo de donde está a (r1, r2, p) poco a poco (perfil coseno), sin saltos."""
    print(f'{texto}: R1={r1:.3f} R2={r2:.3f} P={p:.1f}')
    a = list(medido[:3])
    t0 = time.time()
    while True:
        s = min((time.time() - t0) / dur, 1.0)
        k = (1 - math.cos(math.pi * s)) / 2
        pub_target.publish(Float64MultiArray(data=[a[0] + (r1 - a[0]) * k, a[1] + (r2 - a[1]) * k,
                                                   a[2] + (p - a[2]) * k]))
        esperar(0.05)
        if s >= 1.0:
            break
    # Que termine de llegar (hasta 3 s): el eje va detrás del objetivo y al final empuja contra el límite
    t1 = time.time()
    while time.time() - t1 < 3.0 and not (abs(medido[0] - r1) < 0.03 and abs(medido[1] - r2) < 0.03):
        pub_target.publish(Float64MultiArray(data=[r1, r2, p]))
        esperar(0.1)
    print(f'  quedó en R1={medido[0]:.3f} R2={medido[1]:.3f} P={medido[2]:.1f}')


def servo(grados):
    global servo_obj
    servo_obj = float(grados)   # no se espera: gira poco a poco mientras el brazo viaja (servo_paso)
    print(f'Servo hacia {grados:.1f} grados')


def esperar_servo():
    t0 = time.time()
    limite = abs(servo_obj - servo_act) / SERVO_VEL + 1.0   # lo que le falta girar a SERVO_VEL, con margen
    while servo_act != servo_obj and time.time() - t0 < limite:
        esperar(0.05)


# Calcular todo antes de mover nada: si un punto no se alcanza, no arranca
c1, c2, ang_coger = articulaciones(*CAJA)
plan = []
for i in range(N_PLACAS):
    altura = A0 + ESPESOR * (N_PLACAS - i)
    x, y = BANDEJA[i]
    s1, s2, ang_soltar = articulaciones(x, y)
    giro = GIRO_PILA if i % 2 else 0   # i = 0 es la tarjeta de arriba
    corr = SIGNO_SERVO * (-giro - math.degrees(ang_soltar - ang_coger))
    corr = (corr + 180) % 360 - 180   # a [-180, 180)
    serv_coger, serv_soltar = (0.0, corr) if corr >= 0 else (-corr, 0.0)
    plan.append((altura, x, y, s1, s2, serv_coger, serv_soltar))
    print(f'Tarjeta {i + 1}: coger a {altura:.1f} mm con servo {serv_coger:.1f}°, '
          f'soltar en ({x:.0f}, {y:.0f}) con servo {serv_soltar:.1f}°')

while pub_target.get_subscription_count() == 0 or pub_bomba.get_subscription_count() == 0:
    rclpy.spin_once(nodo, timeout_sec=0.1)   # esperar al bridge
esperar(0.5)

while medido is None:
    rclpy.spin_once(nodo, timeout_sec=0.1)

T_INICIO = time.time()
try:
    ir(medido[0], medido[1], P_CAJA, 'Subir donde está', viaje=True)
    for i, (altura, x, y, s1, s2, serv_coger, serv_soltar) in enumerate(plan):
        print(f'--- Tarjeta {i + 1} de {N_PLACAS} ({time.time() - T_INICIO:.1f} s) ---')
        servo(serv_coger)
        est1('PILA', i + 1, i)
        ir(c1, c2, P_CAJA, 'Ir arriba de la caja', viaje=True)
        esperar_servo()   # con el servo lento, que termine de girar antes de bajar: si no, coge la tarjeta torcida
        est1('BAJAR_PILA', i + 1, i)
        ir(c1, c2, altura - BAJAR_EXTRA, 'Bajar a la tarjeta', toca=True)   # un poco más abajo para que la ventosa apriete
        est1('VACIO', i + 1, i)
        bomba(True)
        esperar(1.0)   # que el vacío agarre bien la tarjeta antes de subir
        est1('SUBIR_PILA', i + 1, i)
        ir(c1, c2, P_CAJA, 'Subir con la tarjeta', viaje=True, hasta=altura + SUBIR_TARJETA)
        servo(serv_soltar)
        est1('BANDEJA', i + 1, i)
        ir(s1, s2, P_SUELO, f'Ir a ({x:.0f}, {y:.0f}) bajando a soltar', toca=True)   # P baja mientras viaja
        esperar_servo()   # que la tarjeta ya esté girada antes de soltarla
        est1('SOLTAR', i + 1, i)
        bomba(False)
        esperar(0.3)
        est1('SUBIR_BANDEJA', i + 1, i + 1)
        ir(s1, s2, P_CAJA, 'Subir', viaje=True, hasta=P_SALIR_SUELO)   # recto hasta P_SALIR_SUELO, el resto mientras va a la caja
    servo(0)
    print(f'Secuencia terminada en {time.time() - T_INICIO:.1f} s')
    est1('ESPERA_PILA', None, N_PLACAS, t_ultimo=round(time.time() - T_INICIO, 1), ciclos=1)
    # Quitar el brazo de encima de la bandeja: primero el hombro (R2) y después el codo (R1) a sus finales de carrera
    ir_suave(medido[0], ESTACIONAR[1], P_CAJA, 'Hombro al final de carrera')
    ir_suave(ESTACIONAR[0], ESTACIONAR[1], P_CAJA, 'Codo al final de carrera')
    esperar_servo()
    esperar(SERVO_SOLTAR_S + 0.2)   # que alcance a apagar la señal del servo antes de terminar
except RuntimeError as e:
    print('DETENIDA:', e)
    bomba(False)
    if medido:   # quedarse donde está, para que el motor no siga empujando hacia un objetivo al que no llega
        pub_target.publish(Float64MultiArray(data=medido[:3]))
        esperar(0.5)
finally:
    nodo.destroy_node()
    rclpy.shutdown()
