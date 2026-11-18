/*
 * FC41.h
 *
 *  Created on: Aug 24, 2024
 *      Author: vatin
 */

#ifndef SRC_FC41_FC41_H_
#define SRC_FC41_FC41_H_


void init_FC(void);  //inicializa las contantes
void FC41_state_machine(void); //maquinas de estado, llamar en main
void FC41_state_response(void);  //maquinas de estado, llamr ne main
void publish(char *topic, char *msg); //pucblicar en un topic un mensaje
char isWifiReady(void);  //ya esta conectado
char isNewMQTTReceive(void);      //(SCARA) llego un mensaje a scara/cmd
void clearisNewMQTTReceive(void); //(SCARA) ya se leyo
char *mqtt_payload(void);         //(SCARA) texto del ultimo mensaje
void publishJSON(char *topic, char *n1, char *n2, int v1, int v2); //publicar en un topic un json
void make_post_json(char *n1, char *n2, int v1, int v2); //Hacer post a una URL de un JSON


#endif /* SRC_FC41_FC41_H_ */
