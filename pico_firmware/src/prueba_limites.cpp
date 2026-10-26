// Programa aparte para revisar los finales de carrera. Los motores quedan APAGADOS.
// Cada 100 ms imprime el estado de los 3 interruptores (1 = presionado) y avisa cuando uno cambia.
// Presiona cada final de carrera con la mano y mira que cambie el que corresponde.
#include <cstdio>
#include "pico/stdlib.h"
#include "limit_switch.h"

// Mismos pines que config/axis_config.h (todos activos en bajo)
LimitSwitch lim_r1(19, true);
LimitSwitch lim_r2(21, true);
LimitSwitch lim_p(20, true);

// Enables de los drivers (R1: 7,6  R2: 3,2  P: 17,16) en bajo = motores apagados
const uint ENABLES[] = { 7, 6, 3, 2, 17, 16 };

int main() {
    for (uint pin : ENABLES) {
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_OUT);
        gpio_put(pin, 0);
    }
    stdio_init_all();
    lim_r1.init();
    lim_r2.init();
    lim_p.init();

    sleep_ms(3000);   // tiempo para abrir el monitor serie
    printf("\nPrueba de finales de carrera (motores apagados). 1 = presionado\n");

    bool antes[3] = { false, false, false };
    const char* nombres[3] = { "R1 (GP19)", "R2 (GP21)", "P (GP20)" };
    while (true) {
        const bool ahora[3] = { lim_r1.pressed(), lim_r2.pressed(), lim_p.pressed() };
        printf("R1=%d  R2=%d  P=%d\n", ahora[0], ahora[1], ahora[2]);
        for (int i = 0; i < 3; ++i) {
            if (ahora[i] != antes[i]) printf(">>> %s %s\n", nombres[i], ahora[i] ? "PRESIONADO" : "suelto");
            antes[i] = ahora[i];
        }
        sleep_ms(100);
    }
}
