// Programa aparte para revisar los motores, uno por uno y sin control.
// Con cada Enter: el codo y el hombro empujan 0.4 s hacia un lado y 0.4 s de regreso al 25 % de PWM,
// y se imprime cuánto se movió CADA encoder. Debe moverse solo el encoder del eje que se está empujando.
#include <cstdio>
#include <initializer_list>
#include "pico/stdlib.h"
#include "encoder.h"
#include "motor_driver.h"

// Mismos pines que config/axis_config.h (PCB nueva). Los EN los maneja el STM32
Encoder enc[3] = { Encoder(pio0, 0, 12, 13), Encoder(pio0, 1, 10, 11), Encoder(pio0, 2, 14, 15) };
MotorDriver drv[3] = { MotorDriver(3, 2, 0xFF, 0xFF, 0.3f), MotorDriver(1, 0, 0xFF, 0xFF, 0.3f), MotorDriver(7, 6, 0xFF, 0xFF, 0.3f) };
const char* NOMBRES[3] = { "R1 (codo)", "R2 (hombro)", "P" };
constexpr float DUTY = 0.25f;
constexpr uint32_t T_MS = 400;

void empujar(int i, float duty) {
    int32_t antes[3];
    for (int k = 0; k < 3; ++k) antes[k] = enc[k].count();
    drv[i].enable(true);
    drv[i].set(duty);
    // Durante el empujón: cuentas y pines A/B de los 3 encoders cada 50 ms
    for (uint32_t t = 0; t < T_MS; t += 50) {
        sleep_ms(50);
        printf("    %3lu ms  R1 %+6ld (%d%d)  R2 %+6ld (%d%d)  P %+6ld (%d%d)\n", (unsigned long)(t + 50),
               (long)(enc[0].count() - antes[0]), gpio_get(12), gpio_get(13),
               (long)(enc[1].count() - antes[1]), gpio_get(10), gpio_get(11),
               (long)(enc[2].count() - antes[2]), gpio_get(14), gpio_get(15));
    }
    drv[i].enable(false);
    sleep_ms(300);
    printf("  driver %-11s %+.2f -> cuentas  R1 %+6ld  R2 %+6ld  P %+6ld | finales 17,18,19=%d%d%d\n", NOMBRES[i], duty,
           (long)(enc[0].count() - antes[0]), (long)(enc[1].count() - antes[1]), (long)(enc[2].count() - antes[2]),
           gpio_get(17), gpio_get(18), gpio_get(19));
}

int main() {
    for (MotorDriver& d : drv) d.init();   // drivers apagados lo antes posible
    stdio_init_all();
    for (Encoder& e : enc) e.init();
    for (uint pin : {17u, 18u, 19u}) { gpio_init(pin); gpio_pull_up(pin); }

    sleep_ms(3000);
    printf("\nPrueba de motores: cada driver empuja %.0f %% durante %lu ms ida y vuelta.\n", DUTY * 100, (unsigned long)T_MS);
    while (true) {
        printf("Presiona ENTER para probar el hombro (codo y P no se mueven).\n");
        int c;
        do { c = getchar(); } while (c != '\n' && c != '\r');
        // Solo el hombro (10-11): + debe alejarlo de su final (a la izquierda), - acercarlo (a la derecha)
        printf("Hombro + (deberia ir a la IZQUIERDA)\n");
        empujar(1, DUTY);
        sleep_ms(3000);
        printf("Hombro - (deberia ir a la DERECHA, hacia su final)\n");
        empujar(1, -DUTY);
    }
}
