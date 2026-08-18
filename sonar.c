#include "device_driver.h"

#define SONAR_PERIOD_MS  (60U)
#define SONAR_TIMEOUT_US (25000U) // 약 4m
#define SONAR_NO_ECHO    (999U)

static volatile unsigned short s_echo_start = 0;
static volatile int s_measuring = 0;
static volatile int s_got_result = 0;
static unsigned short s_hist[3] = { SONAR_NO_ECHO, SONAR_NO_ECHO, SONAR_NO_ECHO };
static int s_hist_pos = 0;
static unsigned short s_filtered = SONAR_NO_ECHO;
static unsigned long s_last_fire = 0;

// 세 값의 중앙값. 튀는 값 하나를 걸러낸다.
static unsigned short Median3(unsigned short a, unsigned short b, unsigned short c)
{
    unsigned short t;
    if (a > b) { t = a; a = b; b = t; }
    if (b > c) { t = b; b = c; c = t; }
    if (a > b) { t = a; a = b; b = t; }
    return b;
}

static void Store(unsigned short cm)
{
    s_hist[s_hist_pos] = cm;
    s_hist_pos = (s_hist_pos + 1) % 3;
    s_filtered = Median3(s_hist[0], s_hist[1], s_hist[2]);
    s_got_result = 1;
}

static void Fire(void)
{
    s_measuring = 0;
    s_got_result = 0;

    Macro_Clear_Bit(GPIOC->ODR, 0);
    Tick_Spin_Us(3);
    Macro_Set_Bit(GPIOC->ODR, 0);
    Tick_Spin_Us(10); // 데이터시트 요구 : 최소 10us
    Macro_Clear_Bit(GPIOC->ODR, 0);
}

void Sonar_Init(void)
{
    Macro_Set_Bit(RCC->AHB1ENR, 2);  // GPIOC 클럭
    Macro_Set_Bit(RCC->APB2ENR, 14); // SYSCFG 클럭 (EXTI 매핑에 필요)

    // Trig : 출력
    Macro_Write_Block(GPIOC->MODER, 0x3U, 0x1U, 0U);
    Macro_Clear_Bit(GPIOC->ODR, 0);

    // Echo : 입력 + 풀다운
    Macro_Write_Block(GPIOC->MODER, 0x3U, 0x0U, 2U);
    Macro_Write_Block(GPIOC->PUPDR, 0x3U, 0x2U, 2U);

    // EXTI1 -> 포트 C  (EXTICR[0] 비트[7:4], 포트C = 0x2)
    Macro_Write_Block(SYSCFG->EXTICR[0], 0xFU, 0x2U, 4U);

    Macro_Set_Bit(EXTI->RTSR, 1); // 상승 엣지
    Macro_Set_Bit(EXTI->FTSR, 1); // 하강 엣지
    Macro_Set_Bit(EXTI->IMR, 1);

    NVIC_ClearPendingIRQ(7); // EXTI1
    NVIC_EnableIRQ(7);

    s_last_fire = Tick_Get_Ms();
    Fire();
}

void Sonar_Task(unsigned long now_ms)
{
    if (s_measuring && ((unsigned short)(Tick_Get_Us() - s_echo_start) > SONAR_TIMEOUT_US))
    {
        s_measuring = 0;
        Store(SONAR_NO_ECHO);
    }

    if ((now_ms - s_last_fire) >= SONAR_PERIOD_MS)
    {
        if (!s_got_result)
        {
            Store(SONAR_NO_ECHO); // 이번 주기에 아무 결과도 못 얻었으면 미감지 처리
        }
        s_last_fire = now_ms;
        Fire();
    }
}

void Sonar_Edge_ISR(void)
{
    if (!Macro_Check_Bit_Set(EXTI->PR, 1)) return;
    EXTI->PR = (1U << 1); // 펜딩 클리어

    if (Macro_Check_Bit_Set(GPIOC->IDR, 1))
    {
        // 상승 엣지 : 비행 시작
        s_echo_start = Tick_Get_Us();
        s_measuring = 1;
    }
    else if (s_measuring)
    {
        // 하강 엣지 : 비행 종료
        unsigned short width = (unsigned short)(Tick_Get_Us() - s_echo_start);
        s_measuring = 0;

        if (width > SONAR_TIMEOUT_US)
        {
            Store(SONAR_NO_ECHO);
        }
        else
        {
            unsigned short cm = (unsigned short)(width / 58U);
            if (cm == 0U) cm = 1U;
            if (cm > 400U) cm = SONAR_NO_ECHO;
            Store(cm);
        }
    }
}

unsigned short Sonar_Get_Cm(void) { return s_filtered; }
