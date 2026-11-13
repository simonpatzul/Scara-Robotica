# Ventana con una barra para mover el servo (GP26) con el robot corriendo.
# Publica en /scara/servo (Float64, grados 0..180); el bridge se lo manda a la Pico como "S<grados>".
# Uso: source /opt/ros/jazzy/setup.bash && python3 herramientas/scroll_servo.py
import tkinter as tk
import rclpy
from std_msgs.msg import Float64

rclpy.init()
nodo = rclpy.create_node('scroll_servo')
pub = nodo.create_publisher(Float64, '/scara/servo', 10)


def mandar(g):
    pub.publish(Float64(data=float(g)))
    estado.config(text=f'Servo a {g} grados')


ventana = tk.Tk()
ventana.title('Servo GP26')
barra = tk.Scale(ventana, label='Servo (grados)', from_=0, to=180, orient='horizontal', length=300,
                 command=mandar)
barra.set(90)
# rueda del mouse: 5 grados por paso (Button-4/5 en Linux, MouseWheel en otros)
barra.bind('<Button-4>', lambda e: barra.set(barra.get() + 5))
barra.bind('<Button-5>', lambda e: barra.set(barra.get() - 5))
barra.bind('<MouseWheel>', lambda e: barra.set(barra.get() + (5 if e.delta > 0 else -5)))
barra.pack(padx=20, pady=10)
estado = tk.Label(ventana, text='Servo a 90 grados')
estado.pack(pady=10)
ventana.mainloop()
nodo.destroy_node()
rclpy.shutdown()
