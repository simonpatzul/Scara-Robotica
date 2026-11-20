/*
 * scara_simple.c - Programa del STM32 del SCARA (version simple)
 *
 * Hace 3 cosas:
 *   1. Mide la corriente de los 3 motores (ADC + DMA).
 *   2. Cada 0.5 s la publica por MQTT en "scara/estado" usando el modulo Wi-Fi FC41D.
 *      Si un motor pasa de LIMITE_A, publica una alerta en "scara/evento".
 *   3. Escucha "scara/cmd":  "off" apaga los 3 drivers (PARAR)  y  "on" los prende.
 *
 * El FC41D se maneja con comandos AT por la USART1 (PA9/PA10).
 * Los EN de los drivers son PB12, PB13 y PB14. El CEN del FC41D es PB8.
 */

#include "main.h"
#include <stdio.h>
#include <string.h>

// ===== Configuracion (cambiar aqui) =====
#define WIFI_RED     "Simon"
#define WIFI_CLAVE   "12345678"
#define BROKER_IP    "10.206.63.20"    // IP del PC con Mosquitto
#define LIMITE_A     3.0f               // alerta si un motor pasa de 3 A
// Conversion del ADC a amperios: 3.3 V / 4095 cuentas / 470 ohm * 19500 (ganancia del sensor del IFX007T)
#define AMP_POR_CUENTA  (3.3f / 4095.0f / 470.0f * 19500.0f)

extern ADC_HandleTypeDef hadc;
extern UART_HandleTypeDef huart1;

// ===== 1. Corriente =====
static uint16_t adc[6];        // el DMA llena esto solo: 2 canales por motor
static uint16_t cero[6];       // lectura con los motores quietos (se mide al arrancar)

// Corriente de un motor en amperios (motor 0, 1 o 2)
static float corriente(int motor) {
	int a = adc[2 * motor]     - cero[2 * motor];
	int b = adc[2 * motor + 1] - cero[2 * motor + 1];
	if (a < 0) a = 0;
	if (b < 0) b = 0;
	return (a + b) * AMP_POR_CUENTA;
}

// ===== 2. EN de los drivers =====
static int motores_on = 0;

static void poner_motores(int on) {
	GPIO_PinState v = on ? GPIO_PIN_SET : GPIO_PIN_RESET;
	HAL_GPIO_WritePin(GPIOB, M1_EN_Pin | M2_EN_Pin | M3_EN_Pin, v);
	motores_on = on;
}

// ===== 3. Modulo Wi-Fi FC41D (comandos AT) =====
// Lo que manda el FC41D llega por interrupcion, byte a byte, a este buffer circular
static uint8_t byte_rx;
static char rx[256];
static volatile uint16_t rx_entra = 0;
static uint16_t rx_sale = 0;

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
	rx[rx_entra] = byte_rx;
	rx_entra = (rx_entra + 1) % sizeof(rx);
	HAL_UART_Receive_IT(&huart1, &byte_rx, 1);
}

// Arma lineas con lo que llega. Devuelve 1 cuando hay una linea completa en 'linea'
static char linea[200];
static int largo = 0;

static int leer_linea(void) {
	while (rx_sale != rx_entra) {
		char c = rx[rx_sale];
		rx_sale = (rx_sale + 1) % sizeof(rx);
		if (c == '\n') {
			linea[largo] = 0;
			largo = 0;
			return 1;
		}
		if (c != '\r' && largo < (int)sizeof(linea) - 1) linea[largo++] = c;
	}
	return 0;
}

static void enviar(const char *texto) {
	HAL_UART_Transmit(&huart1, (uint8_t *)texto, strlen(texto), 1000);
}

// Manda un comando AT y espera una respuesta que contenga 'espera'. Devuelve 1 si llego
static int comando_at(const char *cmd, const char *espera, uint32_t ms) {
	enviar(cmd);
	enviar("\r\n");
	uint32_t inicio = HAL_GetTick();
	while (HAL_GetTick() - inicio < ms) {
		if (leer_linea()) {
			if (strstr(linea, espera)) return 1;
			if (strstr(linea, "ERROR")) return 0;
		}
	}
	return 0;
}

