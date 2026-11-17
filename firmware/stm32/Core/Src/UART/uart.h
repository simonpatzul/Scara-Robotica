/*
 * uart.h
 *
 *  Created on: Oct 20, 2024
 *      Author: vatin
 */

#ifndef SRC_UART_UART_H_
#define SRC_UART_UART_H_


#define uart_OK		0
#define uart_ERROR	1

char remove_uart1_buffer(char *dato);
char remove_uart2_buffer(char *dato);   // (SCARA) vacia, no hay USART2
void enable_uart1_rx(void);


#endif /* SRC_UART_UART_H_ */
