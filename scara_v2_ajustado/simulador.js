// Simulador: STM32 de mentira para probar sin el robot
// Uso: node simulador.js (con server.js corriendo)

const mqtt = require('mqtt');
const client = mqtt.connect('mqtt://localhost:1883');

// Tiempo y falla simulada (en segundos)
let t = 0;
let falla = 0;

client.on('connect', () => {
    console.log('Simulador conectado. Publicando cada 500 ms... (CTRL+C para parar)');

    // Cada 500 ms
    setInterval(() => {
        t += 0.5;

        // Cada 60 s: falla de sobrecorriente en R2
        if (t % 60 === 0) {
            falla = 3;
            client.publish('scara/evento', JSON.stringify({ up: t, eje: 'R2', falla: 'pico', i: 4.8 }));
            console.log('Falla simulada en R2');
        }

        // Posiciones que van y vienen
        const r1 = 0.8 * Math.sin(t / 4);
        const r2 = 0.6 * Math.sin(t / 3);
        const p  = 25 + 20 * Math.sin(t / 2);

        // Corrientes con ruido (4.8 A en R2 si hay falla)
        const ruido = () => (Math.random() - 0.5) * 0.1;
        const i1 = 0.4 + 1.5 * Math.abs(Math.cos(t / 4)) + ruido();
        const i2 = falla > 0 ? 4.8 : 0.4 + 1.8 * Math.abs(Math.cos(t / 3)) + ruido();
        const i3 = 0.2 + 1.0 * Math.abs(Math.cos(t / 2)) + ruido();

        // Mismo JSON que el STM32
        const estado = {
            v: 1, up: t, ciclo: falla > 0 ? 'FALLA' : 'LISTO', pico_ok: true,
            temp: { pico: 34 + Math.sin(t / 30), stm32: 31 + Math.sin(t / 40) },
            ejes: [
                { n: 'R1', est: 'R',                   pos: r1, i: i1, falla: 'ninguna' },
                { n: 'R2', est: falla > 0 ? 'F' : 'R', pos: r2, i: i2, falla: falla > 0 ? 'pico' : 'ninguna' },
                { n: 'P',  est: 'R',                   pos: p,  i: i3, falla: 'ninguna' }
            ]
        };

        // Publicar
        client.publish('scara/estado', JSON.stringify(estado));
        if (falla > 0) falla -= 0.5;
    }, 500);
});
