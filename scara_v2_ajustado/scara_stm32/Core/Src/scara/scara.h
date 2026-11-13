/*
 * scara.h
 *
 *  Lectura de corrientes, proteccion y envio por MQTT
 *  para la placa de proteccion del SCARA (STM32F030C8).
 */

#ifndef SRC_SCARA_SCARA_H_
#define SRC_SCARA_SCARA_H_

void scara_init(void);        // arranca el ADC con DMA y prende los EN
void scara_proteccion(void);  // llamar en el while(1): revisa sobrecorriente
void scara_publicar(void);    // llamar en el while(1): manda scara/estado cada 500 ms

#endif /* SRC_SCARA_SCARA_H_ */
