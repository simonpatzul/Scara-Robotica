/*
 * FC41.c
 *
 *  Created on: Aug 24, 2024
 *      Author: vatin
 */


#include "FC41.h"
#include "broker.h"
#include "../UART/uart.h"
#include "../timer/timer.h"
#include "../STATES/states.h"
#include <string.h>
#include <stdio.h>
#include "main.h"


extern UART_HandleTypeDef huart1;
// (SCARA) en la PCB no hay USART2: se quito extern huart2

void fc41_process(void);
void get_stat(void);
void get_ip(void);
char * next_coma(char *p);
char * copyto(char *dest, char *p);
char * copytocomma(char *dest, char *p);
char * copyintto(int * dest, char *p);
void getData(void);
void testPostResponse(void);



enum FC41_EVENTS {
	EV_FC41_ok=1,
	EV_FC41_error,
	EV_FC41_timeout,
	EV_FC41_scan,
	EV_FC41_ready,
	EV_FC41_connected,
	EV_FC41_IP,
	EV_FC41_disconnected,
	EV_FC41_gotip,
	EV_FC41_session,
	EV_FC41_sessionError,
	EV_FC41_broker,
	EV_FC41_subscript,
	EV_FC41_connectpost,
	EV_FC41_postok,
	EV_FC41_posterr,
	EV_FC41_jsonpost,
};

enum FC41_STATE {
	FC_start=0,
	FC_startEnd,
	FC_startWait,
	FC_at,
	FC_atrx,
	FC_echo,
	FC_echorx,
	FC_wait_1,
	FC_wait_1_delay,
	FC_scan,
	FC_scanrx,
	FC_connect,
	FC_connectrx,
	FC_wait_2,
	FC_disconnect,
	FC_disconnectrx,
	FC_CONFIG,
	FC_CONFIGrx,
	FC_OPEN,
	FC_OPENrx,
	FC_OPEN_state,
	FC_OPEN_WAIT,
	FC_CONN_BROKER,
	FC_CONN_BROKERrx,
	FC_CONN_BROKER_state,
	FC_CONN_SUBS,
	FC_CONN_SUBSrx,
	FC_CONN_SUBS_state,
	FC_wait_3,
	FC_getIP,
	FC_getIPrx,
	FC_looop,
	FC_setup_post,
	FC_setup_postrx,
	FC_start_post,
	FC_start_postrx,
	FC_complete_post,
	FC_complete_postrx,
	FC_post_status,
};

enum PC41_STATE_RX {
	FC_CR1,
	FC_LF1,
	FC_RESP,
	FC_LF2,
	FC_RESP2,
	FC_LF3,
};


const char response[10][30]={
	"OK\0",
	"ERROR\0",
	"ready",
	"CONNECT",
};

const unsigned char commands[15][60]={
	"\r\nAT\r\n\0",
	"\r\nAT+QECHO=0\r\n\0",
	"AT+QSTAAPINFO=%s,%s\r\0",
	"AT+QWSCAN\r\0",
	"AT+QSTASTOP\r\0",
	"AT+QGETIP=station\r\0",
	"AT+QMTCFG=\"version\",1,4\r\0",
	"AT+QMTOPEN=1,\"%s\",%s\r\0",
	"AT+QMTCONN=1,\"%s\",\"%s\",\"%s\"\r\0",
	"AT+QMTPUB=1,1,1,0,\"%s\",%i,\"%s\"\r\0",
	"AT+QMTSUB=1,1,\"%s\",1\r\0",
	"{\"%s\":%i,\"%s\":%i}\0",
	"AT+QHTTPCFG=\"url\",\"%s\"\r\0",  //url post setup
	"AT+QHTTPPOST=%i,120,120,\"data\",\"\",\"application/json\"\r\0", //post start
	"AT+QMTCLOSE=1\r\0",   //(SCARA) soltar un QMTOPEN que no contesto, para probar otra IP
};

