# Ventana con dos botones para prender y apagar la bomba (firmware prueba_bomba en la Pico).
# Uso: python3 herramientas/botones_bomba.py
import tkinter as tk
import serial

s = serial.Serial('/dev/ttyACM0', 115200, timeout=0.2)


def mandar(c):
    s.write(c.encode())
    resp = s.readline().decode(errors='ignore').strip()
    estado.config(text=resp or 'sin respuesta de la Pico')


ventana = tk.Tk()
ventana.title('Bomba GP22')
tk.Button(ventana, text='PRENDER', bg='green', fg='white', width=15, height=3,
          command=lambda: mandar('1')).pack(padx=20, pady=10)
tk.Button(ventana, text='APAGAR', bg='red', fg='white', width=15, height=3,
          command=lambda: mandar('0')).pack(padx=20, pady=10)
estado = tk.Label(ventana, text='BOMBA=?')
estado.pack(pady=10)
ventana.protocol('WM_DELETE_WINDOW', lambda: (mandar('0'), ventana.destroy()))  # al cerrar, apaga
ventana.mainloop()
