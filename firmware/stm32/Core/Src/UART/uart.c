/*
 * uart.c
 *
 *  Created on: Oct 20, 2024
 *      Author: vatin
 *
 *  (SCARA) Version para la PCB del SCARA: solo USART1 (FC41).
 *  En la PCB no hay USART2 (PA2/PA3 son entradas de corriente),
 *  asi que se quito todo lo de uart2 y se deja remove_uart2_buffer
 *  vacia porque FC41.c la usa en el modo "looop".
 */

#include "main.h"

extern UART_HandleTypeDef huart1;

#define CIR_SIZE	128
#define OK		0
#define ERROR	1

char uart1_buffer[CIR_SIZE];
int uart_in_data=0;
int uart_out_data=0;
int uart_num_data=0;


char insert_uart1_buffer(char *dato) {
	uart1_buffer[uart_in_data]=*dato;
	uart_in_data++;
	if (uart_in_data>=CIR_SIZE) {
		uart_in_data=0;
	}
	uart_num_data++;
	if (uart_out_data==uart_in_data) {
		uart_out_data++;
		uart_num_data--;
		if (uart_out_data>=CIR_SIZE) {
			uart_out_data=0;
		}
	}

	return OK;
}

char remove_uart1_buffer(char *dato) {
	if (uart_in_data==uart_out_data) {
		return ERROR;
	}
	*dato=uart1_buffer[uart_out_data];
	uart_out_data++;
	if (uart_out_data>=CIR_SIZE) {
		uart_out_data=0;
	}
	uart_num_data--;
	return OK;
}

// (SCARA) sin USART2: siempre "no hay datos"
char remove_uart2_buffer(char *dato) {
	return ERROR;
}


unsigned char uart_data1;

void enable_uart1_rx(void) {
	HAL_UART_Receive_IT(&huart1, &uart_data1, 1);
}

// Cada byte que llega del FC41 se guarda en el buffer circular
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
	if (huart->Instance == huart1.Instance) {
		insert_uart1_buffer((char *)&uart_data1);
		HAL_UART_Receive_IT(&huart1, &uart_data1, 1);
	}
}
