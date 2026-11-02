#include "device_driver.h"

// 회전 시 도는 쪽 바퀴의 듀티비. 전진/후진 속도(main.c 의 WHEEL_SPEED_FIXED)와
// 별개로 고정한다. 회전은 도는 쪽만 반영되고, 정지한 쪽은 완전히 프리휠 정지.
#define TURN_DUTY_FIXED (90)

/*
 * 좌우 바퀴 각각 -100 ~ +100 (음수는 역회전)
 *
 * 좌 바퀴 방향 : PB12(IN1), PB13(IN2)
 * 우 바퀴 방향 : PB14(IN1), PB15(IN2)
 */
static void Wheel_Set(int left, int right)
{
    if (left > 100) left = 100;
    if (left < -100) left = -100;
    if (right > 100) right = 100;
    if (right < -100) right = -100;

    // ---- 좌 바퀴 방향 (PB12=IN1, PB13=IN2) ----
    if (left > 0)
    {
        Macro_Set_Bit(GPIOB->ODR, 12);
        Macro_Clear_Bit(GPIOB->ODR, 13);
    }
    else if (left < 0)
    {
        Macro_Clear_Bit(GPIOB->ODR, 12);
        Macro_Set_Bit(GPIOB->ODR, 13);
    }
    else
    {
        Macro_Clear_Bit(GPIOB->ODR, 12); // 둘 다 Low = 프리휠 정지
        Macro_Clear_Bit(GPIOB->ODR, 13);
    }

    // ---- 우 바퀴 방향 (PB14=IN1, PB15=IN2) ----
    if (right > 0)
    {
        Macro_Set_Bit(GPIOB->ODR, 14);
        Macro_Clear_Bit(GPIOB->ODR, 15);
    }
    else if (right < 0)
    {
        Macro_Clear_Bit(GPIOB->ODR, 14);
        Macro_Set_Bit(GPIOB->ODR, 15);
    }
    else
    {
        Macro_Clear_Bit(GPIOB->ODR, 14);
        Macro_Clear_Bit(GPIOB->ODR, 15);
    }

    // ---- 속도 (TIM5 CH1=좌 ENA, CH2=우 ENB) ----
    {
        unsigned int arr1 = TIM5->ARR + 1U;
        int l_abs = (left < 0) ? -left : left;
        int r_abs = (right < 0) ? -right : right;

        TIM5->CCR1 = (arr1 * (unsigned int)l_abs) / 100U;
        TIM5->CCR2 = (arr1 * (unsigned int)r_abs) / 100U;
    }
}

void Motor_Init(void)
{
    // ---- GPIOA, GPIOB 클럭 ----
    Macro_Set_Bit(RCC->AHB1ENR, 0);
    Macro_Set_Bit(RCC->AHB1ENR, 1);
    Macro_Set_Bit(RCC->APB1ENR, 3); // TIM5 클럭

    // ---- 방향 핀 4개(PB12~15)를 출력으로 ----
    Macro_Write_Block(GPIOB->MODER, 0xFFU, 0x55U, 24U);

    Macro_Clear_Bit(GPIOB->ODR, 12);
    Macro_Clear_Bit(GPIOB->ODR, 13);
    Macro_Clear_Bit(GPIOB->ODR, 14);
    Macro_Clear_Bit(GPIOB->ODR, 15);

    // ---- PA0, PA1 : TIM5 CH1/CH2 PWM (AF02) ----
    Macro_Write_Block(GPIOA->MODER, 0xFU, 0xAU, 0U);
    Macro_Write_Block(GPIOA->AFR[0], 0xFFU, 0x22U, 0U);

    TIM5->CR1 = (1U << 4U) | (0U << 3U); // Down-counter, Repeat Mode
    TIM5->PSC = (unsigned int)(TIMXCLK / (10000U * 100U)) - 1U; // 10kHz PWM
    TIM5->ARR = 100U - 1U;                                      // ARR=99 -> CCR가 곧 duty%

    Macro_Write_Block(TIM5->CCMR1, 0xFFFFU, 0x6868U, 0U); // CH1,CH2 PWM Mode1 + Preload
    Macro_Set_Bit(TIM5->CCER, 0U); // CH1 ON
    Macro_Set_Bit(TIM5->CCER, 4U); // CH2 ON

    TIM5->CCR1 = 0;
    TIM5->CCR2 = 0;

    Macro_Set_Bit(TIM5->EGR, 0U);
    Macro_Set_Bit(TIM5->CR1, 0U);

    Motor_Drive(DRIVE_STOP, 0);
}

void Motor_Drive(DriveState state, int duty)
{
    if (duty < 0) duty = 0;
    if (duty > 100) duty = 100;

    switch (state)
    {
    case DRIVE_FORWARD:  Wheel_Set( duty,  duty); break;
    case DRIVE_BACKWARD: Wheel_Set(-duty, -duty); break;

    // 스키드 조향 : 도는 쪽만 90% 로 전진, 반대쪽은 완전 정지(0).
    // 전진/후진에 쓰는 duty 매개변수와 무관하게 TURN_DUTY_FIXED 로 고정.
    case DRIVE_LEFT:  Wheel_Set(0, TURN_DUTY_FIXED); break; // 좌 정지, 우만 전진 -> 좌회전
    case DRIVE_RIGHT: Wheel_Set(TURN_DUTY_FIXED, 0); break; // 우 정지, 좌만 전진 -> 우회전

    case DRIVE_STOP:
    default:             Wheel_Set(0, 0);         break;
    }
}
