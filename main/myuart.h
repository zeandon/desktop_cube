#ifndef __MYUART_H__
#define __MYUART_H__

void uart_init(void);
void uart_send_string(const char* string);
void uart_send_num(int num);

#endif