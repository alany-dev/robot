#ifndef __PWM_H
#define __PWM_H
#include "stm32f10x.h"


#define LEFT 1
#define RIGHT 2






void pwm_ouput_tb6612(int pwm1,int pwm2);

void pwm_tim1_init_tb6612(u16 period,u16 div);

void pwm_tim4_init(u16 period,u16 div);

#endif
