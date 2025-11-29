#include "delay.h"

void delay_nop_us(int n_us)
{
	int i;
	for(i=0;i<n_us;i++)
	{
		__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();
		__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();
		__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();
		
		__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();
		__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();
		__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();__nop();

		__nop();__nop();__nop();__nop(); __nop();__nop();__nop();__nop();__nop();__nop();
		__nop();__nop();
	}//一次循环1us
}

void delay_nop_ms(int n_ms)
{
	int i;
	for(i=0;i<n_ms;i++)
	{
		delay_nop_us(1000);
	}
}




































