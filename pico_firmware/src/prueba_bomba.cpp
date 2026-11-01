// Programa aparte para probar la bomba en GP22. Los motores quedan APAGADOS.
// Por el puerto serie: '1' = prender bomba, '0' = apagar bomba. Responde "BOMBA=1" o "BOMBA=0".
// Desde el PC se usa herramientas/botones_bomba.py
#include <cstdio>
#include "pico/stdlib.h"

const uint PIN_BOMBA = 22;

// Enables de los drivers (R1: 7,6  R2: 3,2  P: 17,16) en bajo = motores apagados
const uint ENABLES[] = { 7, 6, 3, 2, 17, 16 };

int main() {
    for (uint pin : ENABLES) {
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_OUT);
        gpio_put(pin, 0);
    }
    gpio_init(PIN_BOMBA);
    gpio_set_dir(PIN_BOMBA, GPIO_OUT);
    gpio_put(PIN_BOMBA, 0);   // arranca apagada
    stdio_init_all();

    while (true) {
        const int c = getchar_timeout_us(100000);
        if (c == '1' || c == '0') {
            gpio_put(PIN_BOMBA, c == '1');
            printf("BOMBA=%c\n", c);
        }
    }
}