enum {
	QAT=0,
	QECHO,
	QSTAAPINFO,
	QWSCAN,
	QSTOP,
	QIP,
	QCFG,
	QOPEN,
	QCONN,
	QPUB,
	QSUBS,
	QJSON,
	QPOST_setup,
	QPOST_start,
	QCLOSE,
};


unsigned int fc41_tick;
int fc41_timeout=0;
unsigned int fc41_rxtick;
int fc41_rxtimeout=0;


enum FC41_STATE fc41_state;
enum FC41_STATE fc41_next_state;

enum PC41_STATE_RX fc41_staterx;
enum PC41_STATE_RX fc41_staterx_next;
int fc41_response_size;

enum FC41_EVENTS fc41_event;

int fc41_timelimit=100;

/**
  * @brief funcion inicializar el modulo Wifi
  * @param None
  * @retval None
  */

// ===== CONFIGURACION SCARA: cambiar antes de grabar =====
#define DEFAULT_SSID	"Simon\0"          // nombre del Wi-Fi (sin espacios)
#define DEFAULT_PASS	"12345678\0"         // clave del Wi-Fi (si no tiene: "\"\"\0")

#define DEFAULT_IP		"172.19.133.20\0"  // IP del PC con Mosquitto: es solo la primera que se prueba; si no contesta,
                                           // se busca en la red (broker.c) y la que funcione se guarda en la flash
#define DEFAULT_PORT	"1883\0"
#define DEFAULT_USERMQTT 	"scara_stm32\0"   // client id MQTT (max 15 letras)
#define DEFAULT_LOGINMQTT	"\0"
#define DEFAULT_PASSMQTT	"\0"

#define DEFAULT_RXTOPIC		"scara/cmd"

#define DEFAULT_URL		"https://httpbin.org/post"

#define SKIP_WAIT1	1
#define SKIP_GETIP	0   // (SCARA) 0: se pide la IP propia y la puerta de enlace, para buscar el broker en la subred
#define ESPERA_BROKER_MS	2500   // (SCARA) si Mosquitto no contesta en este tiempo, se prueba la siguiente IP

void init_FC(void) {
	strcpy(states.modem.SSID,(char *)DEFAULT_SSID);
	strcpy(states.modem.SSID_pass,(char *)DEFAULT_PASS);

	strcpy(states.mqtt.IP_DNS,(char *)DEFAULT_IP);
	strcpy(states.mqtt.PORT,(char *)DEFAULT_PORT);
	strcpy(states.mqtt.clientid,(char *)DEFAULT_USERMQTT);
	strcpy(states.mqtt.login,(char *)DEFAULT_LOGINMQTT);
	strcpy(states.mqtt.password,(char *)DEFAULT_PASSMQTT);

	strcpy(states.mqtt.topic,(char *)DEFAULT_RXTOPIC);

	strcpy(states.post.url,(char *)DEFAULT_URL);

	states.mqtt.connected=0;
}


void publish(char *topic, char *msg) {
	if (states.mqtt.connected!=0) {
		if (states.mqtt.busy == 0) {
			sprintf(states.modem.tx_command,(char *)commands[QPUB],topic,strlen(msg),msg);
			//insert_sp_buffer(states.modem.tx_command,strlen((char *)states.modem.tx_command));
			HAL_UART_Transmit_IT(&huart1, (unsigned char *)states.modem.tx_command, strlen((char *)states.modem.tx_command));
		}
	}
}

void publishJSON(char *topic, char *n1, char *n2, int v1, int v2) {
	char tmp[100];
	if (states.mqtt.connected!=0) {
		if (states.mqtt.busy == 0) {
			states.mqtt.busy=1;
			sprintf(tmp,(char *)commands[QJSON],n1,v1,n2,v2);
			sprintf(states.modem.tx_command,(char *)commands[QPUB],topic,strlen(tmp),tmp);
			//insert_sp_buffer(states.modem.tx_command,strlen((char *)states.modem.tx_command));
			HAL_UART_Transmit_IT(&huart1, (unsigned char *)states.modem.tx_command, strlen((char *)states.modem.tx_command));
		}
	}
}

