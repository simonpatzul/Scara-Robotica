/*
 * scara.c - placa de proteccion del SCARA (STM32F030C8)
 * Mide corrientes, protege los motores (solo R1 y R2) y publica por MQTT.
 * Habla con la Pico por I2C (enlace.c): le pasa los EN y los comandos, y publica su estado y posicion.
 * M1 = IN0+IN1, M2 = IN2+IN3, M3 = IN4+IN5
 */

#include "scara.h"
#include "../FC41/FC41.h"
#include "../timer/timer.h"
#include "../STATES/states.h"
#include "../enlace/enlace.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

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

// Alertas para scara/evento: se juntan aqui y sale una por envio (el FC41 publica de a una)
#define N_COLA 8
typedef struct { char eje[5]; char falla[13]; float i; } Evento;
static Evento cola[N_COLA];
static int cola_n = 0;

static void avisar(const char *eje, const char *falla, float i) {
	if (cola_n >= N_COLA) return;   // llena: se pierde (no deberia pasar a 2 por segundo)
	strncpy(cola[cola_n].eje, eje, sizeof(cola[0].eje) - 1);
	cola[cola_n].eje[sizeof(cola[0].eje) - 1] = 0;
	strncpy(cola[cola_n].falla, falla, sizeof(cola[0].falla) - 1);
	cola[cola_n].falla[sizeof(cola[0].falla) - 1] = 0;
	cola[cola_n].i = i;
	cola_n++;
}

// Comando para la Pico (lo lee por I2C): tipo 0 nada, 'M' ir a coordenadas, 'Q' secuencia, 'X' parar
static uint8_t cmd_numero = 0;
static char    cmd_tipo = 0;
static float   cmd_valor[3];

const char *nombre_eje[3]  = { "R1", "R2", "P" };
const char *nombre_falla[3] = { "ninguna", "sostenida", "pico" };

// Pines EN de los drivers
GPIO_TypeDef *en_puerto[3] = { M1_EN_GPIO_Port, M2_EN_GPIO_Port, M3_EN_GPIO_Port };
uint16_t      en_pin[3]    = { M1_EN_Pin,       M2_EN_Pin,       M3_EN_Pin };
char          en_on[3];             // 1 = EN prendido (lo cambian scara_init y scara/cmd)

static void poner_en(int k, char on) {
	HAL_GPIO_WritePin(en_puerto[k], en_pin[k], on ? GPIO_PIN_SET : GPIO_PIN_RESET);
	en_on[k] = on;
}


