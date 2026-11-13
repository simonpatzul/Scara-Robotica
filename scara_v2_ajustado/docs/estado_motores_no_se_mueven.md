# SCARA: estado actual y problema "los motores no se mueven"

## Reglas de trabajo
- Responde en **español**.
- **Pide permiso antes de crear o editar archivos.**
- Explicaciones cortas y por bloques (el usuario las presenta a su profesor).
- **Cambia una sola cosa a la vez** y prueba antes de seguir. Cambiar la Pico y el STM32 al mismo tiempo ya causó confusión.
- No entres al PC del usuario (control del computador) sin preguntar.

## Proyecto
Carpeta: `C:\Users\oscar\OneDrive\Documentos\Simon\UNIVERSIDAD\IoT\scara_v2`

| Parte | Archivo | Qué hace |
|---|---|---|
| Pico (Arduino IDE, core Earle Philhower 6.3.2) | `pico\scara_rst\scara_rst.ino` | Control RST de posición de los 3 ejes, home y comandos por Serial (COM9, 115200, "New Line") |
| Pico, programa de prueba | `pico\scara_prueba_pines\scara_prueba_pines.ino` | Mueve cada motor a mano con PWM (`1`/`2`/`3`, `+`/`-`, `0`). **Ya movió los motores antes**: sirve para descartar el hardware |
| STM32F030C8 (CubeIDE) | `scara_stm32\Core\Src\scara\scara.c` | Mide la corriente de los 3 drivers con el ADC, maneja los EN (PB12/13/14) y publica por MQTT |
| Backend | `server.js` + PostgreSQL 18 (puerto 5433) + Grafana | Dashboard "SCARA · Estación 1" |

Nombres que usa el usuario: **R1 = CODO** (motor en P1), **R2 = HOMBRO** (motor en P2), **P = prismático** (P3).

## Hardware relevante
- Pico: M1 INA/INB = GP0/GP1, M2 = GP2/GP3, M3 = GP7/GP6 (cruzados). Encoders: GP10–15. Servo GP8. Bomba GP9 (MOSFET Q2 → J2 → buck de 5 V → bomba).
- Finales de carrera (activos en bajo, **uno por eje**, solo el de home), según la prueba con `scara_prueba_pines`:
  - J2 = GP17 → final del motor en **P1** (codo)
  - J4 = GP19 → final del motor en **P2** (hombro)
  - J3 = GP18 → final de **P**
- Los EN de los drivers los prende el **STM32** al arrancar. Si la protección se dispara, apaga **los 3** EN y queda enclavado hasta reiniciar (botón **SW1**).
- Los drivers necesitan la **fuente de 12 V**. Con solo el USB, la Pico y el STM32 encienden pero los motores no.

## Estado actual de `scara_rst.ino` (en el PC)
```cpp
// Nombres: R1 = CODO (motor en P1), R2 = HOMBRO (motor en P2), P = prismatico (P3).
// nombre ina inb encA encB home fin cuentas relacion signo maxDuty homeV objetivo timeout min max
{ "R1", 0, 1, 10, 11, 17, -1, 3200, 3.0,  1.0, 0.50,  3.5, -1.48, 15000, -1.57,  1.57  }, // CODO
{ "R2", 2, 3, 12, 13, 19, -1, 3200, 3.0,  1.0, 0.50,  3.5, -1.83, 15000, -1.885, 1.885 }, // HOMBRO
{ "P",  7, 6, 14, 15, 18, -1, 3200, 3.1416, -1.0, 1.00, -8.0, 0.0, 30000, 0.0,   85.0  },
```
- RST de Landau (incremental, anti-windup), Ts = 10 ms, en el núcleo 1. Coeficientes del repo `scara-pcb-firmware` (modelo Kv = 1.8 rad/s/V, τ = 0.04 s).
- Comandos: `h` (home de los 3), `h1`/`h2`/`h3`, `r1,r2,p` (rad, rad, mm), `s` (parar), `x` (apagar), `v` (ver/ocultar el estado), `u` (modo carga).
- Seguridades del home: si a los 0.3 s el eje va al revés → "signo del encoder invertido". Si a los 2 s no se movió → "no se mueve (EN apagado o motor desconectado)". También hay timeout y falla por salirse del rango.
- Se probó y **se revirtió** un cambio de "cualquier final detiene el home" (el usuario lo deshizo): el archivo del PC ya **no** lo tiene.

