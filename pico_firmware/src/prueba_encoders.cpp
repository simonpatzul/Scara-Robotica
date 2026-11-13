// Programa aparte para revisar los encoders. Los motores quedan APAGADOS.
// Cada 200 ms imprime las cuentas de los 3 encoders y el nivel de sus pines A/B.
// Gira cada eje con la mano: solo deben cambiar las cuentas (y los pines) de ese eje.
#include <cstdio>
#include "pico/stdlib.h"
#include "encoder.h"
#include "quadrature_encoder.pio.h"

// Mismos pines y SM que config/axis_config.h (PCB nueva): R1 codo 12/13, R2 hombro 10/11, P 14/15
Encoder enc_r1(pio0, 0, 12, 13);
Encoder enc_r2(pio0, 1, 10, 11);
Encoder enc_p(pio0, 2, 14, 15);

// Entradas de los puentes en bajo = motores quietos (los EN los maneja el STM32): M1 0/1, M2 2/3, M3 6/7
const uint ENABLES[] = { 0, 1, 2, 3, 6, 7 };

int main() {
    for (uint pin : ENABLES) {
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_OUT);
        gpio_put(pin, 0);
    }
    stdio_init_all();
    enc_r1.init();
    enc_r2.init();
    enc_p.init();

    sleep_ms(3000);   // tiempo para abrir el monitor serie
    printf("\nPrueba de encoders (motores apagados). Gira cada eje con la mano.\n");

    int32_t antes[3] = { 0, 0, 0 };
    while (true) {
        const int32_t c[3] = { enc_r1.count(), enc_r2.count(), enc_p.count() };
        printf("R1=%7ld  R2=%7ld  P=%7ld   | pines R1(12,13)=%d%d R2(10,11)=%d%d P(14,15)=%d%d | finales 17,18,19=%d%d%d",
               (long)c[0], (long)c[1], (long)c[2],
               gpio_get(12), gpio_get(13), gpio_get(10), gpio_get(11), gpio_get(14), gpio_get(15),
               gpio_get(17), gpio_get(18), gpio_get(19));
        const char* nombres[3] = { "R1", "R2", "P" };
        for (int i = 0; i < 3; ++i)
            if (c[i] != antes[i]) printf("  <-- %s se movio %+ld", nombres[i], (long)(c[i] - antes[i]));
        printf("\n");
        for (int i = 0; i < 3; ++i) antes[i] = c[i];
        sleep_ms(200);
    }
}
