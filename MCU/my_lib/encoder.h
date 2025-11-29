#ifndef __ENCODER_H
#define __ENCODER_H
#include "main.h"

#define ENCODER_TIM_PERIOD (u16)(65535)  
void encoder_tim2_init(void);
void encoder_tim4_init(void);
int read_encoder_tim2(void);
int read_encoder_tim4(void);
void TIM4_IRQHandler(void);
void TIM2_IRQHandler(void);
#endif