## Cambios en `scara.c` (STM32) esta noche
- Se agregó un **filtro** a la corriente: promedio móvil exponencial con `FILTRO_N 16` (1 ms por muestra). La protección usa la corriente filtrada.
- Lo que se publica en `scara/estado` es el **promedio de los 500 ms** (antes era una sola muestra).
- Umbrales sin cambios: corte si pasa de 3 A durante 500 ms o de 4.5 A durante 20 ms.
- Hay que confirmar si esta versión ya se grabó en el STM32.

## Historia del problema
1. Con el STM32 recién grabado, **P marcaba 15–22 A falsos** con el robot quieto (33 A con el motor de P desconectado). La protección cortaba los 3 EN → "no se mueve".
2. Después de un reinicio, todo se movió: el home funcionó, pero R1 y R2 giraban al lado contrario → se cambió `homeVolts` a +3.5 y `homeObjetivo` a negativo.
3. Se cruzaron los finales de R1 y R2 → **un eje tocó el final y siguió derecho (chocó)** → se devolvieron a GP17 (P1) y GP19 (P2).
4. Se agregó el filtro en el STM32 y el "cualquier final" en la Pico → **desde ahí los motores no se mueven**. El "cualquier final" ya se revirtió.
5. Grafana desde las 21:40: **P cerca de 0 A** (la lectura falsa desapareció) y **R1 y R2 en 0 A todo el tiempo**, incluso al intentar moverlos. P sí muestra algo de actividad, así que el STM32 **no** parece estar en falla general.

## Hipótesis (de más a menos probable)
1. **El comando no llega o el programa no corre**: el Monitor Serie no está conectado, en "No line ending" o en otro COM, o la Pico tiene otro programa.
2. **EN de M1/M2 apagados**: protección enclavada, o los EN no llegan a los drivers.
3. **Algo en `scara_rst`** impide el PWM en GP0–GP3 (con `scara_prueba_pines` sí se movían).
4. **Fuente de 12 V** desconectada, o la conexión entre tarjetas (header de 40 pines).

## Pasos de diagnóstico (en este orden)
1. **Monitor Serie** (COM9, 115200, **"New Line"**):
   - ¿Salen líneas de estado cada 500 ms? Si no, el programa no corre: volver a subirlo y revisar el puerto.
   - Mandar `h1` y anotar **el texto exacto** que responde.
2. **Subir `scara_prueba_pines`** y mandar `1`, `+` (`0` para parar), luego `2`, `+`:
   - **Se mueven** → el problema está en `scara_rst`.
   - **No se mueven** → hardware, EN o STM32 (paso 3).
3. **Multímetro** (punta negra en GND, 12 V conectados):
   - M1_EN, M2_EN y M3_EN (header J5) → deben dar ~3.3 V.
   - GP0/GP1 mientras se manda movimiento → uno de los dos debe tener voltaje (PWM).
4. **MQTTX / Grafana**: estado `LISTO` o `FALLA`, y si llegó algo a `scara/evento`.
5. Si los EN están en 0 V: presionar **SW1** y volver a probar enseguida. Si se mueve un momento y se para, la protección se está disparando.

## Pendiente después de que se mueva
- Hacer el home de un eje a la vez (`h1`, `h2`, `h3`) con `x` listo, y confirmar que cada uno se detiene en **su** final.
- Revisar el sensor de corriente de P (driver U6/U7): mide PA4/PA5 con el robot quieto.
- Si P vuelve a dar lecturas falsas: proteger por eje, `PROTEGER[3] = {1, 1, 0}`, para que P no corte a todos.
- Comunicación I2C Pico ↔ STM32 (hoy el STM32 publica `pico_ok: false`).
