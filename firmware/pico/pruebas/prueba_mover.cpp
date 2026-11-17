// Programa aparte para mover un motor a mano, sin home y sin control.
// Órdenes por el monitor serie: "c+" / "c-" = driver R1 (codo), "h+" / "h-" = driver R2 (hombro), "p+" / "p-" = driver P.
// Cada orden empuja T_MS al DUTY y apaga; imprime cuánto se movió cada encoder.
#include <cstdio>
#include "pico/stdlib.h"
#include "encoder.h"
#include "motor_driver.h"

// Mismos pines que config/axis_config.h
Encoder enc[3] = { Encoder(pio0, 0, 11, 10), Encoder(pio0, 1, 12, 13), Encoder(pio0, 2, 8, 9) };
MotorDriver drv[3] = { MotorDriver(4, 5, 7, 6, 0.5f), MotorDriver(0, 1, 3, 2, 0.5f), MotorDriver(14, 15, 17, 16, 0.5f) };
constexpr float DUTY = 0.4f;
constexpr uint32_t T_MS = 500;

void empujar(int i, float duty) {
    int32_t antes[3];
    for (int k = 0; k < 3; ++k) antes[k] = enc[k].count();
    drv[i].enable(true);
    drv[i].set(duty);
    sleep_ms(T_MS);
    drv[i].set(0.0f);
    drv[i].enable(false);
    sleep_ms(200);
    printf("driver %d %+.2f -> cuentas  R1 %+6ld  R2 %+6ld  P %+6ld\n", i, duty,
           (long)(enc[0].count() - antes[0]), (long)(enc[1].count() - antes[1]), (long)(enc[2].count() - antes[2]));
}

int main() {
    for (MotorDriver& d : drv) d.init();   // drivers apagados lo antes posible
    stdio_init_all();
    for (Encoder& e : enc) e.init();

    sleep_ms(3000);
    printf("\nMover a mano: c+ c- (R1)  h+ h- (R2)  p+ p- (P)\n");
    int eje = -1;
    while (true) {
        const int c = getchar();
        if (c == 'c') eje = 0;
        else if (c == 'h') eje = 1;
        else if (c == 'p') eje = 2;
        else if ((c == '+' || c == '-') && eje >= 0) {
            empujar(eje, c == '+' ? DUTY : -DUTY);
            eje = -1;
        }
    }
}
