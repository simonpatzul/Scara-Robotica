// I2C maestro por software en GP4 (SCL) y GP5 (SDA), unos 100 kHz. Las pull-ups están en la placa:
// un 1 es soltar el pin (entrada) y un 0 es bajarlo. Respeta el clock stretching del STM32.
#include "enlace_stm32.h"
#include "pico/stdlib.h"

namespace {

constexpr uint PIN_SCL = 4;
constexpr uint PIN_SDA = 5;
constexpr uint8_t DIRECCION = 0x42;
constexpr uint32_t MEDIO_US = 5;            // medio periodo del reloj
constexpr uint32_t STRETCH_MAX_US = 2000;   // el STM32 puede sostener SCL abajo mientras atiende la interrupción

void soltar(uint pin) { gpio_set_dir(pin, GPIO_IN); }
void bajar(uint pin) { gpio_put(pin, 0); gpio_set_dir(pin, GPIO_OUT); }
void espera() { busy_wait_us_32(MEDIO_US); }

// Suelta SCL y espera a que suba (el esclavo puede estirarlo)
bool scl_arriba() {
    soltar(PIN_SCL);
    const absolute_time_t limite = make_timeout_time_us(STRETCH_MAX_US);
    while (!gpio_get(PIN_SCL)) {
        if (time_reached(limite)) return false;
    }
    return true;
}

bool inicio() {
    soltar(PIN_SDA);
    if (!scl_arriba()) return false;
    espera();
    bajar(PIN_SDA);
    espera();
    bajar(PIN_SCL);
    espera();
    return true;
}

void fin() {
    bajar(PIN_SDA);
    espera();
    scl_arriba();
    espera();
    soltar(PIN_SDA);
    espera();
}

bool poner_bit(bool b) {
    if (b) soltar(PIN_SDA); else bajar(PIN_SDA);
    espera();
    if (!scl_arriba()) return false;
    espera();
    bajar(PIN_SCL);
    return true;
}

bool leer_bit(bool& b) {
    soltar(PIN_SDA);
    espera();
    if (!scl_arriba()) return false;
    espera();
    b = gpio_get(PIN_SDA);
    bajar(PIN_SCL);
    return true;
}

// true si el esclavo contestó ACK
bool escribir_byte(uint8_t v) {
    for (int i = 7; i >= 0; --i) {
        if (!poner_bit((v >> i) & 1)) return false;
    }
    bool nack = true;
    return leer_bit(nack) && !nack;
}

bool leer_byte(uint8_t& v, bool ack) {
    v = 0;
    for (int i = 0; i < 8; ++i) {
        bool b = false;
        if (!leer_bit(b)) return false;
        v = static_cast<uint8_t>((v << 1) | (b ? 1 : 0));
    }
    return poner_bit(!ack);
}

// Si un esclavo quedó a medias con SDA abajo: 9 pulsos de reloj y un fin
void destrabar() {
    soltar(PIN_SDA);
    for (int i = 0; i < 9 && !gpio_get(PIN_SDA); ++i) {
        bajar(PIN_SCL);
        espera();
        scl_arriba();
        espera();
    }
    fin();
}

uint8_t suma(const uint8_t* b) {
    uint8_t s = 0;
    for (int i = 0; i < ENLACE_BYTES - 1; ++i) s = static_cast<uint8_t>(s + b[i]);
    return s;
}

void poner_i16(uint8_t* b, float v) {
    if (v > 32767.0f) v = 32767.0f;
    if (v < -32767.0f) v = -32767.0f;
    const int16_t x = static_cast<int16_t>(v >= 0.0f ? v + 0.5f : v - 0.5f);
    b[0] = static_cast<uint8_t>(x & 0xFF);
    b[1] = static_cast<uint8_t>((x >> 8) & 0xFF);
}

float leer_i16(const uint8_t* b) {
    return static_cast<float>(static_cast<int16_t>(b[0] | (b[1] << 8)));
}

}  // namespace

void enlace_init() {
    const uint pines[] = { PIN_SCL, PIN_SDA };
    for (uint pin : pines) {
        gpio_init(pin);
        gpio_disable_pulls(pin);   // las pull-ups son las de la placa (R3, R4)
        gpio_put(pin, 0);
        soltar(pin);
    }
    destrabar();
}

void enlace_estado(uint8_t out[ENLACE_BYTES], char sistema, const char est[3], const float pos[3],
                   uint8_t flags, const uint8_t falla[3]) {
    for (int i = 0; i < ENLACE_BYTES; ++i) out[i] = 0;
    out[0] = 'S';
    out[1] = static_cast<uint8_t>(sistema);
    for (int k = 0; k < 3; ++k) out[2 + k] = static_cast<uint8_t>(est[k]);
    poner_i16(&out[5], pos[0] * 1000.0f);
    poner_i16(&out[7], pos[1] * 1000.0f);
    poner_i16(&out[9], pos[2] * 100.0f);
    out[11] = flags;
    for (int k = 0; k < 3; ++k) out[12 + k] = falla[k];
    out[15] = suma(out);
}

bool enlace_intercambio(const uint8_t estado[ENLACE_BYTES], ComandoStm32& cmd) {
    // 1. Escribir el estado
    bool ok = inicio() && escribir_byte(DIRECCION << 1);
    for (int i = 0; ok && i < ENLACE_BYTES; ++i) ok = escribir_byte(estado[i]);
    fin();
    if (!ok) {
        destrabar();
        return false;
    }

    // 2. Leer el comando
    uint8_t rx[ENLACE_BYTES] = {0};
    ok = inicio() && escribir_byte((DIRECCION << 1) | 1);
    for (int i = 0; ok && i < ENLACE_BYTES; ++i) ok = leer_byte(rx[i], i < ENLACE_BYTES - 1);
    fin();
    if (!ok) {
        destrabar();
        return false;
    }
    if (rx[0] != 'C' || rx[15] != suma(rx)) return false;

    cmd.en = rx[1] & 0x07;
    cmd.numero = rx[2];
    cmd.tipo = static_cast<char>(rx[3]);
    cmd.valor[0] = leer_i16(&rx[4]) / 1000.0f;
    cmd.valor[1] = leer_i16(&rx[6]) / 1000.0f;
    cmd.valor[2] = leer_i16(&rx[8]) / 100.0f;
    return true;
}
