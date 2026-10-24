# Prueba de ejes (firmware de prueba en la Pico). Cada tecla mueve UN motor un pulso corto (0.3 s).
#   1 / 2 / 3 = Motor1 / Motor2 / Motor3 por RPWM      q / w / e = el mismo motor al otro lado (LPWM)
#   + / - = subir / bajar el duty (10 %)     s = ver contadores y límites     x = salir
# Anota qué articulación se movió con cada tecla y qué ENC contó.
import sys, termios, tty, select, serial

s = serial.Serial('/dev/ttyACM0', 115200, timeout=0)
print('1/2/3 = Motor1/2/3 RPWM | q/w/e = LPWM | +/- duty | s = estado | r = revisar encoders | x = salir')
old = termios.tcgetattr(sys.stdin)
tty.setcbreak(sys.stdin)
try:
    while True:
        r, _, _ = select.select([sys.stdin, s], [], [], 0.1)
        if s in r:
            sys.stdout.write(s.read(4096).decode(errors='ignore')); sys.stdout.flush()
        if sys.stdin in r:
            k = sys.stdin.read(1)
            if k == 'x':
                break
            s.write(k.encode())
finally:
    termios.tcsetattr(sys.stdin, termios.TCSADRAIN, old)
