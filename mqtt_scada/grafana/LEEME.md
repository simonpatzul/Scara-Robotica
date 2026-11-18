# SCADA en Grafana

```
Grafana (botones) --POST /cmd--> server.js --MQTT scara/cmd--> STM32  (parar / prender: EN de los drivers)
                                                          \--> PC: mqtt_cmd (mover, bomba, servo) --> bridge --> Pico
STM32 --scara/estado, scara/evento--> server.js --> PostgreSQL --> Grafana (gráficas y alertas)
PC mqtt_cmd --scara/pos, scara/respuesta--> server.js --> PostgreSQL
```

## Comandos de `scara/cmd` (texto, sin comillas)
| Comando | Lo atiende | Qué hace |
|---|---|---|
| `parar` | STM32 y PC | apaga los EN de los 3 drivers; el objetivo se queda donde está el brazo |
| `prender` | STM32 y PC | prende los EN y borra las fallas enclavadas |
| `mover,x,y,p` | PC | IK y mueve (mm). Se rechaza si está parado, sin Home, fuera de alcance o P fuera de 0–50 |
| `bomba,1` / `bomba,0` | PC | prende / apaga la bomba |
| `servo,g` | PC | servo a g grados (0–180) |

`server.js` solo publica comandos de esta lista. Lo que hizo el robot llega a `scara/respuesta`.

## Instalar (PC del SCADA, Windows)
1. Base: correr otra vez `servidor/schema.sql` (agrega las tablas `comandos` y `posicion`).
2. `server.js`: `npm install` y `node server.js`. Ahora también abre el puerto **3001** para los botones.
3. Grafana, en `grafana.ini` (o `custom.ini`): los botones son formularios HTML y Grafana los borra si no se permite:
   ```ini
   [panels]
   disable_sanitize_html = true
   ```
4. Copiar `provisioning/datasources/scara.yaml`, `provisioning/dashboards/scara.yaml` y
   `provisioning/alerting/scara_alertas.yaml` a la carpeta `conf/provisioning/` de Grafana (en las mismas subcarpetas).
   - En `datasources/scara.yaml` poner la contraseña de PostgreSQL.
   - En `dashboards/scara.yaml` poner la ruta real de `mqtt_scada/grafana/dashboards`.
   - O sin provisioning: Dashboards → Import → `dashboards/scara_estacion1.json`.
5. Reiniciar Grafana. El dashboard queda en la carpeta SCARA: **SCARA · Estación 1**.
6. Si Grafana se abre desde otra computadora, cambiar la variable **Servidor de comandos** (arriba del dashboard)
   por `IP_DEL_PC:3001`.

## En el PC del robot (Linux)
Con el bridge corriendo y el Home hecho:
```bash
ros2 run scara_pc mqtt_cmd --ros-args -p broker:=172.19.133.20
```
`broker` es el PC donde corre Mosquitto (el mismo `DEFAULT_IP` de `FC41.c`).
El PC del robot tiene que estar en la misma red que ese PC (el Wi-Fi `DEFAULT_SSID` de `FC41.c`). Para probar que llega:
```bash
timeout 3 bash -c "echo > /dev/tcp/172.19.133.20/1883" && echo ok
```

## STM32
`firmware/stm32` es el proyecto completo de STM32CubeIDE (File → Import → Existing Projects into Workspace).
Se graba con `firmware/stm32/grabar_stm32.bat`.

## Alertas
Sobrecorriente (> 3 A), falla del STM32, STM32 sin datos (> 5 s), robot parado y comando rechazado.
Se ven en **Alerting → Alert rules** y en el panel *Alertas activas*. Para que lleguen por correo o Telegram
hay que crear un *contact point* en Grafana.
