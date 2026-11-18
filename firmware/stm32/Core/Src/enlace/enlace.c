/*
 * enlace.c - I2C1 esclavo con interrupciones (modo Listen de la HAL)
 * La Pico escribe 16 bytes (estado) -> se guardan si la trama es valida.
 * La Pico lee 16 bytes (comando)    -> se le manda lo que dejo enlace_comando().
 */

#include "enlace.h"
#include "main.h"
#include <string.h>

#define DIRECCION  0x42

I2C_HandleTypeDef hi2c1;

static uint8_t rx[ENLACE_BYTES];        // lo que esta escribiendo la Pico
static uint8_t tx[ENLACE_BYTES];        // lo que lee la Pico (se arma con las interrupciones apagadas)
static uint8_t ultimo[ENLACE_BYTES];    // ultima trama de estado valida
static volatile uint32_t ms_ultimo = 0;
static volatile char hay_estado = 0;

static uint8_t suma(const uint8_t *b) {
	uint8_t s = 0;
	for (int i = 0; i < ENLACE_BYTES - 1; i++) s += b[i];
	return s;
}

static void poner_i16(uint8_t *b, float v) {
	if (v > 32767.0f) v = 32767.0f;
	if (v < -32767.0f) v = -32767.0f;
	int16_t x = (int16_t)(v >= 0.0f ? v + 0.5f : v - 0.5f);
	b[0] = (uint8_t)(x & 0xFF);
	b[1] = (uint8_t)((x >> 8) & 0xFF);
}

static float leer_i16(const uint8_t *b) {
	return (float)(int16_t)(b[0] | (b[1] << 8));
}


// PB6/PB7 en AF1 (I2C1), colector abierto, sin pull-up interna (las de la placa)
void enlace_init(void) {
	GPIO_InitTypeDef g = {0};
	__HAL_RCC_GPIOB_CLK_ENABLE();
	__HAL_RCC_I2C1_CLK_ENABLE();
	g.Pin = GPIO_PIN_6 | GPIO_PIN_7;
	g.Mode = GPIO_MODE_AF_OD;
	g.Pull = GPIO_NOPULL;
	g.Speed = GPIO_SPEED_FREQ_HIGH;
	g.Alternate = GPIO_AF1_I2C1;
	HAL_GPIO_Init(GPIOB, &g);

	hi2c1.Instance = I2C1;
	hi2c1.Init.Timing = 0x00201D2B;            // 100 kHz con el I2C1 en HSI (8 MHz), el reloj por defecto
	hi2c1.Init.OwnAddress1 = DIRECCION << 1;
	hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
	hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
	hi2c1.Init.OwnAddress2 = 0;
	hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
	hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
	hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;   // puede estirar SCL mientras atiende (la Pico espera)
	HAL_I2C_Init(&hi2c1);
	HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE);

	float cero[3] = {0, 0, 0};
	enlace_comando(0x07, 0, 0, cero);

	HAL_NVIC_SetPriority(I2C1_IRQn, 1, 0);
	HAL_NVIC_EnableIRQ(I2C1_IRQn);
	HAL_I2C_EnableListen_IT(&hi2c1);
}


void enlace_comando(uint8_t en, uint8_t numero, char tipo, const float valor[3]) {
	uint8_t t[ENLACE_BYTES] = {0};
	t[0] = 'C';
	t[1] = en & 0x07;
	t[2] = numero;
	t[3] = (uint8_t)tipo;
	poner_i16(&t[4], valor[0] * 1000.0f);
	poner_i16(&t[6], valor[1] * 1000.0f);
	poner_i16(&t[8], valor[2] * 100.0f);
	t[15] = suma(t);

	__disable_irq();
	memcpy(tx, t, ENLACE_BYTES);
	__enable_irq();
}


char enlace_estado(EstadoPico *e) {
	uint8_t b[ENLACE_BYTES];
	__disable_irq();
	memcpy(b, ultimo, ENLACE_BYTES);
	uint32_t ms = ms_ultimo;
	char hay = hay_estado;
	__enable_irq();

	if (!hay) return 0;
	e->sistema = (char)b[1];
	for (int k = 0; k < 3; k++) e->est[k] = (char)b[2 + k];
	e->pos[0] = leer_i16(&b[5]) / 1000.0f;
	e->pos[1] = leer_i16(&b[7]) / 1000.0f;
	e->pos[2] = leer_i16(&b[9]) / 100.0f;
	e->flags = b[11];
	for (int k = 0; k < 3; k++) e->falla[k] = b[12 + k];
	return (HAL_GetTick() - ms) < ENLACE_SIN_PICO_MS;
}


// ---------- Interrupcion y callbacks de la HAL ----------

void I2C1_IRQHandler(void) {
	if (hi2c1.Instance->ISR & (I2C_FLAG_BERR | I2C_FLAG_ARLO | I2C_FLAG_OVR)) {
		HAL_I2C_ER_IRQHandler(&hi2c1);
	} else {
		HAL_I2C_EV_IRQHandler(&hi2c1);
	}
}

// La Pico puso la direccion: si escribe, recibir el estado; si lee, mandarle el comando
void HAL_I2C_AddrCallback(I2C_HandleTypeDef *h, uint8_t direccion, uint16_t addr) {
	(void)addr;
	if (direccion == I2C_DIRECTION_TRANSMIT) {
		HAL_I2C_Slave_Seq_Receive_IT(h, rx, ENLACE_BYTES, I2C_FIRST_AND_LAST_FRAME);
	} else {
		HAL_I2C_Slave_Seq_Transmit_IT(h, tx, ENLACE_BYTES, I2C_FIRST_AND_LAST_FRAME);
	}
}

void HAL_I2C_SlaveRxCpltCallback(I2C_HandleTypeDef *h) {
	(void)h;
	if (rx[0] == 'S' && rx[15] == suma(rx)) {
		memcpy(ultimo, rx, ENLACE_BYTES);
		ms_ultimo = HAL_GetTick();
		hay_estado = 1;
	}
}

// Fin de cada transaccion (STOP): volver a escuchar
void HAL_I2C_ListenCpltCallback(I2C_HandleTypeDef *h) {
	HAL_I2C_EnableListen_IT(h);
}

// NACK de la Pico al ultimo byte leido u otro error: volver a escuchar
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *h) {
	HAL_I2C_EnableListen_IT(h);
}
