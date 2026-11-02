# Ventana con dos botones para prender y apagar la bomba y una barra para el servo
# (firmware prueba_bomba en la Pico).
# Uso: python3 herramientas/botones_bomba.py
import glob
import tkinter as tk
import serial

def abrir():
    return serial.Serial(sorted(glob.glob('/dev/ttyACM*'))[0], 115200, timeout=0.2)  # ACM0 o ACM1 si la Pico se reconectó


s = abrir()


def mandar(c):
    global s
    try:
        s.write(c.encode())
    except (serial.SerialException, OSError):   # la Pico se desconectó: se busca el puerto otra vez y se reintenta
        try:
            s.close()
            s = abrir()
            s.write(c.encode())
        except (serial.SerialException, OSError, IndexError):
            estado.config(text='Pico desconectada, intenta otra vez')
            return
    resp = s.readline().decode(errors='ignore').strip()
    estado.config(text=resp or 'sin respuesta de la Pico')


ventana = tk.Tk()
ventana.title('Bomba GP22 / Servo GP26')
tk.Button(ventana, text='PRENDER', bg='green', fg='white', width=15, height=3,
          command=lambda: mandar('1')).pack(padx=20, pady=10)
tk.Button(ventana, text='APAGAR', bg='red', fg='white', width=15, height=3,
          command=lambda: mandar('0')).pack(padx=20, pady=10)
tk.Scale(ventana, label='Servo (grados)', from_=0, to=180, orient='horizontal', length=250,
         command=lambda g: mandar(f'S{g}\n')).pack(padx=20, pady=10)  # también se mueve con la rueda del mouse
estado = tk.Label(ventana, text='BOMBA=?')
estado.pack(pady=10)
ventana.protocol('WM_DELETE_WINDOW', lambda: (mandar('0'), ventana.destroy()))  # al cerrar, apaga la bomba
ventana.mainloop()
