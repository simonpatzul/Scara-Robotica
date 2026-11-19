# Actualización STM32 + MQTT (18 nov 2026)

Resumen de lo que se hizo para que el STM32 publique por MQTT con el firmware del enlace I2C
(`f09bda9` y posteriores) y qué falta para usar el robot desde MQTT/Grafana.

## Estado al terminar
- STM32 grabado con el firmware nuevo y **conectado a Mosquitto** (PC del SCADA en `10.217.162.20`, red "Simon").
- `scara/estado` llega cada 0.5 s con `"broker":"10.217.162.20"`. `scara/evento` publica `EST` al cambiar de ciclo.
- Ciclo `SIN_PICO` (`pico_ok:false`): la Pico todavía no tiene el firmware nuevo o no contesta por I2C. Es lo esperado hasta hacer el Home desde Linux.
- Los 3 EN prendidos (`"est":"R"`), corrientes en reposo de unos 0.1 A.
- Se borraron las tablas `telemetria` y `eventos` de PostgreSQL (los datos eran del simulador). `simulador.js` quedó apagado.

## Cambios en el código (`firmware/stm32/Core/Src/FC41/FC41.c`)
1. **Arreglo del cuelgue en `FC_OPEN` / `FC_OPENrx`.**
   - Antes, `FC_OPEN` mandaba `AT+QMTOPEN` sin arrancar el timeout. Si el FC41 contestaba `+QMTOPEN: 1,<error>` sin un `OK`, `FC_OPENrx` no atendía ese evento y el STM32 **se quedaba esperando para siempre**.
   - Ahora `FC_OPEN` hace `fc41_timeout=1` y `FC_OPENrx` también pasa a la siguiente IP con `EV_FC41_sessionError`.
2. **`DEFAULT_IP` = `"10.217.162.20"`** (antes `"172.19.133.20"`).
   - Es la IP que el hotspot "Simon" le dio hoy al PC del SCADA. Es **provisional** (ver "Pendiente").

Tamaño en Debug: 47 632 B de FLASH (de 63 KB) y 4.8 KB de RAM.

## Cómo se encontró el problema
Se leyó la RAM del STM32 en caliente con el ST-Link (`STM32_Programmer_CLI ... mode=hotplug -r8 / -u`), sin pararlo.
Las direcciones salen de `arm-none-eabi-nm -S Debug/scara_stm32.elf`.

| Variable | Qué dice |
|---|---|
| `fc41_state`, `fc41_event`, `fc41_timeout` | en qué paso está el FC41 y qué evento tiene pendiente |
| `propia`, `puerta`, `base` (broker.c) | IP propia, puerta de enlace y subred |
| `actual`, `paso` (broker.c) | IP del broker que está probando |
| `states` | contiene la última respuesta del FC41 (`rx_response`) y el último comando AT |

## Pendiente (bugs encontrados)
1. ~~**"Identificador MQTT ocupado".**~~ **Arreglado en el commit siguiente (sin probar en la placa).**
   - Antes: tras una IP que no contesta, todos los `AT+QMTOPEN` daban `+QMTOPEN: 1,2`.
   - Ahora: `AT+QMTCLOSE=1` y espera (`FC_CLOSE_WAIT`) a que el FC41 confirme `+QMTCLOSE: 1,...` o a que termine el `QMTOPEN` viejo.
   - Si no se libera en `CIERRE_MAX_MS` (20 s), o si da "ocupado" 3 veces seguidas con la misma IP, reinicia el FC41 con CEN.
   - Con el código 2 repite la **misma** IP, porque puede ser la buena.
   - Tras reiniciar el FC41, la lista sigue por donde iba (si la IP propia es la misma).
   - Ojo: cada IP sin broker puede costar hasta unos 20–35 s (espera + reinicio). En una red nueva, la búsqueda completa puede tardar mucho. La IP buena queda en la flash y la siguiente vez conecta al primer intento.
2. ~~**`get_ip()` toma la máscara como puerta de enlace.**~~ **Arreglado:** si la respuesta trae `gate:`, la puerta se lee de ahí.
3. **Grabación por SWD inestable.** El borrado y la escritura fallaban en páginas al azar con el robot en 12 V (leer sí funcionaba). Se grabó página por página con reintentos y luego se comparó toda la flash: 0 diferencias. Conviene revisar el GND y el ruido del J1.

## Siguiente: Home desde Linux y tareas por MQTT
1. En la laptop de Linux:
   ```
   cd ~/<repo>/ && git pull
   ```
2. Compilar el firmware de la Pico, si no está compilado:
   ```
   cd firmware/pico/build && cmake .. && make scara_fw
   ```
3. Compilar el bridge nuevo (`MV`, `SEQ`, `STOP`):
   ```
   cd alto_nivel/ros2_ws && colcon build
   ```
4. **Home** (recarga el firmware de la Pico y hace el Home; **solo con el robot despejado y con Simon presente**):
   ```
   bajo_nivel/home.sh
   ```
5. Revisar en el PC del SCADA:
   ```
   mosquitto_sub -h 10.217.162.20 -t "scara/#" -v
   ```
   Debe salir `pico_ok:true`, posiciones reales y ciclo `LISTO`.

### Comandos en `scara/cmd`
`mosquitto_pub -h 10.217.162.20 -t scara/cmd -m <comando>`, o con los botones de Grafana (`scara-est1`).

| Comando | Qué hace | ¿Mueve el robot? |
|---|---|---|
| `on` / `off` | prende / apaga los 3 EN (`on` también borra las fallas) | no |
| `on1`..`on3` / `off1`..`off3` | un eje (1 = R1, 2 = R2, 3 = P) | no |
| `reset` | borra las fallas | no |
| `mv r1,r2,p` | ir a esas coordenadas (rad, rad, mm) | **sí** |
| `seq` / `stop` | correr / parar la secuencia de la caja | **sí** |

- **Prueba de EN sin mover el robot:** `off2`. En `scara/estado`, R2 pasa a `"est":"A"`. Después `on2`.
- `mv`, `seq` y `stop` **solo con confirmación de Simon**.
- **P nunca se baja de forma automática.**

## Notas
- `mqtt_scada/servidor/server.js` local tiene la contraseña real de PostgreSQL: **no subirlo**. En el repo debe decir `TU_CONTRASENA`.
- `git status` marca muchos `Drivers/` como cambiados: son solo fines de línea (LF/CRLF). No subirlos.
- Para volver al firmware sin I2C: commit `bb304d7`.
