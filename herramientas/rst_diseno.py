# Diseño de los controladores de Controladores_RST.m: RST incremental, RST posicional y RST enmascarado.
# Se diseña sobre el modelo reducido Kv / (s (tau s + 1)) de axis_config.h, no sobre la planta de 3.er orden del .m: esa tiene
# un cero en -1.3 que da un controlador inestable (con la resolución real del encoder el posicional no llega al objetivo).
# La planta de 3.er orden sí se usa para simular. Forma del firmware (RstController):
#   Incremental y enmascarado: S'(q^-1) du(k) = T(q^-1) y*(k) - R(q^-1) y(k),  u(k) = u(k-1) + du(k),  Am y*(k) = Bm r(k-1)
#   Posicional: u(k) = T r(k) - R y(k) - S'(resto) u(k-1...), voltaje absoluto (RstParams.posicional)
# Enmascarado (un PID escrito como RST): u(k) = u(k-1) + sum(R) r - R y, o sea S' = 1, Am = [1, 0, 0], Bm = 1 y y*(k) = r(k-1).
# Uso: python3 herramientas/rst_diseno.py [implementado|incremental|posicional|enmascarado] [--escribir]
import os
import sys
import numpy as np
from scipy.signal import cont2discrete

TS = 0.01
K_PLANTA = 155212.362859 * 14 / 50
DEN_PLANTA = [1, 988.607399, 24090.185, 0]
KV, TAU = 1.8, 0.04   # el modelo de diseño de axis_config.h
CUENTA = 2 * np.pi / 3200   # resolución del encoder en rad del motor
RST_H = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'pico_firmware', 'config', 'rst_activo.h')
PREDET = {'wn': 15.0, 'zeta': 0.85, 'r': 0.5, 'kp': 5.0, 'ti': 0.2, 'td': 0.01}


def planta():
    num, den, _ = cont2discrete(([K_PLANTA], DEN_PLANTA), TS, 'zoh')
    num = np.ravel(num)
    num[np.abs(num) < 1e-12] = 0.0
    return num, np.ravel(den)   # B = [0, b1, b2, b3], A = [1, a1, a2, a3]


def reducida():
    """Modelo de diseño Kv / (s (tau s + 1)): sin el polo rápido, que deja un cero en -1.3 y un controlador inestable."""
    num, den, _ = cont2discrete(([KV], [TAU, 1, 0]), TS, 'zoh')
    num = np.ravel(num)
    return np.r_[0, num[np.abs(num) > 1e-12]], np.ravel(den)   # B = [0, b1, b2], A = [1, a1, a2]


def polos(wn, zeta, r):
    num, den, _ = cont2discrete(([wn ** 2], [1, 2 * zeta * wn, wn ** 2]), TS, 'zoh')
    Am = np.ravel(den)
    Ac = np.convolve([1, -r], np.convolve([1, -r], [1, -r, r ** 2]))
    return Am, Ac


def diofantina(A, B, nS, Acl):
    """A S + B R = Acl con S(0) = 1. Devuelve S y R (grado de R: el que falte para que la matriz sea cuadrada)."""
    n = len(Acl) - 1
    nR = n - nS - 1
    M = np.zeros((n + 1, nS + 1 + nR + 1))
    for j in range(nS + 1):
        M[j:j + len(A), j] = A
    for j in range(nR + 1):
        M[j:j + len(B), nS + 1 + j] = B
    x = np.linalg.solve(M[1:, 1:], Acl[1:] - M[1:, 0])
    return np.r_[1, x[:nS]], x[nS:]


def incremental(wn, zeta, r):
    B, A = reducida()
    Am, _ = polos(wn, zeta, r)
    Ac = np.poly([r, r])
    S, R = diofantina(np.convolve([1, -1], A), B, 1, np.convolve(Am, Ac))
    return {'R': np.r_[R, 0, 0], 'Sp': np.r_[S, 0, 0], 'T': np.r_[Ac * Am.sum() / B.sum(), np.zeros(5)],
            'Am': Am, 'Bm': Am.sum()}


def posicional(wn, zeta, r):
    B, A = reducida()
    Am, _ = polos(wn, zeta, r)
    Acl = np.convolve(Am, [1, -r])
    S, R = diofantina(A, B, 1, Acl)
    Tg = Acl.sum() / B.sum()
    return {'R': np.r_[R, 0, 0, 0], 'Sp': np.r_[S, 0, 0], 'T': np.r_[Tg, np.zeros(7)],
            'Am': np.array([1.0, 0, 0]), 'Bm': 1.0, 'posicional': True}


def enmascarado(kp, ti, td):
    q = [kp * (1 + TS / ti + td / TS), -kp * (1 + 2 * td / TS), kp * td / TS]
    return {'R': np.r_[q, 0, 0], 'Sp': np.array([1.0, 0, 0, 0]), 'T': np.r_[sum(q), np.zeros(7)],
            'Am': np.array([1.0, 0, 0]), 'Bm': 1.0}


