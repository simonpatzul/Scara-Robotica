# Compara en simulación los RST de rst_diseno.py para la posición: los dos escalones de Controladores_RST.m, con la planta de
# 3.er orden, la resolución del encoder y la saturación real de R1 y R2 (6 V). Deja rst_comparacion.png y rst_comparacion.csv.
# Uso: python3 bajo_nivel/control_rst/rst_comparar.py
import csv
import os
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import rst_diseno as d

AQUI = os.path.dirname(os.path.abspath(__file__))
U_MAX = 6.0
ESCALONES = (1.57, 9.42)   # el del .m y uno de los largos que hace el robot
# Colores fijos por controlador; el trazo también los distingue, para verlos en blanco y negro
SERIES = (('implementado', 'RST implementado', '#2a78d6', '-'), ('incremental', 'RST incremental', '#eb6834', '--'),
          ('posicional', 'RST posicional', '#1baf7a', '-.'), ('enmascarado', 'RST enmascarado', '#6250d6', ':'))


def ref(t):
    return 3.14 if t >= 1.5 else 1.57 if t >= 0.2 else 0.0


def grafica():
    fig, (ax_y, ax_u) = plt.subplots(2, 1, sharex=True, figsize=(11, 7), height_ratios=(2, 1), layout='constrained')
    for tipo, nombre, color, trazo in SERIES:
        t, y, u = d.simular(d.disenar(tipo), ref, U_MAX)
        ax_y.plot(t, y, trazo, color=color, lw=2, label=nombre)
        ax_u.plot(t, u, trazo, color=color, lw=1.6)
    ax_y.step(t, [ref(x) for x in t], where='post', color='#555555', lw=1.2, label='Referencia')
    ax_u.axhline(U_MAX, color='#888888', lw=1, ls=(0, (2, 3)))
    ax_u.axhline(-U_MAX, color='#888888', lw=1, ls=(0, (2, 3)))
    ax_u.text(2.98, U_MAX - 0.3, f'Saturación ±{U_MAX:.0f} V', ha='right', va='top', color='#555555', fontsize=10)
    ax_y.set_title('Respuesta de posición', loc='left', fontweight='bold')
    ax_u.set_title('Esfuerzo de control', loc='left', fontweight='bold')
    ax_y.set_ylabel('Posición [rad]')
    ax_u.set_ylabel('Voltaje [V]')
    ax_u.set_xlabel('Tiempo [s]')
    ax_u.set_xlim(0, 3)
    ax_y.legend(loc='lower right', frameon=False)
    for ax in (ax_y, ax_u):
        ax.grid(axis='y', color='#e4e3df', lw=0.8)
        ax.spines[['top', 'right']].set_visible(False)
    fig.savefig(os.path.join(AQUI, 'rst_comparacion.png'), dpi=160)


def tabla():
    filas = []
    for tipo, nombre, _, _ in SERIES:
        c = d.disenar(tipo)
        for escalon in ESCALONES:
            m = d.metricas(c, escalon, U_MAX)
            filas.append([nombre, escalon, round(m['sobrepaso_pct'], 1), round(m['asentamiento_s'], 2), round(m['u_pico'], 1),
                          round(m['ruido_v'], 3), round(m['error_final'], 3)])
    with open(os.path.join(AQUI, 'rst_comparacion.csv'), 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['controlador', 'escalon_rad', 'sobrepaso_pct', 'asentamiento_2pct_s', 'voltaje_pico_v', 'ruido_v', 'error_final_rad'])
        w.writerows(filas)
    return filas


if __name__ == '__main__':
    grafica()
    for fila in tabla():
        print(*fila, sep=' | ')
