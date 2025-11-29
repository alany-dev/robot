#include "stm32f10x.h"
#include "delay.h"
#include <stdio.h>
#include "pwm.h"
#include "main.h"
#include "usart3.h"
#include "encoder.h"




//宏定义
#define M_PI 3.14159265
#define SYSCLK_HZ 72000000
#define DIV  1 //32
#define PWM_FREQ_HZ 10000 //10KHz
#define PERIOD (SYSCLK_HZ/DIV/PWM_FREQ_HZ) //PERIOD=74M/1/10000=7200
#define CYCLE_TIMER_S 0.02 //0.02s=20ms=50Hz

int new_recv_flag;
int heart_beat_cnt;


CmdData cmd_data;//香橙派下发的命令
McuData mcu_data;//MCU上传的数据

unsigned char *mcu_data_ptr;


void Motor_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct;
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB,ENABLE);//开启时钟
	
	GPIO_InitStruct.GPIO_Mode=GPIO_Mode_Out_PP;//初始化GPIO--PB12、PB13、PB14、PB15为推挽输出
	GPIO_InitStruct.GPIO_Pin=GPIO_Pin_12 |GPIO_Pin_13 |GPIO_Pin_14 |GPIO_Pin_15;
	GPIO_InitStruct.GPIO_Speed=GPIO_Speed_50MHz;
	GPIO_Init(GPIOB,&GPIO_InitStruct);	
	// 设置GPIO初始状态，确保电机方向控制信号正确
	GPIO_ResetBits(GPIOB, GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15);
}

void send(USART_TypeDef* USARTx, uint8_t* pData, uint16_t Size){
	for(uint32_t i = 0;  i < Size; i++){
		while(USART_GetFlagStatus(USART3,USART_FLAG_TXE)== RESET);
		USART_SendData(USART3,pData[i]);
		
	}
	
	while(USART_GetFlagStatus(USART3,USART_FLAG_TC)== RESET);

}


//int fputc(int ch, FILE* f){
//	while(USART_GetFlagStatus(USART3,USART_FLAG_TXE)== RESET);
//	USART_SendData(USART3,(uint8_t)ch);
//	return ch;
//} 
//	



void SysTick_Handler(void) //采用滴答定时器定时，可以省去一个通用定时器
{
int i;
	short encoder1,encoder2;
	static short pwm1=0,pwm2=0;
	encoder1 = read_encoder_tim2();//编码器encoder1和target1为正表示前进
	encoder2 = -read_encoder_tim4();//编码器encoder2和target2为正表示前进
//		printf("encoder1: %d (0x%04X)\r\n", encoder1, (short)encoder1);
//			printf("encoder2: %d (0x%04X)\r\n", encoder2, (short)encoder2);


	if(new_recv_flag==1)
	{
		new_recv_flag = 0;
		heart_beat_cnt= 0;//接收到新消息之后心跳计数清0
		pwm1 = cmd_data.pwm1;
		pwm2 = cmd_data.pwm2;
	}
	
	//心跳计数
	if(heart_beat_cnt>=5)//20ms x 5 = 100ms 会强制停车
	{
		pwm1=0;
		pwm2=0;
	}
	else
	{
		heart_beat_cnt++;
	}
		
	pwm_ouput_tb6612(pwm1,pwm2);
	
	mcu_data.encoder1 = encoder1;
	mcu_data.encoder2 = encoder2;
	
//	
//// 1. 帧头信息
//printf("head1: 0x%02X ('%c')\r\n", mcu_data.head1, mcu_data.head1);
//printf("head2: 0x%02X ('%c')\r\n", mcu_data.head2, mcu_data.head2);

//// 2. 长度字段
//printf("struct_size: 0x%02X (%d)\r\n", mcu_data.struct_size, mcu_data.struct_size);

//// 3. PWM控制值
//printf("pwm1: %d (0x%04X)\r\n", mcu_data.encoder1, (short)mcu_data.encoder1);
//printf("pwm2: %d (0x%04X)\r\n", mcu_data.encoder2, (short)mcu_data.encoder2);

//// 4. 帧尾信息
//printf("end1: 0x%02X ('%c')\r\n", mcu_data.end1, mcu_data.end1);
//printf("end2: 0x%02X ('%c')\r\n", mcu_data.end2, mcu_data.end2);
//printf("end3: 0x%02X ('%c')\r\n", mcu_data.end3, mcu_data.end3);
//printf("end4: 0x%02X ('%c')\r\n", mcu_data.end4, mcu_data.end4);
//	
		//发送MCU数据
	for(i=0;i<sizeof(McuData);i++)
	{
		USART3->DR=mcu_data_ptr[i];
		while( (USART3->SR&0X40)==0 ) ;
	}
}

	