# El RST que venía en axis_config.h (control/rst_motor_v3.m): lento y suave, polos doble en 6 rad/s y auxiliares en 0.7
IMPLEMENTADO = {'R': np.array([2.059447617952, -1.561290298166, -2.047948810692, 1.572789105426, 0.0]),
            'Sp': np.array([1.0, -1.208998870792, 0.394471201417, 0.0]),
            'T': np.array([251.156203565986, -1000.488037304793, 1585.381209607035, -1249.331488730012,
                           489.710270900887, -76.405160424582, 0.0, 0.0]),
            'Am': np.array([1.0, -1.902459, 0.904837]), 'Bm': 0.002378}


def disenar(tipo, p=None):
    p = {**PREDET, **(p or {})}
    if tipo == 'implementado':
        return IMPLEMENTADO
    if tipo == 'incremental':
        return incremental(p['wn'], p['zeta'], p['r'])
    if tipo == 'posicional':
        return posicional(p['wn'], p['zeta'], p['r'])
    if tipo == 'enmascarado':
        return enmascarado(p['kp'], p['ti'], p['td'])
    raise ValueError(tipo)


def simular(c, ref=1.57, u_max=6.0, t_fin=3.0):
    """Corre RstController::step tal cual sobre la planta. ref: escalón en 0.2 s o función del tiempo. Devuelve t, y, u."""
    B, A = planta()
    n = int(t_fin / TS)
    pos = c.get('posicional', False)
    y, u = np.zeros(n), np.zeros(n)
    yh, ysh, duh, rp, up = np.zeros(5), np.zeros(8), np.zeros(3), 0.0, 0.0
    for k in range(3, n):
        y[k] = -A[1:4] @ y[[k - 1, k - 2, k - 3]] + B[1:4] @ u[[k - 1, k - 2, k - 3]]
        r = ref(k * TS) if callable(ref) else ref if k * TS >= 0.2 else 0.0
        yh = np.r_[round(y[k] / CUENTA) * CUENTA, yh[:-1]]
        if pos:
            up = min(max(c['T'][0] * r - c['R'] @ yh - c['Sp'][1:] @ duh, -u_max), u_max)
            duh = np.r_[up, duh[:-1]]
            u[k] = up
            continue
        ysh = np.r_[-c['Am'][1] * ysh[0] - c['Am'][2] * ysh[1] + c['Bm'] * rp, ysh[:-1]]
        rp = r
        du = c['T'] @ ysh - c['R'] @ yh - c['Sp'][1:] @ duh
        un = min(max(up + du, -u_max), u_max)
        duh = np.r_[un - up, duh[:-1]]
        up = un
        u[k] = un
    return np.arange(n) * TS, y, u


def metricas(c, ref=1.57, u_max=6.0):
    t, y, u = simular(c, ref, u_max)
    err = np.abs(y - ref) > 0.02 * ref
    asentado = t[np.nonzero(err)[0][-1]] - 0.2 if err.any() else 0.0
    return {'sobrepaso_pct': max(0.0, (y.max() - ref) / ref * 100), 'asentamiento_s': asentado,
            'error_final': abs(y[-1] - ref), 'u_pico': np.abs(u).max(), 'ruido_v': u[-100:].std(),
            'estable': bool(np.isfinite(y).all() and abs(y[-1] - ref) < 0.05 * ref)}


def segura(c):
    """None si se puede subir; si no, el motivo. Prueba los escalones que hace el robot, con la saturación de cada eje."""
    raices = np.roots(np.trim_zeros(c['Sp'], 'b'))
    if len(raices) and np.abs(raices).max() >= 1:
        return 'el controlador es inestable por sí solo'
    for ref, u_max in ((1.57, 6.0), (9.42, 6.0), (30.0, 12.0)):
        m = metricas(c, ref, u_max)
        if not m['estable']:
            return f'no llega al objetivo en un escalón de {ref} rad'
        if m['ruido_v'] > 0.3:
            return f'vibra {m["ruido_v"]:.2f} V por la resolución del encoder'
        if m['sobrepaso_pct'] > 25:
            return f'se pasa {m["sobrepaso_pct"]:.0f} %'
    return None


def escribir(tipo, c, p=None):
    v = lambda a: ', '.join(f'{x:.12f}' for x in a)
    with open(RST_H, 'w') as f:
        f.write(f'#pragma once\n// Generado por herramientas/rst_diseno.py: controlador {tipo} {p or PREDET}\n'
                '#include "rst_controller.h"\n\nconstexpr RstParams RST_ACTIVO = {\n'
                f'    {{ {v(c["R"])} }},\n    {{ {v(c["Sp"])} }},\n    {{ {v(c["T"])} }},\n    {{ {v(c["Am"])} }},\n'
                f'    {float(c["Bm"]):.12f},\n    {"true" if c.get("posicional") else "false"}\n}};\n')


if __name__ == '__main__':
    tipos = [a for a in sys.argv[1:] if not a.startswith('-')] or ['implementado', 'incremental', 'posicional', 'enmascarado']
    for tipo in tipos:
        c = disenar(tipo)
        print(tipo, {k: round(float(x), 3) if not isinstance(x, bool) else x for k, x in metricas(c).items()}, segura(c) or 'se puede subir')
        if '--escribir' in sys.argv:
            escribir(tipo, c)
