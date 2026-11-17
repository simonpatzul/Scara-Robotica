# Servidor local de la página SCADA: sirve mqtt_scada/web_local/scada_web y corre home.sh y secuencia.sh desde los botones.
#   GET  /api/estado     -> {"pico": bool, "home": {...}, "secuencia": {...}}: si existe /dev/ttyACM*; si corre, si terminó bien y su última línea
#   GET  /rviz.mjpg      -> video de la ventana de RViz del gemelo (rviz_stream.py)
#   POST /api/rviz/camara-> {t, x, y, b, d}: mueve la cámara de RViz (arrastre y rueda). No llega al robot
#   POST /api/home       -> corre bajo_nivel/home.sh (se queda corriendo: ahí vive el bridge)
#   POST /api/secuencia  -> corre alto_nivel/secuencias/secuencia.sh
#   POST /api/simular    -> {tipo, wn, zeta, r, kp, ti, td}: respuesta simulada de ese controlador, no toca nada
#   POST /api/controlador-> igual, y si es seguro escribe rst_activo.h, compila y lo sube con home.sh
#   POST /api/ir         -> {x, y, p} en mm: se lo escribe a scara_pc p2p
#   POST /api/teleop     -> {on}: arranca o para scara_pc teleop en una terminal virtual (lee las teclas de una terminal)
#   POST /api/plano      -> {vx, vy, vp} de -1 a 1: con Control, mueve la punta por el plano. La página lo repite: si deja de llegar, frena
#   POST /api/tecla      -> {k}: tecla sostenida (q a w s e d, z x servo, b n bomba) o null. La página la repite cada 100 ms: si deja de llegar,
#                           se suelta sola (hombre muerto). Mientras tanto se le escribe a teleop a 30 Hz, como un teclado.
# Los botones pasan por aquí y no por MQTT a propósito: el broker es público y cualquiera podría mover el robot.
# Solo escucha en 127.0.0.1 y solo acepta POST desde esta misma página.
# Salida de cada script: /tmp/scada_home.log y /tmp/scada_secuencia.log
# Uso: python3 mqtt_scada/web_local/scada_web.py
# Opcional: python3 mqtt_scada/web_local/scada_web.py <puerto> <home> <secuencia> <subir> <p2p> <teleop>   (otros scripts, para probar sin mover el robot)
import atexit
import functools
import glob
import json
import os
import pty
import signal
import subprocess
import sys
import tempfile
import threading
import time
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

AQUI = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(AQUI))   # la raíz del repositorio (mqtt_scada/web_local -> ..)
sys.path[:0] = [os.path.join(REPO, 'bajo_nivel', 'control_rst'), os.path.join(REPO, 'mqtt_scada', 'puente')]
import rst_diseno    # noqa: E402
import rviz_stream   # noqa: E402

PUERTO = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
HOME = sys.argv[2] if len(sys.argv) > 2 else os.path.join(REPO, 'bajo_nivel', 'home.sh')
SECUENCIA = sys.argv[3] if len(sys.argv) > 3 else os.path.join(REPO, 'alto_nivel', 'secuencias', 'secuencia.sh')
SUBIR = sys.argv[4] if len(sys.argv) > 4 else os.path.join(REPO, 'bajo_nivel', 'subir_controlador.sh')
P2P = sys.argv[5] if len(sys.argv) > 5 else ('source /opt/ros/jazzy/setup.bash && source alto_nivel/ros2_ws/install/setup.bash && '
                                             'exec ros2 run scara_pc p2p')
TELEOP = sys.argv[6] if len(sys.argv) > 6 else ('source /opt/ros/jazzy/setup.bash && source alto_nivel/ros2_ws/install/setup.bash && '
                                                'exec ros2 run scara_pc teleop')
CONTROL = ('source /opt/ros/jazzy/setup.bash && source alto_nivel/ros2_ws/install/setup.bash && exec python3 alto_nivel/gemelo/control_mando.py')
LATIDO_S = 0.25   # sin noticias de la página por este tiempo = botón soltado
RANGOS = {'wn': (1, 30), 'zeta': (0.3, 2), 'r': (0, 0.95), 'kp': (0, 50), 'ti': (0.02, 10), 'td': (0, 1)}
HOSTS = {f'localhost:{PUERTO}', f'127.0.0.1:{PUERTO}'}


class Tarea:
    def __init__(self, nombre, script):
        self.script = script
        self.log = os.path.join(tempfile.gettempdir(), f'scada_{nombre}.log')
        self.proc = None

    def corriendo(self):
        return self.proc is not None and self.proc.poll() is None

    def lanzar(self, script=None):
        salida = open(self.log, 'wb')
        # Sesión propia: el bridge que deja home.sh sigue vivo aunque se cierre este servidor
        self.proc = subprocess.Popen([script or self.script], cwd=REPO, stdin=subprocess.DEVNULL, stdout=salida,
                                     stderr=subprocess.STDOUT, start_new_session=True,
                                     env=dict(os.environ, PYTHONUNBUFFERED='1'))
        salida.close()

    def texto(self):
        try:
            with open(self.log, 'rb') as f:
                return f.read().decode(errors='ignore')
        except OSError:
            return ''

    def estado(self):
        texto = self.texto() if self.proc else ''
        lineas = [x.strip() for x in texto.splitlines() if x.strip()]
        return {'corriendo': self.corriendo(), 'rc': self.proc.poll() if self.proc else None,
                'listo': 'LISTO' in texto, 'linea': lineas[-1][-120:] if lineas else ''}