void setup_post(void) {
	if (states.mqtt.connected!=0) {
		sprintf(states.modem.tx_command,(char *)commands[QPOST_setup],states.post.url);
		HAL_UART_Transmit_IT(&huart1, (unsigned char *)states.modem.tx_command, strlen((char *)states.modem.tx_command));
	}
}

void start_post(void) {
	sprintf(states.modem.tx_command,(char *)commands[QPOST_start],states.post.msgSize);
	HAL_UART_Transmit_IT(&huart1, (unsigned char *)states.modem.tx_command, strlen((char *)states.modem.tx_command));
}

void send_post(void) {
	HAL_UART_Transmit_IT(&huart1, (unsigned char *)states.post.msg, states.post.msgSize);
}

void make_post_json(char *n1, char *n2, int v1, int v2) {
	if (states.mqtt.busy == 0) {
		states.mqtt.busy=1;
		sprintf(states.post.msg,(char *)commands[QJSON],n1,v1,n2,v2);
		states.post.msgSize = strlen(states.post.msg);
		fc41_event = EV_FC41_jsonpost;
	}
}


char isWifiReady(void) {
	return states.mqtt.connected;
}

char isNewMQTTReceive(void) {
	return states.mqtt.new_data;
}

void clearisNewMQTTReceive(void) {
	states.mqtt.new_data=0;
}

// (SCARA) La IP probada no tiene Mosquitto: soltar el QMTOPEN y pasar a la siguiente.
// Si ya se probaron todas, esperar 10 s antes de dar otra vuelta
static char broker_listo = 0;

static void otra_ip(void) {
	HAL_UART_Transmit_IT(&huart1, commands[QCLOSE], strlen((char *)commands[QCLOSE]));
	char vuelta = broker_siguiente();
	fc41_next_state = FC_OPEN_WAIT;
	fc41_timeout=1;
	fc41_timelimit = vuelta ? 500 : 10000;
	fc41_event=0;
}

