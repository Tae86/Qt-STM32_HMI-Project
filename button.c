#include "device_driver.h"
#include <stdio.h>

#define DEBOUNCE_MS (30U)

static volatile unsigned long s_last_edge_ms = 0;
static volatile int s_pending = 0;

static int s_stable_pressed = 0;
static int s_press_event = 0;

static int Read_Pressed(void)
{
    return Macro_Check_Bit_Clear(GPIOC->IDR, 9); // Active Low
}

void Button_Init(void)
{
    Macro_Set_Bit(RCC->AHB1ENR, 2);  // GPIOC 클럭
    Macro_Set_Bit(RCC->APB2ENR, 14); // SYSCFG 클럭

    Macro_Write_Block(GPIOC->MODER, 0x3U, 0x0U, 18U); // PC9 입력
    Macro_Write_Block(GPIOC->PUPDR, 0x3U, 0x1U, 18U); // 내부 풀업

    // EXTI9 -> 포트 C  (EXTICR[2] 비트[7:4] 가 EXTI9 담당, 포트C = 0x2)
    Macro_Write_Block(SYSCFG->EXTICR[2], 0xFU, 0x2U, 4U);

    Macro_Set_Bit(EXTI->RTSR, 9);
    Macro_Set_Bit(EXTI->FTSR, 9);
    Macro_Set_Bit(EXTI->IMR, 9);

    NVIC_ClearPendingIRQ(23); // EXTI9_5
    NVIC_EnableIRQ(23);

    s_stable_pressed = Read_Pressed();
    s_pending = 0;
    s_press_event = 0;
}

void Button_Edge_ISR(void)
{
    if (!Macro_Check_Bit_Set(EXTI->PR, 9)) return;
    EXTI->PR = (1U << 9); // 펜딩 클리어

    s_last_edge_ms = Tick_Get_Ms();
    s_pending = 1;
}

void Button_Task(unsigned long now_ms)
{
    if (!s_pending) return;
    if ((now_ms - s_last_edge_ms) < DEBOUNCE_MS) return;

    s_pending = 0;

    {
        int now_pressed = Read_Pressed();

        // 진단용 : EXTI 가 실제로 걸렸는지, 핀 레벨이 뭐로 읽히는지 확인용.
        // 버튼을 눌렀는데 이 줄 자체가 안 뜨면 EXTI 인터럽트가 아예 발생하지
        // 않는 것 -> 배선(PC9 연결, GND 공통) 문제일 가능성이 높다.
        printf("[BTN] edge processed: raw=%d (pressed=%d) prev_stable=%d\n",
               now_pressed, now_pressed, s_stable_pressed);

        if (now_pressed && !s_stable_pressed)
        {
            s_press_event = 1; // 눌리는 순간만 이벤트. 떼는 건 무시
        }
        s_stable_pressed = now_pressed;
    }
}

int Button_Take_Press(void)
{
    if (!s_press_event) return 0;
    s_press_event = 0;
    return 1;
}