// Prende el FC41D, se conecta al Wi-Fi, al broker y se suscribe a scara/cmd
static int conectar(void) {
	HAL_GPIO_WritePin(CEN_GPIO_Port, CEN_Pin, GPIO_PIN_RESET);   // reiniciar el modulo
	HAL_Delay(200);
	HAL_GPIO_WritePin(CEN_GPIO_Port, CEN_Pin, GPIO_PIN_SET);
	HAL_Delay(3000);

	if (!comando_at("AT", "OK", 1000)) return 0;
	if (!comando_at("AT+QSTAAPINFO=" WIFI_RED "," WIFI_CLAVE, "GOT_IP", 20000)) return 0;
	if (!comando_at("AT+QMTCFG=\"version\",1,4", "OK", 2000)) return 0;
	if (!comando_at("AT+QMTOPEN=1,\"" BROKER_IP "\",1883", "+QMTOPEN: 1,0", 15000)) return 0;
	if (!comando_at("AT+QMTCONN=1,\"scara_stm32\",\"\",\"\"", "+QMTCONN: 1,0,0", 10000)) return 0;
	if (!comando_at("AT+QMTSUB=1,1,\"scara/cmd\",1", "+QMTSUB: 1,1,0", 10000)) return 0;
	return 1;
}

// Publica un mensaje en un topico
static void publicar(const char *topico, const char *msg) {
	char cmd[220];
	sprintf(cmd, "AT+QMTPUB=1,1,1,0,\"%s\",%d,\"%s\"\r\n", topico, (int)strlen(msg), msg);
	enviar(cmd);
}

// Amperios a texto "1.25" (printf con %f no cabe en la memoria)
static void amperios(char *dst, float a) {
	int centesimas = (int)(a * 100.0f + 0.5f);
	sprintf(dst, "%d.%02d", centesimas / 100, centesimas % 100);
}


// ===== Programa principal (lo llama main.c) =====
void scara_simple(void) {
	// 1. Prender los drivers y medir el cero de la corriente con los motores quietos
	//    (con el EN prendido: el sensor del IFX007T cambia su nivel de reposo al activarse)
	poner_motores(1);
	HAL_ADCEx_Calibration_Start(&hadc);
	HAL_ADC_Start_DMA(&hadc, (uint32_t *)adc, 6);
	HAL_Delay(100);
	uint32_t suma[6] = {0};
	for (int n = 0; n < 50; n++) {
		for (int c = 0; c < 6; c++) suma[c] += adc[c];
		HAL_Delay(2);
	}
	for (int c = 0; c < 6; c++) cero[c] = suma[c] / 50;

	// 2. Conectarse a MQTT (reintenta hasta lograrlo)
	HAL_UART_Receive_IT(&huart1, &byte_rx, 1);
	while (!conectar()) { }

	const char *nombre[3] = { "R1", "R2", "P" };
	int alerta_dada[3] = { 0, 0, 0 };
	float suma_i[3] = { 0, 0, 0 };
	int muestras = 0;
	uint32_t ultimo_ms = HAL_GetTick();
	uint32_t ultimo_envio = HAL_GetTick();

	while (1) {
		// a) Cada 1 ms: sumar la corriente (para mandar el promedio de 0.5 s)
		if (HAL_GetTick() != ultimo_ms) {
			ultimo_ms = HAL_GetTick();
			for (int m = 0; m < 3; m++) suma_i[m] += corriente(m);
			muestras++;
		}

		// b) Mensajes del FC41D: comandos de scara/cmd o conexion perdida
		if (leer_linea()) {
			if (strstr(linea, "+QMTRECV:")) {
				if (strstr(linea, "\"off\"")) poner_motores(0);    // PARAR
				else if (strstr(linea, "\"on\"")) poner_motores(1);
			}
			if (strstr(linea, "+QMTSTAT:")) {                     // se cayo el broker: reconectar
				while (!conectar()) { }
			}
		}

		// c) Cada 500 ms: publicar la corriente y las alertas
		if (HAL_GetTick() - ultimo_envio >= 500) {
			ultimo_envio = HAL_GetTick();
			char i[3][10], msg[120];
			float prom[3];
			for (int m = 0; m < 3; m++) {
				prom[m] = suma_i[m] / muestras;
				amperios(i[m], prom[m]);
				suma_i[m] = 0;
			}
			muestras = 0;

			// Alerta: una sola vez cada vez que un motor pasa el limite
			for (int m = 0; m < 3; m++) {
				if (prom[m] > LIMITE_A && !alerta_dada[m]) {
					sprintf(msg, "{\"eje\":\"%s\",\"falla\":\"alta\",\"i\":%s}", nombre[m], i[m]);
					publicar("scara/evento", msg);
					HAL_Delay(50);
				}
				alerta_dada[m] = prom[m] > LIMITE_A;
			}

			sprintf(msg, "{\"on\":%d,\"i1\":%s,\"i2\":%s,\"i3\":%s}", motores_on, i[0], i[1], i[2]);
			publicar("scara/estado", msg);
		}
	}
}
