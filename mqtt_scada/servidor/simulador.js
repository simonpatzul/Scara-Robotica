// Simulador: STM32 de mentira para probar sin el robot
// Uso: node simulador.js (con server.js corriendo)
// Tambien obedece scara/cmd como el STM32 real: on, off, on1..off3, reset,
// y ademas "mv r1,r2,p" (el robot real todavia no mueve con eso).

const mqtt = require('mqtt');
const client = mqtt.connect('mqtt://localhost:1883');

// Tiempo y falla simulada (en segundos)
let t = 0;
let falla = 0;

// EN de cada eje (1 = prendido) y coordenadas objetivo (null = vaiven automatico)
const en = [1, 1, 1];
let objetivo = null;
const pos = [0, 0, 25];

client.on('connect', () => {
    console.log('Simulador conectado. Publicando cada 500 ms... (CTRL+C para parar)');
    client.subscribe('scara/cmd');

    // Cada 500 ms
    setInterval(() => {
        t += 0.5;

        // Cada 60 s: falla de sobrecorriente en R2 (solo si R2 esta prendido)
        if (t % 60 === 0 && en[1]) {
            falla = 3;
            client.publish('scara/evento', JSON.stringify({ up: t, eje: 'R2', falla: 'pico', i: 4.8 }));
            console.log('Falla simulada en R2');
        }

        // Posiciones: van y vienen, o se acercan al objetivo de "mv"
        const auto = [0.8 * Math.sin(t / 4), 0.6 * Math.sin(t / 3), 25 + 20 * Math.sin(t / 2)];
        for (let k = 0; k < 3; k++) {
            if (!en[k]) continue;   // eje apagado: se queda quieto
            pos[k] = objetivo ? pos[k] + (objetivo[k] - pos[k]) * 0.3 : auto[k];
        }

        // Corrientes con ruido (4.8 A en R2 si hay falla; 0 si el eje esta apagado)
        const ruido = () => (Math.random() - 0.5) * 0.1;
        const i1 = en[0] ? 0.4 + 1.5 * Math.abs(Math.cos(t / 4)) + ruido() : 0;
        const i2 = !en[1] ? 0 : falla > 0 ? 4.8 : 0.4 + 1.8 * Math.abs(Math.cos(t / 3)) + ruido();
        const i3 = en[2] ? 0.2 + 1.0 * Math.abs(Math.cos(t / 2)) + ruido() : 0;

        const est = (k) => (k === 1 && falla > 0) ? 'F' : (en[k] ? 'R' : 'A');
        const ciclo = falla > 0 ? 'FALLA' : (en.some(e => e) ? 'LISTO' : 'APAGADO');

        // Mismo JSON que el STM32
        const estado = {
            v: 1, up: t, ciclo, pico_ok: true,
            temp: { pico: 34 + Math.sin(t / 30), stm32: 31 + Math.sin(t / 40) },
            ejes: [
                { n: 'R1', est: est(0), pos: pos[0], i: i1, falla: 'ninguna' },
                { n: 'R2', est: est(1), pos: pos[1], i: i2, falla: falla > 0 ? 'pico' : 'ninguna' },
                { n: 'P',  est: est(2), pos: pos[2], i: i3, falla: 'ninguna' }
            ]
        };

        // Publicar
        client.publish('scara/estado', JSON.stringify(estado));
        if (falla > 0) falla -= 0.5;
    }, 500);
});

// Comandos de Grafana (via server.js)
client.on('message', (topic, msg) => {
    const c = msg.toString().trim();
    console.log('Comando recibido:', c);
    if (c === 'on')    { en.fill(1); falla = 0; }
    else if (c === 'off')   en.fill(0);
    else if (c === 'reset') falla = 0;
    else if (/^on[1-3]$/.test(c))  { en[+c[2] - 1] = 1; if (c === 'on2') falla = 0; }
    else if (/^off[1-3]$/.test(c)) en[+c[3] - 1] = 0;
    else if (c.startsWith('mv ')) {
        const v = c.slice(3).split(',').map(Number);
        if (v.length === 3 && v.every(Number.isFinite)) objetivo = v;
    }
});
