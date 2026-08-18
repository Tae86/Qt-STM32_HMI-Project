#include "device_driver.h"

#define LED_BLINK_MS (400U)

// PC4=GL, PC5=GR, PC6=RL, PC7=RR
#define PIN_GL 4
#define PIN_GR 5
#define PIN_RL 6
#define PIN_RR 7

static DriveState s_state = DRIVE_STOP;
static int s_hazard = 0;
static int s_blink_phase = 0;
static unsigned long s_last_toggle = 0;

static void Set4(int gl, int gr, int rl, int rr)
{
    gl ? Macro_Set_Bit(GPIOC->ODR, PIN_GL) : Macro_Clear_Bit(GPIOC->ODR, PIN_GL);
    gr ? Macro_Set_Bit(GPIOC->ODR, PIN_GR) : Macro_Clear_Bit(GPIOC->ODR, PIN_GR);
    rl ? Macro_Set_Bit(GPIOC->ODR, PIN_RL) : Macro_Clear_Bit(GPIOC->ODR, PIN_RL);
    rr ? Macro_Set_Bit(GPIOC->ODR, PIN_RR) : Macro_Clear_Bit(GPIOC->ODR, PIN_RR);
}

// 현재 상태(비상등/주행상태/점멸위상)를 실제 LED 출력에 반영
static void Apply(void)
{
    if (s_hazard)
    {
        Set4(s_blink_phase, s_blink_phase, s_blink_phase, s_blink_phase);
        return;
    }

    switch (s_state)
    {
    case DRIVE_FORWARD:  Set4(1, 1, 0, 0); break;
    case DRIVE_BACKWARD: Set4(0, 0, 1, 1); break;
    case DRIVE_LEFT:     Set4(s_blink_phase, 0, s_blink_phase, 0); break;
    case DRIVE_RIGHT:    Set4(0, s_blink_phase, 0, s_blink_phase); break;
    case DRIVE_STOP:
    default:             Set4(0, 0, 0, 0); break;
    }
}

void Led_Init(void)
{
    Macro_Set_Bit(RCC->AHB1ENR, 2); // GPIOC 클럭
    Macro_Write_Block(GPIOC->MODER, 0xFFU, 0x55U, 8U); // PC4~PC7 출력

    s_state = DRIVE_STOP;
    s_hazard = 0;
    s_blink_phase = 0;
    s_last_toggle = 0;

    Set4(0, 0, 0, 0);
}

void Led_Set_Drive_State(DriveState state)
{
    s_state = state;

    // 좌/우회전으로 새로 진입하는 순간 즉시 켜진 것처럼 보이도록
    // 위상을 1로 리셋한다 (반응이 빨라 보인다).
    if (!s_hazard && (state == DRIVE_LEFT || state == DRIVE_RIGHT))
    {
        s_blink_phase = 1;
        s_last_toggle = 0;
    }

    Apply();
}

void Led_Set_Hazard(int on)
{
    on = on ? 1 : 0;
    if (s_hazard == on) return;

    s_hazard = on;

    if (s_hazard)
    {
        s_blink_phase = 1;
        s_last_toggle = 0;
    }

    Apply(); // 꺼지면 이 시점의 주행 상태 표시로 즉시 복귀
}

int Led_Get_Hazard(void) { return s_hazard; }

void Led_Task(unsigned long now_ms)
{
    int blinking_now = s_hazard || (s_state == DRIVE_LEFT) || (s_state == DRIVE_RIGHT);
    if (!blinking_now) return;

    if (s_last_toggle == 0UL)
    {
        s_last_toggle = now_ms;
        return;
    }

    if ((now_ms - s_last_toggle) >= LED_BLINK_MS)
    {
        s_last_toggle = now_ms;
        s_blink_phase = !s_blink_phase;
        Apply();
    }
}
