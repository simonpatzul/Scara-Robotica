# Muestra en vivo el estado de los pines de finales de carrera (firmware de prueba en la Pico).
# Solo imprime cuando algún pin cambia.   abierto = flotando | GND = a tierra | 3V3 = a 3.3 V
# Uso: python3 ~/scara_ws/ver_limits.py      (Ctrl+C para salir)
import re
import serial

s = serial.Serial('/dev/ttyACM0', 115200, timeout=0.5)
prev = None
print('Presiona cada final de carrera uno por uno...')
while True:
    try:
        line = s.readline().decode(errors='ignore')
    except KeyboardInterrupt:
        break
    pins = re.findall(r'GP(\d+) U\d D\d ([A-Za-z0-9?]+)', line)
    if len(pins) != 8:
        continue
    now = dict(pins)
    if now != prev:
        cambios = [p for p in now if prev and prev[p] != now[p]]
        print('  '.join(f'GP{p}={e}' + ('  <--' if p in cambios else '') for p, e in now.items()))
        prev = now
