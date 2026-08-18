#include "device_driver.h"

static volatile unsigned long s_tick_ms = 0;

void Timer_Init(void)
{
    // ---------- TIM3 : 1us 프리러닝 카운터 ----------
    Macro_Set_Bit(RCC->APB1ENR, 1); // TIM3 클럭

    TIM3->CR1 = 0;                            // 업카운터, 리피트
    TIM3->PSC = (TIMXCLK / 1000000U) - 1U;    // 1MHz
    TIM3->ARR = 0xFFFFU;
    Macro_Set_Bit(TIM3->EGR, 0);
    Macro_Set_Bit(TIM3->CR1, 0);

    // ---------- TIM4 : 1ms 틱 인터럽트 ----------
    Macro_Set_Bit(RCC->APB1ENR, 2); // TIM4 클럭

    TIM4->CR1 = 0;
    TIM4->PSC = (TIMXCLK / 1000000U) - 1U;    // 1MHz
    TIM4->ARR = 1000U - 1U;                   // 1000us = 1ms

    Macro_Set_Bit(TIM4->EGR, 0);
    Macro_Clear_Bit(TIM4->SR, 0);

    NVIC_ClearPendingIRQ(30);
    Macro_Set_Bit(TIM4->DIER, 0); // UIE
    NVIC_EnableIRQ(30);

    Macro_Set_Bit(TIM4->CR1, 0);
}

// exception.c 의 TIM4_IRQHandler 에서 호출
void Tick_ISR_Handler(void)
{
    s_tick_ms++;
}

unsigned long Tick_Get_Ms(void)
{
    return s_tick_ms;
}

unsigned short Tick_Get_Us(void)
{
    return (unsigned short)TIM3->CNT;
}

void Tick_Spin_Us(unsigned short us)
{
    unsigned short t0 = Tick_Get_Us();
    while ((unsigned short)(Tick_Get_Us() - t0) < us)
        ;
}