home = Tarea('home', HOME)
secuencia = Tarea('secuencia', SECUENCIA)
candado = threading.Lock()
p2p = None   # scara_pc p2p corriendo: lee "x y p" por su entrada estándar
P2P_LOG = os.path.join(tempfile.gettempdir(), 'scada_p2p.log')


def ir(x, y, p):
    global p2p
    if p2p is None or p2p.poll() is not None:
        with open(P2P_LOG, 'wb') as salida:
            p2p = subprocess.Popen(['bash', '-c', P2P], cwd=REPO, stdin=subprocess.PIPE, stdout=salida,
                                   stderr=subprocess.STDOUT, start_new_session=True, env=dict(os.environ, PYTHONUNBUFFERED='1'))
    p2p.stdin.write(f'{x} {y} {p}\n'.encode())
    p2p.stdin.flush()


class Teleop:
    def __init__(self):
        self.proc = None
        self.fd = None
        self.tecla = None
        self.mando = False   # corre control_mando.py en vez de teleop
        self.t_latido = 0.0
        threading.Thread(target=self.repetir, daemon=True).start()

    def corriendo(self):
        return self.proc is not None and self.proc.poll() is None

    def arrancar(self, mando=False):
        if self.corriendo():
            return
        self.mando = mando
        self.fd, esclavo = pty.openpty()
        with open(os.path.join(tempfile.gettempdir(), 'scada_teleop.log'), 'wb') as salida:
            self.proc = subprocess.Popen(['bash', '-c', CONTROL if mando else TELEOP], cwd=REPO, stdin=esclavo, stdout=salida,
                                         stderr=subprocess.STDOUT, start_new_session=True)
        os.close(esclavo)

    def parar(self):
        self.tecla = None
        if self.corriendo():
            os.killpg(self.proc.pid, signal.SIGINT)

    def latido(self, k):
        self.tecla, self.t_latido = k, time.monotonic()

    def repetir(self):
        while True:
            time.sleep(1 / 30)
            k = self.tecla
            if k is None:
                continue
            if time.monotonic() - self.t_latido > LATIDO_S or not self.corriendo():
                self.tecla = None
                continue
            os.write(self.fd, 2 * k.encode())   # doble: teleop lo toma como repetición y frena en 50 ms, no en los 0.8 s de la 1.ª pulsación


teleop = Teleop()
captura = rviz_stream.Captura()
atexit.register(teleop.parar)
signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))


def leer_controlador(datos):
    tipo = datos.get('tipo')
    if tipo not in ('implementado', 'incremental', 'posicional', 'enmascarado'):
        raise ValueError('controlador desconocido')
    p = {}
    for k, (lo, hi) in RANGOS.items():
        if k in datos:
            p[k] = float(datos[k])
            if not lo <= p[k] <= hi:
                raise ValueError(f'{k} fuera de rango ({lo} a {hi})')
    return tipo, p


def activo():
    try:
        with open(rst_diseno.RST_H) as f:
            f.readline()
            return f.readline().split('controlador ')[-1].split(' {')[0].strip()
    except OSError:
        return ''