int main(void)
{
	//定义局部变量
	int loop_cnt=0;
	float stat_val=0;

	mcu_data.head1 = 'D';
	mcu_data.head2 = 'A';
	mcu_data.struct_size = sizeof(McuData);
	mcu_data.end1 = 'T';
	mcu_data.end2 = 'A';
	mcu_data.end3 = '\r';
	mcu_data.end4 = '\n';
	
	mcu_data_ptr = (unsigned char *)&mcu_data;

	
	
	SystemInit();
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

	RCC_ClocksTypeDef  RCC_Clocks; 
	RCC_GetClocksFreq(&RCC_Clocks);
	
	Motor_Init();//电机方向GPIO初始化
//	printf("gpio_motor_dirve_init\r\n");
	
	
	//tb6612
	pwm_tim1_init_tb6612(PERIOD,DIV);//电机速度PWM定时器初始化
	
	
	encoder_tim2_init();//编码器脉冲捕获定时器2初始化
	encoder_tim4_init();//编码器脉冲捕获定时器4初始化
//	printf("encoder_tim3_init\r\n");
	
//	PWM_Init_TIM1(0,7199);
	pwm_ouput_tb6612(0,0);
//	pwm_ouput_tb6612(0,1200);
        pwm_ouput_tb6612(1200,1200);
	delay_nop_ms(500);
        pwm_ouput_tb6612(0,0);
	uart3_init(115200);

//	printf("pwm_ouput_tb6612\r\n");
	
	//左-》右
//	pwm_ouput_tb6612(2000,2000);
//	delay_nop_ms(5000);

	
	uint32_t reload_value = RCC_Clocks.SYSCLK_Frequency * CYCLE_TIMER_S;
if (reload_value > 0xFFFFFF) {
    reload_value = 0xFFFFFF; // 取最大值
//    printf("[WARN] Reload value clipped to 0xFFFFFF\r\n");
}
	
if (SysTick_Config(reload_value)) {
//printf("[ERROR] SysTick config failed! Check clock or reload value.\r\n");
while(1); // 死循环，防止继续运行
} else {
//    printf("[OK] SysTick configured (Reload=0x%08X)\r\n", reload_value);
}
//	SysTick_Config(RCC_Clocks.SYSCLK_Frequency * CYCLE_TIMER_S);
	    


while(1)
	{
//delay_nop_ms(10);//10ms
	}
//			GPIO_WriteBit(GPIOC, GPIO_Pin_13, Bit_SET);

//	//前
//	pwm_ouput_tb6612(1200,1200);
//	Delay(500);
//	

	
	//左轮前
//	pwm_ouput_tb6612(1200,0);
//	Delay(500);
//			GPIO_WriteBit(GPIOC, GPIO_Pin_13, Bit_SET);

//	//左轮后
//	pwm_ouput_tb6612(-1200,0);
//	Delay(500);
//	pwm_ouput_tb6612(0,1200);
//	//右轮前
//	Delay(500);
//	pwm_ouput_tb6612(0,-1200);
//	//右轮后
//	Delay(500);
//	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);  

//	
//	// ??-??-??-2MHZ
//    GPIO_InitTypeDef  GPIO_InitStructure = {0};

//    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_13;       
//    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD; 
//    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
//    
//    GPIO_Init(GPIOC, &GPIO_InitStructure);
//		
////		GPIO_WriteBit(GPIOC,GPIO_Pin_13,Bit_RESET); 
////		GPIO_WriteBit(GPIOC,GPIO_Pin_13,Bit_SET); 
//	while(1)
//	{
//		GPIO_WriteBit(GPIOC,GPIO_Pin_13,Bit_RESET); 
//		Delay(100);
//		GPIO_WriteBit(GPIOC,GPIO_Pin_13,Bit_SET);  
//		Delay(100);
//	}



//   RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);  

//    GPIO_InitTypeDef  GPIO_InitStructure = {0};

//    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;       
//    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP; 
//    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
//    
//    GPIO_Init(GPIOA, &GPIO_InitStructure);
//    
//    GPIO_WriteBit(GPIOA,GPIO_Pin_0,Bit_SET);
//		GPIO_WriteBit(GPIOA,GPIO_Pin_0,Bit_RESET);





//        RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);
//        USART_InitTypeDef USART_InitStructure;
//        
//        USART_InitStructure.USART_BaudRate = 115200;
//        USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
//        
//        USART_InitStructure.USART_WordLength = USART_WordLength_8b;
//        USART_InitStructure.USART_StopBits = USART_StopBits_1;
//        USART_InitStructure.USART_Parity = USART_Parity_No;

//    USART_Init(USART1, &USART_InitStructure);
//		
//		
//		RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);  

//    GPIO_InitTypeDef  GPIO_InitStructure = {0};

//    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;       
//    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP; 
//    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_10MHz;
//    
//    GPIO_Init(GPIOA, &GPIO_InitStructure);
//		
//		
//		RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);  
//		GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;       
//    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU; 
//		GPIO_Init(GPIOA, &GPIO_InitStructure);
//	
//		
//		USART_Cmd(USART1, ENABLE);   
//    
//		uint8_t data[] = {1,2,3,4,5};
////		send(USART1, data,5);
//		printf("Hello World\r\n");
		
		
		
		
		
//		    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);
//        USART_InitTypeDef USART_InitStructure;
//        
//        USART_InitStructure.USART_BaudRate = 115200;
//        USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
//        
//        USART_InitStructure.USART_WordLength = USART_WordLength_8b;
//        USART_InitStructure.USART_StopBits = USART_StopBits_1;
//        USART_InitStructure.USART_Parity = USART_Parity_No;

//    USART_Init(USART3, &USART_InitStructure);
//		
//		
//		RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);  

//    GPIO_InitTypeDef  GPIO_InitStructure = {0};

//    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;       
//    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP; 
//    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_10MHz;
//    
//    GPIO_Init(GPIOB, &GPIO_InitStructure);
//		
//		
//		RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);  
//		GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11;       
//    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU; 
//		GPIO_Init(GPIOB, &GPIO_InitStructure);
//	
//		
//		USART_Cmd(USART3, ENABLE);   
//    
//		uint8_t data[] = {1,2,3,4,5};
////		send(USART1, data,5);
//		printf("Hello World\r\n");
		
}
