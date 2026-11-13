/*
 * scara.c - placa de proteccion del SCARA (STM32F030C8)
 * Mide corrientes, protege los motores y publica por MQTT.
 * M1 = IN0+IN1, M2 = IN2+IN3, M3 = IN4+IN5
 */

#include "scara.h"
#include "../FC41/FC41.h"
#include "../timer/timer.h"
#include "main.h"
#include <stdio.h>

extern ADC_HandleTypeDef hadc;

// Conversion ADC -> amperios (~0.033 A por cuenta)
#define AMP_POR_CUENTA   (3.3f / 4095.0f / 470.0f * 19500.0f)

// Umbrales de proteccion
#define LIMITE_SOSTENIDO_A   3.0f
#define TIEMPO_SOSTENIDO_MS  500
#define LIMITE_PICO_A        4.5f
#define TIEMPO_PICO_MS       20

// Filtro: promedio movil exponencial (1 ms por muestra, ~16 ms de constante de tiempo)
#define FILTRO_N             16
// Tope de cada lectura antes del filtro: una lectura erronea del ADC (1 ms)
// no puede inflar la filtrada por encima del limite de pico
#define TOPE_LECTURA_A       6.0f

#define PERIODO_PUBLICAR_MS  500

// 1 = una falla apaga los EN.  0 = solo avisa por MQTT, los EN siguen prendidos
// (DESACTIVADA por ahora a pedido; volver a 1 cuando se arregle el driver del hombro)
#define PROTECCION_CORTA_EN  0

// Variables
uint16_t adc[6];
uint16_t offset[6];
float corriente[3];
char falla[3];                      // 0 ninguna, 1 sostenida, 2 pico
unsigned int ms_sobre_3A[3];
unsigned int ms_sobre_45A[3];
int eje_evento = -1;

const char *nombre_eje[3]  = { "R1", "R2", "P" };
const char *nombre_falla[3] = { "ninguna", "sostenida", "pico" };

// Pines EN de los drivers
GPIO_TypeDef *en_puerto[3] = { M1_EN_GPIO_Port, M2_EN_GPIO_Port, M3_EN_GPIO_Port };
uint16_t      en_pin[3]    = { M1_EN_Pin,       M2_EN_Pin,       M3_EN_Pin };


// Inicio: ADC con DMA, prender EN y medir el cero
void scara_init(void) {
	HAL_ADCEx_Calibration_Start(&hadc);
	HAL_ADC_Start_DMA(&hadc, (uint32_t *)adc, 6);

	for (int k = 0; k < 3; k++) {
		HAL_GPIO_WritePin(en_puerto[k], en_pin[k], GPIO_PIN_SET);
	}

	// Cero: promedio de 50 lecturas con motores quietos
	HAL_Delay(100);
	uint32_t suma[6] = {0};
	for (int n = 0; n < 50; n++) {
		for (int c = 0; c < 6; c++) suma[c] += adc[c];
		HAL_Delay(2);
	}
	for (int c = 0; c < 6; c++) offset[c] = suma[c] / 50;
}


// Corriente filtrada y suma para el promedio de cada envio
float filtrada[3];
float suma_envio[3];
unsigned int n_envio = 0;

// Lectura menos el cero
static uint16_t sin_offset(int c) {
	return (adc[c] > offset[c]) ? (adc[c] - offset[c]) : 0;
}


// Proteccion: corre cada 1 ms
void scara_proteccion(void) {
	static unsigned int ultimo_ms = 0;
	if (get_tick() == ultimo_ms) return;
	ultimo_ms = get_tick();

	for (int k = 0; k < 3; k++) {
		// Corriente del motor en amperios, filtrada (quita el ruido del PWM)
		float cruda = (sin_offset(2 * k) + sin_offset(2 * k + 1)) * AMP_POR_CUENTA;
		if (cruda > TOPE_LECTURA_A) cruda = TOPE_LECTURA_A;
		filtrada[k] += (cruda - filtrada[k]) / FILTRO_N;
		corriente[k] = filtrada[k];
		suma_envio[k] += filtrada[k];

		if (falla[k] != 0) continue;

		// Contar ms por encima de cada limite
		if (corriente[k] > LIMITE_SOSTENIDO_A) ms_sobre_3A[k]++;  else ms_sobre_3A[k] = 0;
		if (corriente[k] > LIMITE_PICO_A)      ms_sobre_45A[k]++; else ms_sobre_45A[k] = 0;

		if (ms_sobre_45A[k] >= TIEMPO_PICO_MS)          falla[k] = 2;
		else if (ms_sobre_3A[k] >= TIEMPO_SOSTENIDO_MS) falla[k] = 1;

		// Falla: apagar los 3 EN y avisar
		if (falla[k] != 0) {
#if PROTECCION_CORTA_EN
			for (int j = 0; j < 3; j++) {
				HAL_GPIO_WritePin(en_puerto[j], en_pin[j], GPIO_PIN_RESET);
			}
#endif
			eje_evento = k;
		}
	}
	n_envio++;
}


// Float a texto "1.25" (sin %f)
static int amperios_a_texto(char *dst, float a) {
	int centi = (int)(a * 100.0f + 0.5f);
	return sprintf(dst, "%d.%02d", centi / 100, centi % 100);
}


// Publicar cada 500 ms
void scara_publicar(void) {
	static unsigned int ultimo_envio = 0;
	static char json[400];
	char amp[3][12];

	if (get_tick() - ultimo_envio < PERIODO_PUBLICAR_MS) return;
	ultimo_envio = get_tick();

	// Si hubo falla: mandar scara/evento
	if (eje_evento >= 0) {
		int k = eje_evento;
		eje_evento = -1;
		amperios_a_texto(amp[0], corriente[k]);
		sprintf(json, "{\"eje\":\"%s\",\"falla\":\"%s\",\"i\":%s}",
				nombre_eje[k], nombre_falla[(int)falla[k]], amp[0]);
		publish("scara/evento", json);
		return;
	}

	// Si no: mandar scara/estado (formato de server.js; sin Pico: pos y temp en 0)
	// Corriente = promedio de los ultimos 500 ms (no una sola muestra)
	for (int k = 0; k < 3; k++) {
		float prom = (n_envio > 0) ? suma_envio[k] / n_envio : corriente[k];
		amperios_a_texto(amp[k], prom);
		suma_envio[k] = 0;
	}
	n_envio = 0;
	char hay_falla = falla[0] || falla[1] || falla[2];

	sprintf(json,
		"{\"ciclo\":\"%s\",\"pico_ok\":false,\"temp\":{\"pico\":0,\"stm32\":0},\"ejes\":["
		"{\"n\":\"R1\",\"est\":\"%c\",\"pos\":0,\"i\":%s,\"falla\":\"%s\"},"
		"{\"n\":\"R2\",\"est\":\"%c\",\"pos\":0,\"i\":%s,\"falla\":\"%s\"},"
		"{\"n\":\"P\",\"est\":\"%c\",\"pos\":0,\"i\":%s,\"falla\":\"%s\"}]}",
		hay_falla ? "FALLA" : "LISTO",
		falla[0] ? 'F' : 'R', amp[0], nombre_falla[(int)falla[0]],
		falla[1] ? 'F' : 'R', amp[1], nombre_falla[(int)falla[1]],
		falla[2] ? 'F' : 'R', amp[2], nombre_falla[(int)falla[2]]);

	publish("scara/estado", json);
}