void FC41_state_machine(void) {
	if (get_tick()!=fc41_tick) {
		fc41_tick = get_tick();
		if (fc41_timeout) {
			fc41_timeout++;
			if (fc41_timeout>=fc41_timelimit) {
				fc41_event = EV_FC41_timeout;
			}
		}
	}
	fc41_next_state = fc41_state;
	switch(fc41_state) {
	case FC_start:
		//reset MODEM: PB8 va directo a CEN (CEN=0 apagado, CEN=1 encendido)
		init_FC();
		HAL_GPIO_WritePin(CEN_GPIO_Port, CEN_Pin, 0);
		fc41_next_state = FC_startEnd;
		fc41_timeout=1;
		break;
	case FC_startEnd:
		if (fc41_event == EV_FC41_timeout) {
			HAL_GPIO_WritePin(CEN_GPIO_Port, CEN_Pin, 1);
			fc41_timeout=1;   //si no llega "ready", seguir igual con AT
			fc41_next_state = FC_startWait;
			fc41_event=0;
		}
		break;

	case FC_startWait:
		if (fc41_event == EV_FC41_ready || fc41_event == EV_FC41_timeout) {
			fc41_timeout=0;
			fc41_next_state = FC_at;
			fc41_event=0;
		}
		break;

	case FC_at:
		HAL_UART_Transmit_IT(&huart1, commands[QAT], strlen((char *)commands[QAT]));
		fc41_next_state = FC_atrx;
		fc41_timeout=1;
		break;
	case FC_atrx:
		if (fc41_event == EV_FC41_ok) {
			fc41_next_state = FC_echo;
			fc41_timeout=0;
			fc41_event=0;
		}
		//sin respuesta o error: reintentar AT
		if (fc41_event == EV_FC41_error || fc41_event == EV_FC41_timeout) {
			fc41_next_state = FC_at;
			fc41_timeout=0;
			fc41_event=0;
		}
		break;
	case FC_echo:
		HAL_UART_Transmit_IT(&huart1, commands[QECHO], strlen((char *)commands[QECHO]));
		fc41_next_state = FC_echorx;
		fc41_timeout=1;
		break;
	case FC_echorx:
		if (fc41_event == EV_FC41_ok) {
			fc41_next_state = FC_wait_1;
			fc41_timeout=0;
			fc41_event=0;
		}
		if (fc41_event == EV_FC41_error) {
			fc41_next_state = FC_echo;
			fc41_timeout=0;
			fc41_event=0;
		}
		break;
	case FC_wait_1:
#if SKIP_WAIT1 == 0
		if (states.modem.connect!=0) {
			states.modem.connect=0;
			fc41_next_state = FC_connect;
		}
		if (states.modem.scan !=0) {
			states.modem.scan=0;
			fc41_next_state = FC_scan;
		}
#else
		fc41_next_state = FC_wait_1_delay;
		fc41_timelimit=500;
		fc41_timeout=1;

#endif
		break;


	case  FC_wait_1_delay:
		if (fc41_event == EV_FC41_timeout) {
			fc41_timeout=0;
			fc41_next_state = FC_connect;
			fc41_event=0;
		}
		break;

	case FC_scan:
		HAL_UART_Transmit_IT(&huart1, commands[QWSCAN], strlen((char *)commands[QWSCAN]));
		fc41_next_state = FC_scanrx;
		fc41_timeout=1;
		break;

	case FC_scanrx:
		if (fc41_event == EV_FC41_ok) {
			fc41_next_state = FC_wait_1;
			fc41_timeout=0;
			fc41_event=0;
		}
		if (fc41_event == EV_FC41_error) {
			fc41_next_state = FC_wait_1;
			fc41_timeout=0;
			fc41_event=0;
		}
		break;

	case FC_connect:
		sprintf(states.modem.tx_command,(char *)commands[QSTAAPINFO],states.modem.SSID,states.modem.SSID_pass);
		HAL_UART_Transmit_IT(&huart1, (unsigned char *)states.modem.tx_command, strlen((char *)states.modem.tx_command));
		fc41_timelimit=10000;
		fc41_next_state = FC_connectrx;
		break;
	case FC_connectrx:
		if (fc41_event == EV_FC41_ok) {
			fc41_next_state = FC_wait_2;
			fc41_timeout=0;
			fc41_timelimit=300;
			fc41_event=0;
		}
		if (fc41_event == EV_FC41_error) {
			fc41_next_state = FC_wait_1;
			fc41_timeout=0;
			fc41_timelimit=300;
			fc41_event=0;
		}
		if (fc41_event == EV_FC41_connected) {
			fc41_next_state = FC_wait_2;
			fc41_timeout=0;
			fc41_timelimit=300;
			fc41_event=0;
		}
		break;

	case FC_wait_2:
		if (states.modem.disconnect!=0) {
			states.modem.disconnect=0;
			fc41_next_state = FC_disconnect;
		}
		if (fc41_event == EV_FC41_IP) {
			fc41_event = 0;
#if SKIP_GETIP == 1
			fc41_next_state = FC_CONFIG;
#else
			fc41_next_state = FC_getIP;
#endif

			fc41_timelimit=600;
		}
		if (states.modem.looop!=0) {
			states.modem.looop=0;
			fc41_next_state = FC_looop;
		}
		break;

	case FC_looop:
		while(1) {
			char dato;
			if ((USART2->CR1 & USART_CR1_TE_Msk)!=0) {
				if (remove_uart1_buffer(&dato)==uart_OK) {
					USART2->TDR = dato;
				}
			}
			if ((USART1->CR1 & USART_CR1_TE_Msk)!=0) {
				if (remove_uart2_buffer(&dato)==uart_OK) {
					USART1->TDR = dato;
				}
			}
		}
		break;

	case FC_getIP:
		HAL_UART_Transmit_IT(&huart1, commands[QIP], strlen((char *)commands[QIP]));
		fc41_next_state = FC_getIPrx;
		fc41_timeout=1;
		break;

	case FC_getIPrx:
		// (SCARA) con o sin respuesta se sigue: sin IP propia solo se prueban la guardada y DEFAULT_IP
		if (fc41_event == EV_FC41_gotip || fc41_event == EV_FC41_ok) {
			fc41_next_state = FC_CONFIG;
			fc41_event=0;
			fc41_timeout=0;
		}
		if (fc41_event == EV_FC41_error || fc41_event == EV_FC41_timeout) {
			if (!broker_listo) broker_inicio("", "", DEFAULT_IP);
			broker_listo = 1;
			fc41_next_state = FC_CONFIG;
			fc41_timeout=0;
			fc41_event=0;
		}
		break;

	case FC_disconnect:
		HAL_UART_Transmit_IT(&huart1, commands[QSTOP], strlen((char *)commands[QSTOP]));
		fc41_next_state = FC_disconnectrx;
		break;

	case FC_disconnectrx:
		if (fc41_event == EV_FC41_ok) {
			fc41_next_state = FC_wait_1;
			fc41_timeout=0;
			fc41_event=0;
		}
		if (fc41_event == EV_FC41_error) {
			fc41_next_state = FC_wait_2;
			fc41_timeout=0;
			fc41_event=0;
		}
		break;

	case FC_CONFIG:
		HAL_UART_Transmit_IT(&huart1, commands[QCFG], strlen((char *)commands[QCFG]));
		fc41_next_state = FC_CONFIGrx;
		fc41_timeout=1;
		break;

	case FC_CONFIGrx:
		if (fc41_event == EV_FC41_ok) {
			fc41_next_state = FC_OPEN;
			fc41_timeout=0;
			fc41_event=0;
		}
		if (fc41_event == EV_FC41_error) {
			fc41_next_state = FC_CONFIG;
			fc41_timeout=0;
			fc41_event=0;
		}
		break;

	case FC_OPEN:
		if (!broker_listo) { broker_inicio("", "", DEFAULT_IP); broker_listo = 1; }
		strcpy(states.mqtt.IP_DNS, broker_actual());   //(SCARA) la IP que toca probar
		sprintf(states.modem.tx_command,(char *)commands[QOPEN],states.mqtt.IP_DNS,states.mqtt.PORT);
		//insert_sp_buffer(states.modem.tx_command,strlen((char *)states.modem.tx_command));
		HAL_UART_Transmit_IT(&huart1, (unsigned char *)states.modem.tx_command, strlen((char *)states.modem.tx_command));
		fc41_timelimit=10000;
		fc41_next_state = FC_OPENrx;
		break;

	case FC_OPENrx:
		if (fc41_event == EV_FC41_ok) {
			fc41_next_state = FC_OPEN_state;
			fc41_timeout=1;                     //(SCARA) antes esperaba sin limite
			fc41_timelimit=ESPERA_BROKER_MS;
			fc41_event=0;
		}
		if (fc41_event == EV_FC41_error || fc41_event == EV_FC41_timeout) {
			otra_ip();
		}
		break;

	case FC_OPEN_state:
		if (fc41_event == EV_FC41_session) {
			broker_encontrado();                //(SCARA) esta IP sirve: guardarla
			fc41_next_state = FC_CONN_BROKER;
			fc41_timeout=0;
			fc41_event=0;
		}
		if (fc41_event == EV_FC41_sessionError || fc41_event == EV_FC41_timeout) {
			otra_ip();
		}
		break;

	case FC_OPEN_WAIT:
		if (fc41_event == EV_FC41_timeout) {
			fc41_timeout=0;
			fc41_next_state = FC_OPEN;
			fc41_event=0;
		}
		break;

	case FC_CONN_BROKER:
		sprintf(states.modem.tx_command,(char *)commands[QCONN],states.mqtt.clientid,states.mqtt.login,states.mqtt.password);
		//insert_sp_buffer(states.modem.tx_command,strlen((char *)states.modem.tx_command));
		HAL_UART_Transmit_IT(&huart1, (unsigned char *)states.modem.tx_command, strlen((char *)states.modem.tx_command));
		fc41_timelimit=10000;
		fc41_next_state = FC_CONN_BROKERrx;
		break;

	case FC_CONN_BROKERrx:
		if (fc41_event == EV_FC41_ok) {
			fc41_next_state = FC_CONN_BROKER_state;
			fc41_timeout=0;
			fc41_event=0;
		}
		if (fc41_event == EV_FC41_error) {
			fc41_next_state = FC_CONN_BROKER;
			fc41_timeout=0;
			fc41_event=0;
		}
		break;

	case FC_CONN_BROKER_state:
		if (fc41_event == EV_FC41_broker) {
			fc41_next_state = FC_CONN_SUBS;
			fc41_timeout=0;
			fc41_event=0;
		}
		break;

	case FC_CONN_SUBS:
		sprintf(states.modem.tx_command,(char *)commands[QSUBS],states.mqtt.topic);
		//insert_sp_buffer(states.modem.tx_command,strlen((char *)states.modem.tx_command));
		HAL_UART_Transmit_IT(&huart1, (unsigned char *)states.modem.tx_command, strlen((char *)states.modem.tx_command));
		fc41_timelimit=10000;
		fc41_next_state = FC_CONN_SUBSrx;
		break;

	case FC_CONN_SUBSrx:
		if (fc41_event == EV_FC41_ok) {
			fc41_next_state = FC_CONN_SUBS_state;
			fc41_timeout=0;
			fc41_event=0;
		}
		if (fc41_event == EV_FC41_error) {
			fc41_next_state = FC_wait_2;
			fc41_timeout=0;
			fc41_event=0;
		}
		break;

	case FC_CONN_SUBS_state:
		if (fc41_event == EV_FC41_subscript) {
			fc41_next_state = FC_wait_3;
			fc41_timeout=0;
			fc41_event=0;
			states.mqtt.connected=1;
		}
		break;

	case FC_wait_3:
		if (fc41_event == EV_FC41_jsonpost) {
			fc41_next_state = FC_setup_post;
			fc41_timeout=0;
			fc41_event=0;
		}
		break;

		////////POST/////////////

	case FC_setup_post:
		setup_post();
		fc41_next_state = FC_setup_postrx;
		fc41_timeout=1;
		fc41_event=0;
		break;
	case FC_setup_postrx:
		if (fc41_event == EV_FC41_ok) {
			fc41_next_state = FC_start_post;
			fc41_timeout=0;
			fc41_event=0;
		}
		if (fc41_event == EV_FC41_error) {
			fc41_next_state = FC_wait_3;
			states.mqtt.busy=0;

			fc41_timeout=0;
			fc41_event=0;
		}
		break;

	case FC_start_post:
		start_post();
		fc41_next_state = FC_start_postrx;
		fc41_timeout=1;
		fc41_event=0;
		break;

	case FC_start_postrx:
		if (fc41_event == EV_FC41_connectpost) {
			fc41_next_state = FC_complete_post;
			fc41_timeout=0;
			fc41_event=0;
		}
		if (fc41_event == EV_FC41_error) {
			fc41_next_state = FC_wait_3;
			states.mqtt.busy=0;
			fc41_timeout=0;
			fc41_event=0;
		}
		break;
	case FC_complete_post:
		send_post();
		fc41_next_state = FC_complete_postrx;
		fc41_timeout=1;
		fc41_event=0;
		break;

	case FC_complete_postrx:
		if (fc41_event == EV_FC41_ok) {
			fc41_next_state = FC_post_status;
			fc41_timeout=0;
			fc41_event=0;
		}
		break;

	case FC_post_status:
		if (fc41_event == EV_FC41_postok) {
			fc41_next_state = FC_wait_3;
			states.mqtt.busy=0;
			fc41_timeout=0;
			fc41_event=0;
		}
		if (fc41_event == EV_FC41_posterr) {
			fc41_next_state = FC_wait_3;
			states.mqtt.busy=0;
			fc41_timeout=0;
			fc41_event=0;
		}
		break;

	}
	fc41_state = fc41_next_state;
}


