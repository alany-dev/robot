#ifndef __USART3_H
#define __USART3_H

#include "stm32f10x.h"
#include "main.h"


#pragma pack(1)

typedef struct
{
	unsigned char head1;//数据头1 'D'
	unsigned char head2;//数据头2 'A'
	unsigned char struct_size;//结构体长度

	short encoder1;//编码器当前值1
	short encoder2;//编码器当前值2

	unsigned char end1;//数据尾1 'T'
	unsigned char end2;//数据尾2 'A'
	unsigned char end3;//数据尾3 '\r' 0x0d
	unsigned char end4;//数据尾4 '\n' 0x0a
}McuData;


typedef struct
{
	unsigned char head1;//数据头1 'D'
	unsigned char head2;//数据头2 'A'
	unsigned char struct_size;//结构体长度
	
	short pwm1;//油门PWM1
	short pwm2;//油门PWM2
	
	unsigned char end1;//数据尾1 'T'
	unsigned char end2;//数据尾2 'A'
	unsigned char end3;//数据尾3 '\r' 0x0d
	unsigned char end4;//数据尾4 '\n' 0x0a
}CmdData;

#pragma pack()

void USART3_Send_String(char *String);
void uart3_init(u32 bound);				
void USART3_IRQHandler(void);
#endif
