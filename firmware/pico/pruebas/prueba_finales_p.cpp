// Programa aparte para saber qué pin es el final de arriba y cuál el de abajo del prismático.
// Al dar Enter sube P con VOLTS hasta que se apriete un final, y luego baja hasta que se apriete otro.
// Cada tramo se para en el primer final que se aprieta, si se traba o a los TIMEOUT_MS. R1 y R2 quedan APAGADOS.
// Pines de la PCB nueva (AXIS_P en config/axis_config.h): voltaje positivo = subir.
#include <cstdio>
#include <cstdlib>
#include "pico/stdlib.h"
#include "encoder.h"
#include "motor_driver.h"

constexpr float    VOLTS        = 6.0f;
constexpr float    SUPPLY_VOLTS = 12.0f;
constexpr uint32_t TIMEOUT_MS   = 25000;   // a 6 V baja unos 3.5 mm/s: 50 mm de recorrido = 15 s
constexpr float    CUENTAS_MM   = 1600.0f;   // 3200 cuentas/vuelta, tornillo de 2 mm
constexpr int      LECTURAS     = 3;         // 3 lecturas seguidas (15 ms) para aceptar un final: filtra ruido

Encoder     encoder(pio0, 2, 14, 15);
MotorDriver driver(7, 6, 0xFF, 0xFF, 1.0f);   // M3_INA = GP7, M3_INB = GP6; los EN los maneja el STM32

// Candidatos a final de carrera (activos en bajo, con pull-up)
const uint FINALES[] = { 16, 17, 18, 19, 20, 21, 22 };
constexpr int N = 7;
// Entradas de R1 (0,1) y R2 (2,3) en bajo = motores quietos
const uint QUIETOS[] = { 0, 1, 2, 3 };

float mm() { return -encoder.count() / CUENTAS_MM; }   // encoder_sign = -1: positivo = arriba

// Mueve P en el sentido de 'volts' hasta que se apriete un final que estaba suelto al empezar. Devuelve su pin, o -1
int mover(float volts, const char* nombre) {
    bool antes[N];
    int seguidas[N] = {};
    for (int i = 0; i < N; ++i) antes[i] = !gpio_get(FINALES[i]);
    const float inicio = mm();
    float revisado = inicio;
    driver.enable(true);
    driver.set(volts / SUPPLY_VOLTS);
    const absolute_time_t t0 = get_absolute_time();
    int pin = -1;
    const char* motivo = "tiempo maximo";
    for (uint32_t ms = 0; ms < TIMEOUT_MS; ms += 5) {
        for (int i = 0; i < N; ++i) {
            seguidas[i] = (!antes[i] && !gpio_get(FINALES[i])) ? seguidas[i] + 1 : 0;
            if (seguidas[i] >= LECTURAS) { pin = FINALES[i]; break; }
        }
        if (pin >= 0) { motivo = "final"; break; }
        // Desde el primer segundo, cada 0.5 s debe avanzar algo: si no, topó o se trabó
        if (ms >= 1000 && ms % 500 == 0) {
            if (std::abs(mm() - revisado) < 0.2f) { motivo = "se trabo (no avanza)"; break; }
            revisado = mm();
        }
        sleep_until(delayed_by_ms(t0, ms + 5));
    }
    driver.set(0.0f);
    driver.enable(false);
    if (pin >= 0) printf("%s: final en GP%d, despues de %.1f mm\n", nombre, pin, mm() - inicio);
    else printf("%s: sin final (%s), despues de %.1f mm\n", nombre, motivo, mm() - inicio);
    return pin;
}

int main() {
    for (uint pin : QUIETOS) {
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_OUT);
        gpio_put(pin, 0);
    }
    stdio_init_all();
    driver.init();
    encoder.init();
    for (uint pin : FINALES) {
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_IN);
        gpio_pull_up(pin);
    }

    sleep_ms(3000);   // tiempo para abrir el monitor serie
    printf("\nFinales de P: sube hasta un final y baja hasta otro, a %.1f V. Apretados ahora:", VOLTS);
    for (uint pin : FINALES) if (!gpio_get(pin)) printf(" GP%u", pin);
    printf(". Presiona ENTER.\n");
    int c;
    do { c = getchar(); } while (c != '\n' && c != '\r');

    encoder.reset();
    const int arriba = mover(VOLTS, "SUBIR");
    sleep_ms(1000);
    const int abajo = mover(-VOLTS, "BAJAR");

    if (arriba >= 0 && abajo >= 0 && arriba != abajo) printf("RESULTADO: arriba = GP%d, abajo = GP%d\n", arriba, abajo);
    else if (arriba >= 0 && arriba == abajo) printf("RESULTADO: el mismo pin GP%d en los dos extremos\n", arriba);
    else printf("RESULTADO: incompleto (arriba = %d, abajo = %d; -1 = sin final)\n", arriba, abajo);
    while (true) sleep_ms(1000);
}