// Inicio: ADC con DMA, prender EN y medir el cero
void scara_init(void) {
	HAL_ADCEx_Calibration_Start(&hadc);
	HAL_ADC_Start_DMA(&hadc, (uint32_t *)adc, 6);

	for (int k = 0; k < 3; k++) {
		poner_en(k, 1);
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


// Lo que lee la Pico: EN de cada eje y el ultimo comando
static void actualizar_enlace(void) {
	uint8_t en = (en_on[0] ? 1 : 0) | (en_on[1] ? 2 : 0) | (en_on[2] ? 4 : 0);
	enlace_comando(en, cmd_numero, cmd_tipo, cmd_valor);
}


// Proteccion: corre cada 1 ms (fallas solo en R1 y R2)
void scara_proteccion(void) {
	static unsigned int ultimo_ms = 0;
	if (get_tick() == ultimo_ms) return;
	ultimo_ms = get_tick();
	actualizar_enlace();

	for (int k = 0; k < 3; k++) {
		// Corriente del motor en amperios, filtrada (quita el ruido del PWM)
		float cruda = (sin_offset(2 * k) + sin_offset(2 * k + 1)) * AMP_POR_CUENTA;
		if (cruda > TOPE_LECTURA_A) cruda = TOPE_LECTURA_A;
		filtrada[k] += (cruda - filtrada[k]) / FILTRO_N;
		corriente[k] = filtrada[k];
		suma_envio[k] += filtrada[k];

		if (falla[k] != 0) continue;
		// Solo se protegen las revolutas (R1, R2). P se mide y se publica, pero no entra en falla
		if (k == 2) continue;

		// Contar ms por encima de cada limite
		if (corriente[k] > LIMITE_SOSTENIDO_A) ms_sobre_3A[k]++;  else ms_sobre_3A[k] = 0;
		if (corriente[k] > LIMITE_PICO_A)      ms_sobre_45A[k]++; else ms_sobre_45A[k] = 0;

		if (ms_sobre_45A[k] >= TIEMPO_PICO_MS)          falla[k] = 2;
		else if (ms_sobre_3A[k] >= TIEMPO_SOSTENIDO_MS) falla[k] = 1;

		// Falla: apagar los 3 EN y avisar
		if (falla[k] != 0) {
#if PROTECCION_CORTA_EN
			for (int j = 0; j < 3; j++) {
				poner_en(j, 0);
			}
#endif
			avisar(nombre_eje[k], nombre_falla[(int)falla[k]], corriente[k]);
		}
	}
	n_envio++;
}


// Borrar la falla de un eje (vuelve a vigilar desde cero)
static void borrar_falla(int k) {
	falla[k] = 0;
	ms_sobre_3A[k] = 0;
	ms_sobre_45A[k] = 0;
}


// Numero decimal "-1.25" -> float. Devuelve donde termino, o 0 si no hay numero
// (en vez de strtof: con strtof el programa ya no cabe en los 64 KB)
static const char *leer_numero(const char *p, float *v) {
	char signo = 0, hay = 0;
	float x = 0.0f, escala = 1.0f;
	if (*p == '-' || *p == '+') signo = (*p++ == '-');
	while (*p >= '0' && *p <= '9') { x = x * 10.0f + (*p++ - '0'); hay = 1; }
	if (*p == '.') {
		p++;
		while (*p >= '0' && *p <= '9') { escala /= 10.0f; x += (*p++ - '0') * escala; hay = 1; }
	}
	if (!hay) return 0;
	*v = signo ? -x : x;
	return p;
}


// Comandos desde Grafana (scara/cmd), texto plano:
//   on / off       prender / apagar los 3 EN (on tambien borra las fallas)
//   on1..on3       prender un eje (1 = R1, 2 = R2, 3 = P)
//   off1..off3     apagar un eje
//   reset          borrar fallas sin tocar los EN
//   mv r1,r2,p     ir a esas coordenadas (rad, rad, mm): se lo pasa a la Pico
//   seq / stop     correr / parar la secuencia de la caja (la corre el PC de la Pico)
void scara_comandos(void) {
	if (!isNewMQTTReceive()) return;
	clearisNewMQTTReceive();
	char *c = states.mqtt.payload;

	if (strcmp(c, "on") == 0) {
		for (int k = 0; k < 3; k++) { borrar_falla(k); poner_en(k, 1); }
	} else if (strcmp(c, "off") == 0) {
		for (int k = 0; k < 3; k++) poner_en(k, 0);
	} else if (strcmp(c, "reset") == 0) {
		for (int k = 0; k < 3; k++) borrar_falla(k);
	} else if (strncmp(c, "on", 2) == 0 && c[2] >= '1' && c[2] <= '3' && c[3] == 0) {
		int k = c[2] - '1';
		borrar_falla(k);
		poner_en(k, 1);
	} else if (strncmp(c, "off", 3) == 0 && c[3] >= '1' && c[3] <= '3' && c[4] == 0) {
		poner_en(c[3] - '1', 0);
	} else if (strncmp(c, "mv ", 3) == 0) {
		const char *p = c + 3;
		float v[3];
		for (int k = 0; k < 3; k++) {
			p = leer_numero(p, &v[k]);
			if (p == 0 || *p != (k < 2 ? ',' : 0)) return;   // formato no valido: no se manda nada
			p++;
		}
		for (int k = 0; k < 3; k++) cmd_valor[k] = v[k];
		cmd_tipo = 'M';
		cmd_numero++;
	} else if (strcmp(c, "seq") == 0) {
		cmd_tipo = 'Q';
		cmd_numero++;
	} else if (strcmp(c, "stop") == 0) {
		cmd_tipo = 'X';
		cmd_numero++;
	}
	actualizar_enlace();
}


// Float a texto "1.25" (sin %f)
static int amperios_a_texto(char *dst, float a) {
	int centi = (int)(a * 100.0f + 0.5f);
	return sprintf(dst, "%d.%02d", centi / 100, centi % 100);
}


// Float con signo a texto con 'dec' decimales (sin %f)
static int numero_a_texto(char *dst, float v, int dec) {
	int escala = (dec == 3) ? 1000 : (dec == 2) ? 100 : 10;
	char signo = v < 0.0f;
	if (signo) v = -v;
	long n = (long)(v * escala + 0.5f);
	return sprintf(dst, "%s%ld.%0*ld", signo ? "-" : "", n / escala, dec, n % escala);
}


// Estado de la Pico y alertas cuando cambia (encoder, desconexion, ciclo)
static EstadoPico pico;
static char pico_ok = 0;

static const char *ciclo_actual(char hay_falla, char todos_off) {
	if (hay_falla) return "FALLA";
	if (!pico_ok) return "SIN_PICO";
	if (pico.sistema == 'F') return "FALLA";
	if (todos_off) return "APAGADO";
	if (pico.sistema == 'W') return "SIN_HOME";
	if (pico.sistema == 'H') return "HOME";
	if (pico.flags & 2) return "SECUENCIA";
	return "LISTO";
}

static void vigilar_pico(const char *ciclo) {
	static char ok_antes = 0;
	static uint8_t falla_antes[3];
	static char ciclo_antes[13] = "";

	if (pico_ok != ok_antes) avisar("PICO", pico_ok ? "conectada" : "desconectada", 0.0f);
	ok_antes = pico_ok;

	for (int k = 0; k < 3; k++) {
		uint8_t f = pico_ok ? pico.falla[k] : 0;
		if (f != 0 && f != falla_antes[k]) avisar(nombre_eje[k], f == 1 ? "encoder" : "pico", corriente[k]);
		falla_antes[k] = f;
	}

	if (strcmp(ciclo, ciclo_antes) != 0) {
		avisar("EST", ciclo, 0.0f);
		strncpy(ciclo_antes, ciclo, sizeof(ciclo_antes) - 1);
	}
}


// Publicar cada 500 ms
void scara_publicar(void) {
	static unsigned int ultimo_envio = 0;
	static char json[480];
	char amp[3][12];

	if (get_tick() - ultimo_envio < PERIODO_PUBLICAR_MS) return;
	ultimo_envio = get_tick();

	char hay_falla = falla[0] || falla[1] || falla[2];
	char todos_off = !en_on[0] && !en_on[1] && !en_on[2];
	pico_ok = enlace_estado(&pico);
	const char *ciclo = ciclo_actual(hay_falla, todos_off);
	vigilar_pico(ciclo);

	// Si hay alertas: mandar una por scara/evento
	if (cola_n > 0) {
		amperios_a_texto(amp[0], cola[0].i);
		sprintf(json, "{\"eje\":\"%s\",\"falla\":\"%s\",\"i\":%s}", cola[0].eje, cola[0].falla, amp[0]);
		publish("scara/evento", json);
		cola_n--;
		memmove(&cola[0], &cola[1], cola_n * sizeof(Evento));
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

	// est: F = falla, A = apagado (EN en 0), y si hay Pico, su estado (R listo, H home, D sin home, L subiendo)
	// falla: la del STM32 (corriente) o la de la Pico (encoder u otra)
	char est[3];
	const char *txt_falla[3];
	char pos[3][14];
	for (int k = 0; k < 3; k++) {
		char f_pico = pico_ok ? pico.falla[k] : 0;
		est[k] = (falla[k] || f_pico) ? 'F' : (!en_on[k] ? 'A' : (pico_ok ? pico.est[k] : 'R'));
		txt_falla[k] = falla[k] ? nombre_falla[(int)falla[k]] : (f_pico == 1 ? "encoder" : (f_pico ? "pico" : "ninguna"));
		numero_a_texto(pos[k], pico_ok ? pico.pos[k] : 0.0f, k < 2 ? 3 : 2);
	}
	char flags = pico_ok ? pico.flags : 0;

	sprintf(json,
		"{\"ciclo\":\"%s\",\"pico_ok\":%s,\"pc\":%s,\"seq\":%s,\"bomba\":%s,"
		"\"temp\":{\"pico\":0,\"stm32\":0},\"ejes\":["
		"{\"n\":\"R1\",\"est\":\"%c\",\"pos\":%s,\"i\":%s,\"falla\":\"%s\"},"
		"{\"n\":\"R2\",\"est\":\"%c\",\"pos\":%s,\"i\":%s,\"falla\":\"%s\"},"
		"{\"n\":\"P\",\"est\":\"%c\",\"pos\":%s,\"i\":%s,\"falla\":\"%s\"}]}",
		ciclo, pico_ok ? "true" : "false",
		(flags & 1) ? "true" : "false", (flags & 2) ? "true" : "false", (flags & 4) ? "true" : "false",
		est[0], pos[0], amp[0], txt_falla[0],
		est[1], pos[1], amp[1], txt_falla[1],
		est[2], pos[2], amp[2], txt_falla[2]);

	publish("scara/estado", json);
}