int rx_size;
char dato;

void FC41_state_response(void) {
	if (get_tick()!=fc41_rxtick) {
		fc41_rxtick = get_tick();
		if (fc41_staterx != FC_CR1) {
			fc41_rxtimeout++;
			if (fc41_rxtimeout>=500) {
				if (fc41_state == FC_getIPrx) {
					if (rx_size>10) {
						fc41_process();
					}
				}
				fc41_staterx = FC_CR1;
			}
		} else {
			fc41_rxtimeout=0;
		}
	}
	if (remove_uart1_buffer(&dato)==uart_OK) {
		fc41_staterx_next = fc41_staterx;

		switch(fc41_staterx) {
		case FC_CR1:
			if (dato==0x0d) {
				fc41_staterx_next=FC_LF1;
				break;
			}
			if (dato=='+') {
				fc41_staterx_next=FC_RESP2;
				rx_size=0;
				break;
			}
			if (dato=='E' || dato=='O') {
				fc41_staterx_next=FC_RESP;
				states.modem.rx_response[0] = dato;
				rx_size=1;
				break;
			}
			break;
		case FC_LF1:
			if (dato==0x0a) {
				fc41_staterx_next=FC_RESP;
				rx_size=0;
			} else {
				fc41_staterx_next=FC_CR1;
			}
			break;
		case FC_RESP:
			if (dato == '+' && rx_size == 0) {
				fc41_staterx_next=FC_RESP2;
				break;
			}
			if (dato == 0x0d) {
				fc41_staterx_next=FC_LF2;
				states.modem.response_size = rx_size;
				states.modem.rx_response[rx_size] = 0;
			} else {
				states.modem.rx_response[rx_size++] = dato;
				if (rx_size>127) {
					fc41_staterx_next=FC_CR1;
					fc41_event = EV_FC41_error;
				}
			}
			break;
		case FC_LF2:
			if (dato==0x0a) {
				fc41_process();
			}
			rx_size=0;
			fc41_staterx_next=FC_CR1;
			break;

		case FC_RESP2:
			if (dato == 0x0d) {
				fc41_staterx_next=FC_LF3;
				states.modem.response_size = rx_size;
				states.modem.rx_response[rx_size] = 0;
			} else {
				states.modem.rx_response[rx_size++] = dato;
				if (rx_size>127) {
					fc41_staterx_next=FC_CR1;
					fc41_event = EV_FC41_error;
				}
			}
			break;

		case FC_LF3:
			if (dato==0x0a) {
				fc41_process();
			}
			rx_size=0;
			fc41_staterx_next=FC_CR1;
			break;

		}

		fc41_staterx = fc41_staterx_next;
	}
}



