#pragma once
// Enlace I2C con el STM32 de la placa (PCB scara_v2: GP4 = SCL, GP5 = SDA -> PB6 / PB7, pull-ups de 5.1k a 3.3 V).
// La Pico es el maestro (por software: GP4/GP5 están cruzados respecto al I2C del RP2040) y el STM32 el esclavo 0x42.
// Cada PERIODO_MS la Pico escribe su estado (16 bytes) y lee los comandos (16 bytes). Todo lo que va a internet
// sale por el FC41 del STM32: la Pico no se conecta a nada más.
//
// Estado (Pico -> STM32)                       Comando (STM32 -> Pico)
//  0  'S'                                       0  'C'
//  1  sistema: W espera home, H home,           1  EN: bit 0 = R1, bit 1 = R2, bit 2 = P (1 = prendido)
//     R listo, F falla                          2  número de comando (cambia con cada comando nuevo)
//  2-4 estado de R1, R2, P:                     3  tipo: 0 nada, 'M' ir a coordenadas, 'Q' secuencia, 'X' parar
//     R listo, H home, F falla, A EN apagado,   4-9 int16 de 'M': R1, R2 (mrad) y P (centésimas de mm)
//     L subiendo, D sin home                    10-14 0
//  5-10 int16 posición: R1, R2 (mrad), P (centésimas de mm)
//  11 bit 0 = PC conectado, bit 1 = secuencia corriendo, bit 2 = bomba
//  12-14 falla de R1, R2, P: 0 ninguna, 1 encoder, 2 otra
//  15 suma de los bytes 0-14 (en los dos)
#include <cstdint>

constexpr int ENLACE_BYTES = 16;
constexpr uint32_t ENLACE_PERIODO_MS = 50;
constexpr uint32_t ENLACE_SIN_STM32_MS = 1000;   // sin respuesta este tiempo: STM32 desconectado

struct ComandoStm32 {
    uint8_t en = 0x07;      // hasta tener enlace se suponen prendidos (así funciona con un STM32 sin I2C)
    uint8_t numero = 0;
    char tipo = 0;
    float valor[3] = {0, 0, 0};   // de 'M': rad, rad, mm
};

void enlace_init();
// Un intercambio: manda 'estado' y llena 'cmd'. Devuelve false si el STM32 no contestó o la trama vino mal
bool enlace_intercambio(const uint8_t estado[ENLACE_BYTES], ComandoStm32& cmd);
// Arma la trama de estado
void enlace_estado(uint8_t out[ENLACE_BYTES], char sistema, const char est[3], const float pos[3],
                   uint8_t flags, const uint8_t falla[3]);
