/*
 * broker.c - (SCARA) lista de IPs donde buscar Mosquitto y la ultima que funciono, guardada en la flash
 * La ultima pagina de la flash (1 KB en 0x0800FC00) queda para esto: el .ld deja 63 KB al programa.
 */

#include "broker.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

#define DIR_GUARDADA   0x0800FC00u
#define MARCA          0x53434152u          // "SCAR": la pagina tiene una IP valida

typedef struct {
	uint32_t marca;
	char     ip[16];
} Guardada;

static char guardada[16];     // de la flash
static char por_defecto[16];  // DEFAULT_IP
static char puerta[16];       // puerta de enlace
static char propia[16];       // la del FC41
static char base[12];         // "192.168.43." de la subred (vacia si no se sabe)
static int  paso = 0;         // 0 guardada, 1 por defecto, 2 puerta, 3..256 = base + (paso - 2)
static char actual[16];


static void leer_flash(void) {
	const Guardada *g = (const Guardada *)DIR_GUARDADA;
	guardada[0] = 0;
	if (g->marca == MARCA && memchr(g->ip, 0, sizeof(g->ip)) != 0) strcpy(guardada, g->ip);
}

static void copiar(char *dst, const char *src) {
	strncpy(dst, src ? src : "", 15);
	dst[15] = 0;
}

// IP del paso 'p', o vacia si ese paso no aplica
static void ip_del_paso(int p, char *dst) {
	dst[0] = 0;
	if (p == 0) strcpy(dst, guardada);
	else if (p == 1) strcpy(dst, por_defecto);
	else if (p == 2) strcpy(dst, puerta);
	else if (base[0] != 0 && p <= 256) sprintf(dst, "%s%d", base, p - 2);
}

// La IP del paso 'p' ya se probo antes (o es la propia): saltarla
static char repetida(int p, const char *ip) {
	char otra[16];
	if (ip[0] == 0 || strcmp(ip, propia) == 0) return 1;
	for (int q = 0; q < p && q < 3; q++) {
		ip_del_paso(q, otra);
		if (strcmp(ip, otra) == 0) return 1;
	}
	return 0;
}


void broker_inicio(const char *mi_ip, const char *gw, const char *ip_default) {
	leer_flash();
	copiar(por_defecto, ip_default);
	copiar(puerta, gw);
	copiar(propia, mi_ip);

	// Subred: los 3 primeros numeros de la IP propia (se barre de .1 a .254 aunque la mascara sea mas grande)
	base[0] = 0;
	const char *ultimo_punto = strrchr(propia, '.');
	if (ultimo_punto != 0 && ultimo_punto - propia < (int)sizeof(base) - 1) {
		int n = ultimo_punto - propia + 1;
		memcpy(base, propia, n);
		base[n] = 0;
	}

	paso = -1;
	broker_siguiente();
}

const char *broker_actual(void) {
	return actual;
}

char broker_siguiente(void) {
	char vuelta = 1;
	for (int intentos = 0; intentos < 260; intentos++) {
		paso++;
		if (paso > 256) {
			paso = 0;
			vuelta = 0;
		}
		ip_del_paso(paso, actual);
		if (!repetida(paso, actual)) return vuelta;
	}
	// Nada que probar (sin IP propia ni guardada): usar la de por defecto
	strcpy(actual, por_defecto);
	paso = 1;
	return 0;
}

void broker_encontrado(void) {
	if (strcmp(actual, guardada) != 0) {
		Guardada g;
		memset(&g, 0, sizeof(g));
		g.marca = MARCA;
		strcpy(g.ip, actual);

		FLASH_EraseInitTypeDef borrar = {0};
		uint32_t error = 0;
		borrar.TypeErase = FLASH_TYPEERASE_PAGES;
		borrar.PageAddress = DIR_GUARDADA;
		borrar.NbPages = 1;
		HAL_FLASH_Unlock();
		if (HAL_FLASHEx_Erase(&borrar, &error) == HAL_OK) {
			const uint16_t *h = (const uint16_t *)&g;
			for (unsigned i = 0; i < sizeof(g) / 2; i++) {
				HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, DIR_GUARDADA + 2 * i, h[i]);
			}
		}
		HAL_FLASH_Lock();
		strcpy(guardada, actual);
	}
	paso = 0;   // si se cae, se vuelve a empezar por esta
}