void fc41_process(void) {
	if (strncmp(states.modem.rx_response,response[0],states.modem.response_size)==0) {
		fc41_event=EV_FC41_ok;
	}
	if (strncmp(states.modem.rx_response,response[2],states.modem.response_size)==0) {
		fc41_event=EV_FC41_ready;
	}
	if (strncmp(states.modem.rx_response,response[1],states.modem.response_size)==0) {
		fc41_event=EV_FC41_error;
	}
	if (strncmp(states.modem.rx_response,response[3],states.modem.response_size)==0) {
		fc41_event=EV_FC41_connectpost;
	}
	if (strncmp(states.modem.rx_response,"QSTASTAT:",9)==0) {
		//mensaje de respuesta WIFI
		get_stat();
	}
/*	if (strncmp(states.modem.rx_response,"CMDRSP:",7)==0) {
		//mensaje de respuesta WIFI
		insert_sp_buffer(&states.modem.rx_response[7],states.modem.response_size-7);
	}*/

	if (strncmp(states.modem.rx_response,"QGETIP:",7)==0) {
		get_ip();
	}
	if (strncmp(states.modem.rx_response,"QMTOPEN: 1,",11)==0) {
		//(SCARA) 0 = conecto; cualquier otro codigo (-1, 1..5) = esa IP no sirve
		fc41_event = (states.modem.rx_response[11]=='0') ? EV_FC41_session : EV_FC41_sessionError;
	}
	if (strncmp(states.modem.rx_response,"QMTCONN: 1,0,0",14)==0) {
		fc41_event=EV_FC41_broker;
	}
	if (strncmp(states.modem.rx_response,"QMTRECV: ",9)==0) {
		getData();
		states.mqtt.new_data = 1;
	}
	if (strncmp(states.modem.rx_response,"QMTSUB: 1,1,0",13)==0) {
		fc41_event=EV_FC41_subscript;
	}
	if (strncmp(states.modem.rx_response,"QHTTPPOST: 0,",13)==0) {
		testPostResponse();
	}
	return;
}


