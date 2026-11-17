// Programa aparte para probar la bomba en GP22 y el servo en GP26. Los motores quedan APAGADOS.
// Por el puerto serie: '1' = prender bomba, '0' = apagar bomba. Responde "BOMBA=1" o "BOMBA=0".
//                      "S<grados>\n" (0..180) = mover servo. Responde "SERVO=<grados>".
// Desde el PC se usa bajo_nivel/pruebas/botones_bomba.py
#include <cstdio>
#include "pico/stdlib.h"
#include "hardware/pwm.h"

const uint PIN_BOMBA = 22;
const uint PIN_SERVO = 26;

// Enables de los drivers (R1: 7,6  R2: 3,2  P: 17,16) en bajo = motores apagados
const uint ENABLES[] = { 7, 6, 3, 2, 17, 16 };

// Servo: 50 Hz (20 ms). Con divisor 125 el contador va a 1 MHz, así el nivel del PWM queda en microsegundos.
void servo_init() {
    gpio_set_function(PIN_SERVO, GPIO_FUNC_PWM);
    const uint slice = pwm_gpio_to_slice_num(PIN_SERVO);
    pwm_set_clkdiv(slice, 125.0f);
    pwm_set_wrap(slice, 19999);
    pwm_set_gpio_level(PIN_SERVO, 0);   // sin pulso hasta la primera orden: el servo no se mueve al arrancar
    pwm_set_enabled(slice, true);
}

void servo_grados(int grados) {
    if (grados < 0) grados = 0;
    if (grados > 180) grados = 180;
    pwm_set_gpio_level(PIN_SERVO, 500 + grados * 2000 / 180);   // 0° = 500 us, 180° = 2500 us
    printf("SERVO=%d\n", grados);
}

int main() {
    for (uint pin : ENABLES) {
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_OUT);
        gpio_put(pin, 0);
    }
    gpio_init(PIN_BOMBA);
    gpio_set_dir(PIN_BOMBA, GPIO_OUT);
    gpio_put(PIN_BOMBA, 0);   // arranca apagada
    servo_init();
    stdio_init_all();

    int grados = -1;   // -1 = no se está leyendo un número del servo
    while (true) {
        const int c = getchar_timeout_us(100000);
        if (c == PICO_ERROR_TIMEOUT) continue;
        if (c == '1' || c == '0') {
            if (grados >= 0) { grados = grados * 10 + (c - '0'); continue; }
            gpio_put(PIN_BOMBA, c == '1');
            printf("BOMBA=%c\n", c);
        } else if (c == 'S') {
            grados = 0;
        } else if (grados >= 0 && c >= '2' && c <= '9') {
            grados = grados * 10 + (c - '0');
        } else if (grados >= 0 && (c == '\n' || c == '\r')) {
            servo_grados(grados);
            grados = -1;
        }
    }
}
