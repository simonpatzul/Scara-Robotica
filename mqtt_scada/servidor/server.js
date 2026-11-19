// Librerias: PostgreSQL y MQTT
const { Pool } = require('pg');
const mqtt = require('mqtt');

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
const TOPIC_CMD    = 'scara/cmd';

// Comandos de Grafana: los botones insertan en la tabla comandos y el trigger avisa por NOTIFY.
// Solo se mandan comandos de los ultimos 10 s (uno viejo, p. ej. "on" de ayer, no debe prender nada).
async function enviarComandos() {
    const { rows } = await pool.query(
        "UPDATE comandos SET enviado = true WHERE NOT enviado AND ts > now() - interval '10 seconds' " +
        'RETURNING id, cmd');
    for (const c of rows.sort((a, b) => a.id - b.id)) {
        mqttClient.publish(TOPIC_CMD, c.cmd);
        console.log(`COMANDO -> ${TOPIC_CMD}: ${c.cmd}`);
    }
}

async function escucharComandos() {
    const cliente = await pool.connect();
    await cliente.query('LISTEN scara_cmd');
    cliente.on('notification', () => enviarComandos().catch(e => console.error('Error enviando comando:', e.message)));
    console.log('Escuchando comandos de Grafana (tabla comandos)');
}
escucharComandos().catch(e => console.error('No se pudo escuchar comandos (¿corriste schema.sql?):', e.message));

// Al conectar: suscribirse a los 2 topicos
mqttClient.on('connect', () => {
    console.log('Conectado a Mosquitto. Suscrito a scara/estado y scara/evento');
    mqttClient.subscribe([TOPIC_ESTADO, TOPIC_EVENTO]);
});

// Al llegar un mensaje: leer el JSON y guardarlo
mqttClient.on('message', async (topic, message) => {
    try {
        const d = JSON.parse(message.toString());

        // Estado: {"on":1,"i1":0.12,"i2":0.08,"i3":0.11} -> una fila en telemetria
        if (topic === TOPIC_ESTADO) {
            const ciclo = d.on ? 'ENCENDIDO' : 'APAGADO';
            const query = 'INSERT INTO telemetria (ciclo, r1_i, r2_i, p_i) VALUES ($1, $2, $3, $4)';
            await pool.query(query, [ciclo, d.i1, d.i2, d.i3]);
            console.log(`Estado guardado. ${ciclo}  i=${d.i1} ${d.i2} ${d.i3} A`);
        }

        // Alerta: {"eje":"R1","falla":"alta","i":3.20} -> una fila en eventos
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

// CTRL+C: cerrar MQTT y la base
console.log('Backend SCARA corriendo. Presiona CTRL+C para detenerlo.');

process.on('SIGINT', async () => {
    console.log('\n[CTRL+C]');
    mqttClient.end();
    await pool.end();
    console.log('Adiós!');
    process.exit(0);
});