class Pagina(SimpleHTTPRequestHandler):
    def responder(self, codigo, datos):
        cuerpo = json.dumps(datos).encode()
        self.send_response(codigo)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(cuerpo)))
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        self.wfile.write(cuerpo)

    def end_headers(self):
        if self.path == '/' or self.path.endswith('.html'):
            self.send_header('Cache-Control', 'no-store')   # que un cambio en la página se vea al recargar
        super().end_headers()

    def video(self):
        if self.headers.get('Host') not in HOSTS:
            return self.responder(403, {'error': 'origen no permitido'})
        self.send_response(200)
        self.send_header('Content-Type', 'multipart/x-mixed-replace; boundary=foto')
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        visto = 0.0
        with captura.cambio:
            captura.mirando += 1
        try:
            while True:
                with captura.cambio:
                    captura.cambio.wait_for(lambda: captura.t_foto != visto, timeout=5)   # sin foto nueva repite: así nota si la página se fue
                    foto, visto = captura.foto, captura.t_foto
                if foto:
                    self.wfile.write(b'--foto\r\nContent-Type: image/jpeg\r\nContent-Length: %d\r\n\r\n' % len(foto) + foto + b'\r\n')
        except OSError:
            pass   # la página cerró el video
        finally:
            with captura.cambio:
                captura.mirando -= 1

    def do_GET(self):
        if self.path.split('?')[0] == '/rviz.mjpg':
            return self.video()
        if self.path != '/api/estado':
            return super().do_GET()
        with candado:
            self.responder(200, {'pico': bool(glob.glob('/dev/ttyACM*')), 'controlador': activo(), 'teleop': teleop.corriendo(), 'mando': teleop.corriendo() and teleop.mando, 'rviz': captura.estado(),
                                 'home': home.estado(), 'secuencia': secuencia.estado()})

    def do_POST(self):
        if self.path not in ('/api/home', '/api/secuencia', '/api/simular', '/api/controlador', '/api/ir', '/api/teleop', '/api/tecla', '/api/plano', '/api/rviz/camara'):
            return self.responder(404, {'error': 'no existe'})
        # Otra página abierta en el navegador no puede mover el robot
        origen = self.headers.get('Origin', '').removeprefix('http://')
        if self.headers.get('Host') not in HOSTS or origen not in HOSTS:
            return self.responder(403, {'error': 'origen no permitido'})
        try:
            datos = json.loads(self.rfile.read(int(self.headers.get('Content-Length') or 0)) or b'{}')
            if self.path in ('/api/simular', '/api/controlador'):
                tipo, p = leer_controlador(datos)
                c = rst_diseno.disenar(tipo, p)
            elif self.path == '/api/ir':
                x, y, z = float(datos['x']), float(datos['y']), float(datos['p'])
                if not 0 <= z <= 100:
                    raise ValueError('p fuera de rango (0 a 100 mm)')
            elif self.path == '/api/rviz/camara':
                t, x, y = datos['t'], float(datos['x']), float(datos['y'])
                boton, pasos = int(datos.get('b', 1)), int(datos.get('d', 0))
                if t not in ('down', 'move', 'up', 'wheel') or boton not in (1, 2, 3) or not (0 <= x <= 1 and 0 <= y <= 1):
                    raise ValueError('evento de cámara inválido')
            elif self.path == '/api/plano':
                v = [float(datos[k]) for k in ('vx', 'vy', 'vp')]
                if not all(-1 <= x <= 1 for x in v):
                    raise ValueError('velocidad fuera de rango')
            elif self.path == '/api/tecla' and datos.get('k') not in (None, 'q', 'a', 'w', 's', 'e', 'd', 'z', 'x', 'b', 'n'):
                raise ValueError('tecla desconocida')
        except (ValueError, KeyError, TypeError, ZeroDivisionError, OverflowError) as e:
            return self.responder(400, {'error': str(e)})
        except Exception as e:   # numpy: un diseño imposible (matriz singular)
            return self.responder(400, {'error': f'no se pudo diseñar: {e}'})
        if self.path == '/api/rviz/camara':
            return self.responder(200 if captura.camara(t, x, y, boton, pasos) else 409, {'ok': True})
        if self.path == '/api/simular':
            t, yy, u = rst_diseno.simular(c)
            m = rst_diseno.metricas(c)
            return self.responder(200, {'t': t[::2].round(3).tolist(), 'y': yy[::2].round(4).tolist(), 'ref': 1.57,
                                        'sobrepaso': round(float(m['sobrepaso_pct']), 1), 'asentamiento': round(float(m['asentamiento_s']), 2),
                                        'u_pico': round(float(m['u_pico']), 1), 'motivo': rst_diseno.segura(c)})
        if self.path == '/api/tecla' and datos.get('k') is None:
            teleop.tecla = None   # soltar siempre se atiende, aunque haya algo corriendo
            return self.responder(200, {'ok': True})
        if self.path == '/api/teleop' and not datos.get('on'):
            teleop.parar()
            return self.responder(200, {'ok': True})
        with candado:
            if secuencia.corriendo():
                return self.responder(409, {'error': 'la secuencia está corriendo'})
            h = home.estado()
            if h['corriendo'] and not h['listo']:
                return self.responder(409, {'error': 'el home está en curso'})
            if self.path == '/api/ir':
                ir(x, y, z)
            elif self.path == '/api/teleop':
                teleop.arrancar(bool(datos.get('mando')))
            elif self.path == '/api/plano':
                if not (teleop.corriendo() and teleop.mando):
                    return self.responder(409, {'error': 'Control no está activo'})
                os.write(teleop.fd, ('v %.3f %.3f %.3f\n' % tuple(v)).encode())
                return self.responder(200, {'ok': True})
            elif self.path == '/api/tecla':
                if not teleop.corriendo():
                    return self.responder(409, {'error': 'teleop no está corriendo'})
                teleop.latido(datos['k'])
                return self.responder(200, {'ok': True})
            elif self.path == '/api/controlador':
                motivo = rst_diseno.segura(c)
                if motivo:
                    return self.responder(422, {'error': f'no se sube: {motivo}'})
                rst_diseno.escribir(tipo, c, p)
                home.lanzar(SUBIR)
            else:
                (home if self.path == '/api/home' else secuencia).lanzar()
        print(f'{self.path} desde la página', flush=True)
        self.responder(200, {'ok': True})

    def log_message(self, *args):
        pass   # la página pregunta el estado cada segundo: no llenar la terminal


def main():
    pagina = functools.partial(Pagina, directory=os.path.join(AQUI, 'scada_web'))
    print(f'Página en http://localhost:{PUERTO}/', flush=True)
    ThreadingHTTPServer(('127.0.0.1', PUERTO), pagina).serve_forever()


if __name__ == '__main__':
    main()
