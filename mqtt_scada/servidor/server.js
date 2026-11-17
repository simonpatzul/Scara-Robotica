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

// Al conectar: suscribirse a los 2 topicos
mqttClient.on('connect', () => {
    console.log('Conectado a Mosquitto. Suscrito a scara/estado y scara/evento');
    mqttClient.subscribe([TOPIC_ESTADO, TOPIC_EVENTO]);
});

// Al llegar un mensaje: leer el JSON y guardarlo
mqttClient.on('message', async (topic, message) => {
    try {
        const d = JSON.parse(message.toString());

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

// CTRL+C: cerrar MQTT y la base
console.log('Backend SCARA corriendo. Presiona CTRL+C para detenerlo.');

process.on('SIGINT', async () => {
    console.log('\n[CTRL+C]');
    mqttClient.end();
    await pool.end();
    console.log('Adiós!');
    process.exit(0);
});
