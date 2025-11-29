#include "usart3.h"	
#include <stdlib.h>
#include <stdio.h>

#include <string.h>
#include "main.h"


#define MAX_RECV_LEN 100

extern CmdData cmd_data;
extern int new_recv_flag;

char rx_data[MAX_RECV_LEN];
unsigned char rx_cnt;



#pragma import(__use_no_semihosting)             
                 
struct __FILE 
{ 
	int handle; 
}; 

FILE __stdout;       
_sys_exit(int x) 
{ 
	x = x; 
}

int fputc(int ch, FILE *f)
{
	USART3->DR=(uint8_t)ch;
	while( (USART3->SR&0X40)==0 ) ;
	return ch;
}


void uart3_init(u32 bound)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;

	
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB| RCC_APB2Periph_AFIO, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);
	
	//USART3_TX   PB10
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_Init(GPIOB, &GPIO_InitStructure);
	
	
	 NVIC_InitStructure.NVIC_IRQChannel = USART3_IRQn;
		NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority=0;
		NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;

		NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;			
		NVIC_Init(&NVIC_InitStructure);	
		
	
	
	//USART3_RX	  PB11
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
	GPIO_Init(GPIOB, &GPIO_InitStructure);  
	USART_InitStructure.USART_BaudRate = bound;;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;
	USART_InitStructure.USART_StopBits = USART_StopBits_1;
	USART_InitStructure.USART_Parity = USART_Parity_No;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
	USART_Init(USART3, &USART_InitStructure);
	
	USART_ITConfig(USART3, USART_IT_RXNE, ENABLE);
	USART_Cmd(USART3, ENABLE);                   
}


void receive_finished_process() //收到U V \r \n后进入此函数
{
	unsigned char i;

	if(rx_data[0] != 'D' || rx_data[1] != 'A')//和数据头不符
	{
//		printf("STM32: head 0x%x 0x%x error, data: ",rx_data[0],rx_data[1]);
		for(i=0;i<rx_cnt;i++) printf("0x%x ",rx_data[i]);
		printf("\r\n");
		return;
	}

	if(rx_data[2]!=rx_cnt) //读取数据长度和结构体长度信息不符
	{
//		printf("STM32: struct len info %d != recv len %d error, data: ",rx_data[2],rx_cnt);
		for(i=0;i<rx_cnt;i++) printf("0x%x ",rx_data[i]);
		printf("\r\n");
		return;
	}
	
	if(rx_cnt!=sizeof(CmdData))//和提前约定的结构体长度信息不符
	{
//		printf("STM32: recv len %d error, data: ",rx_cnt);
		for(i=0;i<rx_cnt;i++) printf("0x%x ",rx_data[i]);
		printf("\r\n");
		return;
	}
	
	//printf("STM32: recv len %d\r\n",rx_cnt);
	
	memcpy(&cmd_data,rx_data,sizeof(cmd_data));//8个字节
	new_recv_flag = 1;
	
}


void USART3_IRQHandler(void) 
{
	unsigned char ch;
	static unsigned char l_ch=0,ll_ch=0,lll_ch=0;
	if(USART_GetITStatus(USART3, USART_IT_RXNE) != RESET)  
	{
		ch = USART_ReceiveData(USART3);	

		//USART1->DR=ch;
		//while( (USART1->SR&0X40)==0 ) ;

		
		rx_data[rx_cnt] = ch;
		rx_cnt++;
		
		if(rx_cnt>=MAX_RECV_LEN)
		{
			rx_cnt=0;
		}
		
		
		if(lll_ch=='T' && ll_ch=='A' && l_ch=='\r' && ch=='\n')
		{
			receive_finished_process();
			rx_cnt=0;
		}
		
		lll_ch = ll_ch;
		ll_ch = l_ch;
 		l_ch = ch;
  }
}