void get_stat(void) {
	if (strncmp("SCAN_NO_AP",&states.modem.rx_response[9],10)==0) {
		return;
	}
	if (strncmp("WLAN_CONNECTED",&states.modem.rx_response[9],14)==0) {
		fc41_event = EV_FC41_connected;
		return;
	}
	if (strncmp("GOT_IP",&states.modem.rx_response[9],6)==0) {
		fc41_event = EV_FC41_IP;
		return;
	}
	if (strncmp("WLAN_DISCONNECTED",&states.modem.rx_response[9],17)==0) {
		fc41_event = EV_FC41_disconnected;
		return;
	}
}

// (SCARA) "QGETIP: <ip>,<mascara>,<puerta>,<dns>": no se sabe si trae comillas o el nombre de la interfaz,
// asi que se toman las 3 primeras direcciones "a.b.c.d" que aparezcan en la linea
static const char *siguiente_ip(const char *p, char *dst) {
	dst[0] = 0;
	while (*p) {
		int n = 0, puntos = 0;
		while (((p[n] >= '0' && p[n] <= '9') || p[n] == '.') && n < 15) {
			if (p[n] == '.') puntos++;
			n++;
		}
		if (puntos == 3 && n >= 7) {
			memcpy(dst, p, n);
			dst[n] = 0;
			return p + n;
		}
		p += (n > 0) ? n : 1;
	}
	return p;
}

