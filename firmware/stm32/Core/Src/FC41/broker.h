/*
 * broker.h - (SCARA) buscar el PC con Mosquitto en la red, sin escribir su IP
 * Orden: la ultima IP que funciono (guardada en la flash) -> DEFAULT_IP -> la puerta de enlace
 * -> toda la subred del FC41 (x.x.x.1 a x.x.x.254). Cuando una conecta, se guarda en la flash.
 */

#ifndef SRC_FC41_BROKER_H_
#define SRC_FC41_BROKER_H_

// Arma la lista con lo que dio AT+QGETIP (cadenas vacias si no se supo). Se puede llamar varias veces
void broker_inicio(const char *mi_ip, const char *puerta, const char *ip_default);
// IP que toca probar ahora
const char *broker_actual(void);
// Pasa a la siguiente. Devuelve 0 si ya se probaron todas y se vuelve a empezar
char broker_siguiente(void);
// La actual conecto: guardarla en la flash (si cambio) y empezar por ella la proxima vez
void broker_encontrado(void);

#endif /* SRC_FC41_BROKER_H_ */
