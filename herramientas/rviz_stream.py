# Captura la ventana de RViz del gemelo y la deja lista para la página como MJPEG. De vuelta solo acepta mover la cámara:
# arrastrar y rueda, únicamente sobre el área 3D del visor sin MoveIt (gemelo_visor.rviz), donde nada puede llegar al robot.
# Wayland no deja capturar el escritorio sin permiso, pero RViz corre por XWayland y su ventana sí se puede leer por su id.
# gst-launch va en un proceso aparte: un error de X (ventana cerrada) mata a ese proceso y no a scada_web.
# Solo captura mientras alguien mira. RViz se abre aparte con herramientas/gemelo_visor.sh.
import ctypes
import re
import subprocess
import threading
import time

FPS = 15
CALIDAD = 75
VENTANA = re.compile(r'^\s*(0x[0-9a-f]+) "(.*)": \("rviz2" "[Rr]viz2"\)\s+(\d+)x(\d+)', re.M)


class EventoBoton(ctypes.Structure):   # XButtonEvent; XMotionEvent tiene la misma forma
    _fields_ = [('type', ctypes.c_int), ('serial', ctypes.c_ulong), ('send_event', ctypes.c_int), ('display', ctypes.c_void_p),
                ('window', ctypes.c_ulong), ('root', ctypes.c_ulong), ('subwindow', ctypes.c_ulong), ('time', ctypes.c_ulong),
                ('x', ctypes.c_int), ('y', ctypes.c_int), ('x_root', ctypes.c_int), ('y_root', ctypes.c_int),
                ('state', ctypes.c_uint), ('button', ctypes.c_uint), ('same_screen', ctypes.c_int)]


class EventoX(ctypes.Union):
    _fields_ = [('b', EventoBoton), ('relleno', ctypes.c_long * 24)]


PRESIONAR, SOLTAR, MOVER = 4, 5, 6
MASCARA = {1: 256, 2: 512, 3: 1024}   # Button1Mask...


class Raton:
    """Manda eventos directo a la ventana con XSendEvent: no mueve el puntero del escritorio."""
    def __init__(self):
        self.x11 = ctypes.CDLL('libX11.so.6')
        self.x11.XOpenDisplay.restype = ctypes.c_void_p
        self.x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
        self.x11.XSendEvent.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_long, ctypes.POINTER(EventoX)]
        self.x11.XFlush.argtypes = [ctypes.c_void_p]
        self.d = self.x11.XOpenDisplay(None)
        self.candado = threading.Lock()
        self.boton = 0

    def enviar(self, ventana, tipo, x, y, estado, boton):
        e = EventoX()
        e.b.type, e.b.window, e.b.x, e.b.y, e.b.x_root, e.b.y_root = tipo, ventana, x, y, x, y
        e.b.state, e.b.button, e.b.same_screen, e.b.time = estado, boton, 1, int(time.time() * 1000) & 0xffffffff
        self.x11.XSendEvent(self.d, ventana, 1, 0, ctypes.byref(e))
        self.x11.XFlush(self.d)

    def evento(self, ventana, t, x, y, boton, pasos):
        with self.candado:
            if t == 'down':
                self.boton = boton
                self.enviar(ventana, PRESIONAR, x, y, 0, boton)
            elif t == 'move' and self.boton:
                self.enviar(ventana, MOVER, x, y, MASCARA[self.boton], 0)
            elif t == 'up' and self.boton:
                self.enviar(ventana, SOLTAR, x, y, MASCARA[self.boton], self.boton)
                self.boton = 0
            elif t == 'wheel':
                rueda = 4 if pasos > 0 else 5
                self.enviar(ventana, PRESIONAR, x, y, 0, rueda)
                self.enviar(ventana, SOLTAR, x, y, 0, rueda)


def buscar_ventana():
    """(id, ancho, alto, es el visor) del área 3D de RViz (la subventana más grande), o None si está cerrado."""
    try:
        arbol = subprocess.run(['xwininfo', '-root', '-tree'], capture_output=True, text=True, timeout=3).stdout
    except (OSError, subprocess.SubprocessError):
        return None
    ventanas = [(int(w) * int(h), xid, titulo, int(w), int(h)) for xid, titulo, w, h in VENTANA.findall(arbol) if int(w) > 100 and int(h) > 100]
    areas = [v for v in ventanas if v[2] == 'rviz2'] or ventanas
    if not areas:
        return None
    _, xid, _, ancho, alto = max(areas)
    return xid, ancho, alto, any('gemelo_visor' in v[2] for v in ventanas)


class Captura:
    def __init__(self):
        self.cambio = threading.Condition()
        self.foto = None
        self.t_foto = 0.0
        self.mirando = 0
        self.xid = None
        self.ancho = self.alto = 0
        self.visor = False
        self.raton = Raton()
        threading.Thread(target=self.correr, daemon=True).start()

    def estado(self):
        return {'abierto': self.xid is not None, 'camara': self.visor, 'edad_s': round(time.time() - self.t_foto, 1) if self.t_foto else None}

    def camara(self, t, x, y, boton, pasos):
        """x e y van de 0 a 1 sobre la imagen. Devuelve False si RViz no es el visor: el RViz con MoveIt no se toca desde aquí."""
        if not self.xid or not self.visor:
            return False
        self.raton.evento(int(self.xid, 16), t, round(x * self.ancho), round(y * self.alto), boton, pasos)
        return True

    def leer(self, salida):
        """Saca las fotos del multipart de gst-launch hasta que se acabe o ya nadie mire."""
        while self.mirando:
            largo = 0
            while True:
                linea = salida.readline()
                if not linea:
                    return
                if linea.lower().startswith(b'content-length:'):
                    largo = int(linea.split(b':')[1])
                if linea == b'\r\n' and largo:
                    break
            foto = salida.read(largo)
            if len(foto) < largo:
                return
            with self.cambio:
                self.foto, self.t_foto = foto, time.time()
                self.cambio.notify_all()

    def correr(self):
        while True:
            hallada = buscar_ventana()
            self.xid, self.ancho, self.alto, self.visor = hallada or (None, 0, 0, False)
            if not self.xid or not self.mirando:
                time.sleep(1)
                continue
            with self.cambio:
                self.foto, self.t_foto = None, 0.0   # nada viejo: si no llegan fotos, la página lo dice
            proc = subprocess.Popen(['gst-launch-1.0', '-q', 'ximagesrc', f'xid={self.xid}', 'use-damage=false', 'show-pointer=false', '!',
                                     f'video/x-raw,framerate={FPS}/1', '!', 'videoconvert', '!', 'jpegenc', f'quality={CALIDAD}', '!',
                                     'multipartmux', 'boundary=foto', '!', 'fdsink', 'fd=1'],
                                    stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
            self.leer(proc.stdout)
            proc.kill()
            proc.wait()
            time.sleep(1)