void get_ip(void) {
	char ip[16], mascara[16], gw[16];
	const char *r = siguiente_ip(states.modem.rx_response, ip);
	r = siguiente_ip(r, mascara);
	siguiente_ip(r, gw);
	if (ip[0] != 0) {
		broker_inicio(ip, gw, DEFAULT_IP);
		broker_listo = 1;
	}
	char *p = &states.modem.rx_response[10];
	char *q = states.modem.myIP;
	char num=0;
	while(*p!=',' && num<30) {
		*q=*p;
		q++;
		p++;
		num++;
	}
	*q='\r';
	q++;
	*q=0;
	fc41_event = EV_FC41_gotip;
}


void testPostResponse(void) {
	char *p = &states.modem.rx_response[13];
	copytocomma(states.post.response,p);
	if (strncmp(states.post.response,"200",3)==0) {
		fc41_event = EV_FC41_postok;
	} else {
		fc41_event = EV_FC41_posterr;
	}
}

void getData(void) {
	char *p = &states.modem.rx_response[9];
	p=next_coma(p);
	p=next_coma(p);
	p++;
	//p=copyto(states.mqtt.topic,p);
	p=next_coma(p);
	p=copyintto(&states.mqtt.payloadSize,p);
	p++;
	p=copyto(states.mqtt.payload,p);
}

char * next_coma(char *p) {
	while(*p!=',') {
		p++;
	}
	p++;
	return p;
}

char * copyto(char *dest, char *p) {
	while(*p!='\"' && *p!=0) {
		*dest=*p;
		dest++;
		p++;
	}
	*dest=0;   // (SCARA) terminar el texto para poder compararlo
	if (*p) p++;
	return p;
}

char * copytocomma(char *dest, char *p) {
	while(*p!=',') {
		*dest=*p;
		dest++;
		p++;
	}
	p++;
	return p;
}


char * copyintto(int * dest, char *p) {
	int val=0;
	while(*p!=',') {
		val= (val*10) + *p;
		p++;
	}
	p++;
	*dest=val;
	return p;
}

