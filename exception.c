#include "device_driver.h"
#include <stdio.h>

// 정의되지 않은 예외 발생 핸들러
void _Invalid_ISR(void)
{
    unsigned int r = Macro_Extract_Area(SCB->ICSR, 0x1ff, 0);
    printf("\nInvalid_Exception: %d!\n", r);
    printf("Invalid_ISR: %d!\n", r - 16);
    for (;;)
        ;
}

// TIM4 : 1ms 시스템 틱
void TIM4_IRQHandler(void)
{
    Macro_Clear_Bit(TIM4->SR, 0);
    NVIC_ClearPendingIRQ(30);

    Tick_ISR_Handler();
}

// USART2 : Qt 통신 수신. 바이트를 링버퍼에 담기만 한다 (파싱은 Comm_Task).
void USART2_IRQHandler(void)
{
    unsigned char d = (unsigned char)USART2->DR; // 읽으면 RXNE 자동 클리어

    Comm_RX_Push(d);
    NVIC_ClearPendingIRQ(38);
}

// EXTI1 : 초음파 Echo (PC1)
void EXTI1_IRQHandler(void)
{
    Sonar_Edge_ISR();
    NVIC_ClearPendingIRQ(7);
}

// EXTI9_5 : DHT11(PA8, line8) 과 버튼(PC9, line9) 이 공유한다.
// 각 핸들러가 자기 핀의 PR 비트만 확인하고 처리하므로 순서는 상관없다.
void EXTI9_5_IRQHandler(void)
{
    Dht11_Edge_ISR();
    Button_Edge_ISR();
    NVIC_ClearPendingIRQ(23);
}
