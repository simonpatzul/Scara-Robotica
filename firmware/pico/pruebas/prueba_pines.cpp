// Programa aparte para revisar cada pin de encoder y de final de carrera por separado. Los motores quedan APAGADOS.
// Cuenta los flancos de cada pin con interrupciones (no se pierde un pulso corto) y cada 500 ms imprime los totales.
// Un canal de encoder que no cambia al girar el eje queda en 0; un final que no se lee al apretarlo, también.
#include <cstdio>
#include "pico/stdlib.h"

// Encoders: R1 codo 12/13, R2 hombro 10/11, P 14/15. Finales: P 17 y 20, codo 18, hombro 19 (activos en bajo, con pull-up como en scara_fw)
const uint PINES[] = { 12, 13, 10, 11, 14, 15, 16, 17, 18, 19, 20, 21, 22 };
const char* NOMBRES[] = { "codoA", "codoB", "hombA", "hombB", "P_A", "P_B", "fin16", "fin17", "fin18", "fin19", "fin20", "fin21", "fin22" };
constexpr int N = 13;
volatile uint32_t flancos[32];

// Entradas de los puentes en bajo = motores quietos (los EN los maneja el STM32): M1 0/1, M2 2/3, M3 6/7
const uint MOTORES[] = { 0, 1, 2, 3, 6, 7 };

void al_cambiar(uint gpio, uint32_t) { flancos[gpio]++; }

int main() {
    for (uint pin : MOTORES) {
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_OUT);
        gpio_put(pin, 0);
    }
    stdio_init_all();
    for (int i = 0; i < N; ++i) {
        gpio_init(PINES[i]);
        gpio_set_dir(PINES[i], GPIO_IN);
        if (PINES[i] >= 16) gpio_pull_up(PINES[i]);
        else gpio_disable_pulls(PINES[i]);   // encoders sin pull (como INPUT en scara_rst.ino): el TXS0108E ya trae los suyos
        gpio_set_irq_enabled_with_callback(PINES[i], GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &al_cambiar);
    }

    sleep_ms(3000);   // tiempo para abrir el monitor serie
    printf("\nPrueba de pines (motores apagados): flancos acumulados de cada pin y su nivel actual.\n");
    while (true) {
        for (int i = 0; i < N; ++i)
            printf("%s=%lu(%d) ", NOMBRES[i], (unsigned long)flancos[PINES[i]], gpio_get(PINES[i]));
        printf("\n");
        sleep_ms(500);
    }
}
