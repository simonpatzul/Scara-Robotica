/*
 * enlace.h - I2C con la Pico (PCB scara_v2: PB6 = I2C1_SCL, PB7 = I2C1_SDA, pull-ups R3/R4 de 5.1k)
 * El STM32 es el esclavo 0x42; la Pico es el maestro y cada 50 ms escribe su estado (16 bytes)
 * y lee los comandos (16 bytes). Las tramas estan en firmware/pico/config/enlace_stm32.h.
 * Va aparte de main.c para que CubeMX no lo borre al regenerar.
 */

#ifndef SRC_ENLACE_ENLACE_H_
#define SRC_ENLACE_ENLACE_H_

#include <stdint.h>

#define ENLACE_BYTES        16
#define ENLACE_SIN_PICO_MS  1000   // sin trama valida este tiempo: Pico desconectada

typedef struct {
	char    sistema;     // W espera home, H home, R listo, F falla
	char    est[3];      // R listo, H home, F falla, A EN apagado, L subiendo, D sin home
	float   pos[3];      // R1, R2 (rad) y P (mm)
	uint8_t flags;       // bit 0 PC conectado, bit 1 secuencia corriendo, bit 2 bomba
	uint8_t falla[3];    // 0 ninguna, 1 encoder, 2 otra
} EstadoPico;

void enlace_init(void);
// Lo que lee la Pico en el siguiente intercambio. tipo: 0 nada, 'M' ir a coordenadas, 'Q' secuencia, 'X' parar
void enlace_comando(uint8_t en, uint8_t numero, char tipo, const float valor[3]);
// 1 si llego una trama valida en los ultimos ENLACE_SIN_PICO_MS; copia el ultimo estado en *e
char enlace_estado(EstadoPico *e);

#endif /* SRC_ENLACE_ENLACE_H_ */
