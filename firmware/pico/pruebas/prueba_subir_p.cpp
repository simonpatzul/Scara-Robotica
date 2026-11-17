// Programa aparte para despegar el prismático de su final de carrera: al dar Enter sube P unos MM_SUBIR
// con VOLTS y se apaga. R1 y R2 quedan APAGADOS. Tiempo máximo TIMEOUT_MS por seguridad.
// Mismos pines que AXIS_P en config/axis_config.h (voltaje positivo = subir).
#include <cstdio>
#include <cstdlib>
#include "pico/stdlib.h"
#include "encoder.h"
#include "motor_driver.h"
#include "limit_switch.h"

constexpr float    MM_SUBIR     = 3.0f;
constexpr float    VOLTS        = 6.0f;
constexpr float    SUPPLY_VOLTS = 12.0f;
constexpr uint32_t TIMEOUT_MS   = 3000;
constexpr float    CUENTAS_MM   = 1600.0f;   // 3200 cuentas/vuelta, tornillo de 2 mm

Encoder     encoder(pio0, 2, 8, 9);
MotorDriver driver(14, 15, 17, 16, 1.0f);
LimitSwitch limit(21, true);

// Enables de R1 (7,6) y R2 (3,2) en bajo = motores apagados
const uint ENABLES[] = { 7, 6, 3, 2 };

int main() {
    for (uint pin : ENABLES) {
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_OUT);
        gpio_put(pin, 0);
    }
    stdio_init_all();
    driver.init();
    encoder.init();
    limit.init();

    sleep_ms(3000);   // tiempo para abrir el monitor serie
    printf("\nSubir P %.1f mm a %.1f V. limite=%d. Presiona ENTER.\n", MM_SUBIR, VOLTS, limit.pressed());
    int c;
    do { c = getchar(); } while (c != '\n' && c != '\r');

    encoder.reset();
    driver.enable(true);
    driver.set(VOLTS / SUPPLY_VOLTS);
    const absolute_time_t t0 = get_absolute_time();
    while (std::abs(encoder.count()) < MM_SUBIR * CUENTAS_MM &&
           absolute_time_diff_us(t0, get_absolute_time()) < TIMEOUT_MS * 1000) {
        sleep_ms(5);
    }
    driver.set(0.0f);
    driver.enable(false);
    printf("Listo: subio %.2f mm. limite=%d\n", std::abs(encoder.count()) / CUENTAS_MM, limit.pressed());
    while (true) sleep_ms(1000);
}
