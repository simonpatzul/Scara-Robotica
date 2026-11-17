# Manda la imagen de RViz a la página publicada en la web, por el mismo broker de scada_mqtt.py. Solo sale: no recibe nada ni toca el robot.
# Toma el video de scada_web.py, lo achica y publica unas pocas fotos por segundo en <prefijo>/rviz. Se corre aparte y se para con Ctrl+C.
# Uso: python3 mqtt_scada/puente/rviz_mqtt.py
import http.client
import io
import threading
import time
import paho.mqtt.client as mqtt
from PIL import Image

BROKER, PUERTO = 'broker.hivemq.com', 1883
PREFIJO = 'scara'     # el mismo que scada_mqtt.py y la página
SCADA = ('localhost', 8765)
FOTOS_S = 4           # broker público: poca tasa y fotos chicas
ANCHO = 640
CALIDAD = 55

foto = [None]


def leer_video():
    while True:
        try:
            con = http.client.HTTPConnection(*SCADA, timeout=8)
            con.request('GET', '/rviz.mjpg', headers={'Host': f'{SCADA[0]}:{SCADA[1]}'})
            resp, datos = con.getresponse(), b''
            while True:
                bloque = resp.read(65536)
                if not bloque:
                    break
                datos += bloque
                fin = datos.rfind(b'\xff\xd9')
                inicio = datos.rfind(b'\xff\xd8', 0, fin) if fin > 0 else -1
                if inicio >= 0:
                    foto[0] = datos[inicio:fin + 2]
                    datos = datos[fin + 2:]
        except OSError:
            pass
        foto[0] = None
        time.sleep(1)


def main():
    threading.Thread(target=leer_video, daemon=True).start()
    cliente = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    cliente.connect_async(BROKER, PUERTO, keepalive=30)
    cliente.loop_start()
    print(f'Publicando RViz en {BROKER} como {PREFIJO}/rviz', flush=True)
    enviada = None
    while True:
        time.sleep(1 / FOTOS_S)
        actual = foto[0]
        if actual is None or actual is enviada:
            continue
        enviada = actual
        imagen = Image.open(io.BytesIO(actual)).convert('RGB')
        imagen.thumbnail((ANCHO, ANCHO))
        salida = io.BytesIO()
        imagen.save(salida, 'JPEG', quality=CALIDAD)
        cliente.publish(f'{PREFIJO}/rviz', salida.getvalue(), qos=0)


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        pass
