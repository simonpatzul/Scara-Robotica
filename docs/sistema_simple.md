# SCARA: monitoreo de corriente, parada y alertas (versión simple)

## Qué hace
1. El **STM32** mide la corriente de los 3 motores y la manda por Wi-Fi (MQTT) cada 0.5 s.
2. Si un motor pasa de **3 A**, el STM32 manda una **alerta**.
3. En **Grafana** se ve la corriente, las alertas y hay un botón **PARAR**, que apaga los 3 drivers.
4. Las coordenadas y el movimiento se manejan aparte, con **ROS 2** en la laptop Linux (`bajo_nivel/home.sh`).

## Cómo viaja la información
```
Motores -> sensor del IFX007T -> ADC del STM32 -> FC41D (Wi-Fi) -> Mosquitto -> server.js -> PostgreSQL -> Grafana

Grafana (botón PARAR) -> tabla comandos -> server.js -> Mosquitto -> FC41D -> STM32 -> EN de los drivers = 0
```

## Los 3 mensajes MQTT
| Tópico | Quién lo manda | Ejemplo |
|---|---|---|
| `scara/estado` | STM32, cada 0.5 s | `{"on":1,"i1":0.12,"i2":0.08,"i3":0.11}` (on = drivers prendidos; i1, i2, i3 = amperios de R1, R2, P) |
| `scara/evento` | STM32, cuando un motor pasa de 3 A | `{"eje":"R1","falla":"alta","i":3.20}` |
| `scara/cmd` | Grafana (por server.js) | `off` = PARAR, `on` = prender |

## Las 4 partes del código
### 1. STM32: `firmware/stm32/Core/Src/scara_simple.c` (un solo archivo)
- **Corriente:** el ADC lee 2 canales por motor con DMA. Al arrancar se mide el "cero" con los motores quietos.
  `corriente = (lectura - cero) × 3.3 / 4095 / 470 × 19500` (A).
  Se promedia cada 0.5 s.
- **Wi-Fi y MQTT con comandos AT** (`comando_at()` manda el comando y espera la respuesta):

  | Comando AT | Para qué |
  |---|---|
  | `AT+QSTAAPINFO=Simon,clave` | conectarse al Wi-Fi |
  | `AT+QMTOPEN=1,"IP",1883` | abrir la conexión con Mosquitto |
  | `AT+QMTCONN=1,"scara_stm32"` | entrar al broker |
  | `AT+QMTSUB=1,1,"scara/cmd",1` | escuchar los comandos |
  | `AT+QMTPUB=...` | publicar estado y alertas |

- **Comandos:** cuando llega `+QMTRECV` con `"off"`, pone PB12, PB13 y PB14 (EN de los drivers) en 0. Con `"on"`, en 1.
- **Configuración** (arriba del archivo): red Wi-Fi, clave, `BROKER_IP` (la IP del PC) y `LIMITE_A`.
- `main.c` es el que genera CubeMX. Solo llama a `scara_simple()`.

### 2. Mosquitto (broker MQTT, en el PC)
`C:\Program Files\mosquitto\mosquitto.conf` solo tiene 2 líneas:
```
listener 1883 0.0.0.0
allow_anonymous true
```
`mqtt_scada/servidor/abrir_mosquitto_red.bat` las agrega y abre el puerto.

### 3. `mqtt_scada/servidor/server.js` (Node.js)
- Recibe `scara/estado` y lo guarda en la tabla `telemetria` (ciclo = ENCENDIDO o APAGADO, `r1_i`, `r2_i`, `p_i`).
- Recibe `scara/evento` y lo guarda en `eventos`.
- Cuando Grafana inserta un comando en la tabla `comandos`, lo publica en `scara/cmd`.

### 4. Grafana: `mqtt_scada/grafana/dashboards/scara_simple.json`
- Botón **PARAR** (`off`) y **Prender EN** (`on`).
- Corriente ahora y gráfica de 5 minutos (naranja desde 2.5 A, rojo desde 3 A).
- Alertas activas (reglas de Grafana) y tabla con las alertas del STM32.

## Cómo se usa
1. Prender Mosquitto (servicio de Windows) y `node server.js` en `mqtt_scada/servidor`.
2. Prender el robot: el STM32 se conecta solo (tarda unos 10 s).
3. Abrir Grafana: `http://localhost:3000`.

## Si cambia la IP del PC
El celular ("Simon") puede darle otra IP al PC.
1. Ver la IP con `ipconfig`.
2. Cambiar `BROKER_IP` en `scara_simple.c`.
3. Compilar en CubeIDE y grabar con el ST-Link (conector J1: SWDIO, SWCLK y GND).

## Problemas conocidos (18-11)
- **I2C Pico ↔ STM32:** la línea SDA (PB7 / GP5) está pegada a GND en la placa. Por eso esta versión no usa el I2C.
- **Codo en el Home:** el encoder del codo (GP10/GP11) no cuenta. La corriente no pasó de 1.1 A, así que no es un trabado.
- **Grabación por SWD:** con los motores conectados había ruido. Se graba mejor con los motores desconectados.
- La versión completa anterior (I2C, búsqueda del broker, comandos `mv`/`seq`) está en el commit `5f80298`.
