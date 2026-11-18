// Librerias: PostgreSQL, MQTT y Express (los botones de Grafana mandan los comandos por HTTP)
const { Pool } = require('pg');
const mqtt = require('mqtt');
const express = require('express');

// Conexion a PostgreSQL 18
const dbConfig = {
    host: 'localhost',
    port: 5433,
    user: 'postgres',
    password: 'TU_CONTRASENA',
    database: 'scara'
};
const pool = new Pool(dbConfig);

// Conexion a Mosquitto y topicos del STM32
const mqttClient = mqtt.connect('mqtt://localhost:1883');
const TOPIC_ESTADO = 'scara/estado';
const TOPIC_EVENTO = 'scara/evento';
const TOPIC_CMD = 'scara/cmd';             // lo leen el STM32 (parar/prender) y el nodo mqtt_cmd del PC (el resto)
const TOPIC_RESPUESTA = 'scara/respuesta'; // texto: lo que hizo el STM32 o el PC con cada comando
const TOPIC_POS = 'scara/pos';             // JSON del nodo mqtt_cmd: r1, r2, p, x, y

// Al conectar: suscribirse a los topicos
mqttClient.on('connect', () => {
    console.log('Conectado a Mosquitto. Suscrito a scara/estado, evento, respuesta y pos');
    mqttClient.subscribe([TOPIC_ESTADO, TOPIC_EVENTO, TOPIC_RESPUESTA, TOPIC_POS]);
});

// Al llegar un mensaje: leer el JSON y guardarlo
mqttClient.on('message', async (topic, message) => {
    try {
        // Respuesta: es texto, no JSON
        if (topic === TOPIC_RESPUESTA) {
            await pool.query('INSERT INTO comandos (origen, texto) VALUES ($1, $2)', ['respuesta', message.toString()]);
            console.log(`RESPUESTA: ${message.toString()}`);
            return;
        }

        const d = JSON.parse(message.toString());

        // Posicion del robot: una fila en posicion
        if (topic === TOPIC_POS) {
            await pool.query('INSERT INTO posicion (r1, r2, p, x, y, parado) VALUES ($1, $2, $3, $4, $5, $6)',
                [d.r1, d.r2, d.p, d.x, d.y, d.parado]);
            return;
        }

        // Estado: una fila en telemetria ($1..$16 en orden)
        if (topic === TOPIC_ESTADO) {
            const [r1, r2, p] = d.ejes;

            const query = 'INSERT INTO telemetria (ciclo, pico_ok, temp_pico, temp_stm32, ' +
                'r1_pos, r1_i, r1_est, r1_falla, ' +
                'r2_pos, r2_i, r2_est, r2_falla, ' +
                'p_pos,  p_i,  p_est,  p_falla) ' +
                'VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16)';

            await pool.query(query, [
                d.ciclo, d.pico_ok, d.temp.pico, d.temp.stm32,
                r1.pos, r1.i, r1.est, r1.falla,
                r2.pos, r2.i, r2.est, r2.falla,
                p.pos,  p.i,  p.est,  p.falla
            ]);
            console.log(`Estado guardado. ciclo=${d.ciclo}  i=${r1.i} ${r2.i} ${p.i} A`);
        }

        // Falla: una fila en eventos
        if (topic === TOPIC_EVENTO) {
            const query = 'INSERT INTO eventos (eje, falla, corriente) VALUES ($1, $2, $3)';
            await pool.query(query, [d.eje, d.falla, d.i]);
            console.log(`EVENTO: eje ${d.eje} falla=${d.falla} i=${d.i} A`);
        }

    // Error: se avisa y el servidor sigue
    } catch (error) {
        console.error('Error procesando MQTT:', error.message);
    }
});

// Comandos desde Grafana: los botones son formularios HTML (panel Text) que hacen POST a /cmd.
// Solo pasan los comandos de esta lista; cualquier otra cosa se rechaza sin publicar nada.
const PUERTO_HTTP = 3001;
const NUM = '-?\\d+(\\.\\d+)?';
const COMANDO_VALIDO = new RegExp(`^(parar|prender|bomba,[01]|servo,\\d{1,3}|mover,${NUM},${NUM},${NUM})$`);

const app = express();
app.use(express.urlencoded({ extended: false }));
app.use(express.json());
app.use((req, res, next) => {   // Grafana corre en otro puerto: permitir la peticion
    res.set('Access-Control-Allow-Origin', '*');
    res.set('Access-Control-Allow-Headers', 'Content-Type');
    next();
});

// Pagina chica que se ve dentro del panel de Grafana despues de apretar un boton
function pagina(texto, ok) {
    return `<body style="margin:0;font:14px sans-serif;color:${ok ? '#73bf69' : '#f2495c'}">${texto}</body>`;
}

app.post('/cmd', async (req, res) => {
    let cmd = String(req.body.cmd || '').trim().toLowerCase();
    if (cmd === 'mover') cmd = `mover,${req.body.x},${req.body.y},${req.body.p}`;
    if (cmd === 'servo') cmd = `servo,${req.body.g}`;
    cmd = cmd.replace(/\s+/g, '');

    if (!COMANDO_VALIDO.test(cmd)) {
        console.log(`Comando rechazado: ${cmd}`);
        return res.status(400).send(pagina(`Rechazado: "${cmd.replace(/[<>&"]/g, '')}" no es un comando valido`, false));
    }
    if (!mqttClient.connected) {
        return res.status(503).send(pagina('Sin conexion con Mosquitto', false));
    }

    mqttClient.publish(TOPIC_CMD, cmd, { qos: 1 });
    console.log(`COMANDO: ${cmd}`);
    try {
        await pool.query('INSERT INTO comandos (origen, texto) VALUES ($1, $2)', ['grafana', cmd]);
    } catch (error) {
        console.error('Error guardando el comando:', error.message);
    }
    res.send(pagina(`Enviado: ${cmd}`, true));
});

const servidorHttp = app.listen(PUERTO_HTTP, () => console.log(`Comandos en http://localhost:${PUERTO_HTTP}/cmd`));

// CTRL+C: cerrar MQTT y la base
console.log('Backend SCARA corriendo. Presiona CTRL+C para detenerlo.');

process.on('SIGINT', async () => {
    console.log('\n[CTRL+C]');
    servidorHttp.close();
    mqttClient.end();
    await pool.end();
    console.log('Adiós!');
    process.exit(0);
});
