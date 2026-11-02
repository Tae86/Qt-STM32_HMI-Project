#include "device_driver.h"

#define DHT_START_LOW_MS   (18U)  // 데이터시트 : 최소 18ms
#define DHT_EDGE_TIMEOUT_MS (6U)  // 이 시간 안에 다음 엣지가 안 오면 실패 처리

typedef enum
{
    DHT_IDLE = 0,
    DHT_START_LOW,   // MCU가 능동적으로 Low 유지 중 (Task 에서 시간 진행)
    DHT_RELEASE,     // 라인을 놓은 직후, 저항에 의해 뜨는 첫 상승엣지(잡음성) 대기
    DHT_ACK_FALL,    // 센서가 진짜로 Low 를 시작하는 하강엣지 대기
    DHT_ACK_LOW,     // 80us Low 가 끝나는 상승엣지 대기
    DHT_ACK_HIGH,    // 80us High 가 끝나는 하강엣지 대기 (= bit0 Low 시작)
    DHT_BIT_LOW,     // 현재 비트의 Low 가 끝나는 상승엣지 대기
    DHT_BIT_HIGH,    // 현재 비트의 High 가 끝나는 하강엣지 대기 (비트 확정)
    DHT_DONE,
    DHT_ERROR
} DhtPhase;

static volatile DhtPhase s_phase = DHT_IDLE;
static volatile unsigned short s_last_edge_us = 0;
static volatile unsigned long s_last_edge_ms = 0;
static unsigned long s_start_low_ms = 0;

static unsigned char s_bit_index = 0;
static unsigned char s_bytes[5] = { 0, 0, 0, 0, 0 };
static unsigned short s_low_ref_us = 0;

static short s_temp_tenths = 250; // 초기 표시값 25.0도 (아직 못 읽었을 때)
static short s_humi_tenths = 500;
static unsigned long s_fail_count = 0;

static void Set_Output_Low(void)
{
    Macro_Write_Block(GPIOA->MODER, 0x3U, 0x1U, 16U); // PA8 출력
    Macro_Clear_Bit(GPIOA->ODR, 8);
}

static void Set_Input_Release(void)
{
    Macro_Write_Block(GPIOA->MODER, 0x3U, 0x0U, 16U); // PA8 입력
}

void Dht11_Init(void)
{
    Macro_Set_Bit(RCC->AHB1ENR, 0);  // GPIOA 클럭
    Macro_Set_Bit(RCC->APB2ENR, 14); // SYSCFG 클럭

    Set_Input_Release();
    Macro_Write_Block(GPIOA->PUPDR, 0x3U, 0x1U, 16U); // 내부 풀업 (보조)
    Macro_Write_Block(GPIOA->OTYPER, 0x1U, 0x1U, 8U); // 오픈드레인 (출력 시)

    // EXTI8 -> 포트 A  (EXTICR[2] 비트[3:0], 포트A = 0x0)
    Macro_Write_Block(SYSCFG->EXTICR[2], 0xFU, 0x0U, 0U);

    Macro_Set_Bit(EXTI->RTSR, 8);
    Macro_Set_Bit(EXTI->FTSR, 8);
    Macro_Set_Bit(EXTI->IMR, 8);

    NVIC_ClearPendingIRQ(23); // EXTI9_5 (버튼과 공유, 핀 번호로 구분)
    NVIC_EnableIRQ(23);

    s_phase = DHT_IDLE;
}

void Dht11_Start(void)
{
    if (s_phase != DHT_IDLE && s_phase != DHT_DONE && s_phase != DHT_ERROR) return;

    s_bit_index = 0;
    s_bytes[0] = s_bytes[1] = s_bytes[2] = s_bytes[3] = s_bytes[4] = 0;

    Set_Output_Low();
    s_start_low_ms = Tick_Get_Ms();
    s_phase = DHT_START_LOW;
}

void Dht11_Task(unsigned long now_ms)
{
    if (s_phase == DHT_START_LOW)
    {
        if ((now_ms - s_start_low_ms) >= DHT_START_LOW_MS)
        {
            EXTI->PR = (1U << 8); // 혹시 남은 펜딩 정리
            Set_Input_Release();
            s_last_edge_ms = now_ms;
            s_phase = DHT_RELEASE;
        }
        return;
    }

    // 엣지를 기다리는 중인 단계에서 너무 오래 아무 반응이 없으면 실패 처리
    if (s_phase == DHT_RELEASE || s_phase == DHT_ACK_FALL || s_phase == DHT_ACK_LOW ||
        s_phase == DHT_ACK_HIGH || s_phase == DHT_BIT_LOW || s_phase == DHT_BIT_HIGH)
    {
        if ((now_ms - s_last_edge_ms) > DHT_EDGE_TIMEOUT_MS)
        {
            s_phase = DHT_ERROR;
            s_fail_count++;
        }
    }
}

void Dht11_Edge_ISR(void)
{
    unsigned short now_us;
    unsigned short elapsed;

    if (!Macro_Check_Bit_Set(EXTI->PR, 8)) return;
    EXTI->PR = (1U << 8);

    now_us = Tick_Get_Us();
    elapsed = (unsigned short)(now_us - s_last_edge_us);
    s_last_edge_us = now_us;
    s_last_edge_ms = Tick_Get_Ms();

    switch (s_phase)
    {
    case DHT_RELEASE:
        s_phase = DHT_ACK_FALL; // 저항에 의한 첫 상승은 그냥 흘려보낸다
        break;

    case DHT_ACK_FALL:
        s_phase = DHT_ACK_LOW; // 센서가 진짜로 Low 를 시작함
        break;

    case DHT_ACK_LOW:
        s_phase = DHT_ACK_HIGH; // 80us Low 종료
        break;

    case DHT_ACK_HIGH:
        // 80us High 종료 = bit0 의 Low 시작
        s_phase = DHT_BIT_LOW;
        s_bit_index = 0;
        break;

    case DHT_BIT_LOW:
        s_low_ref_us = elapsed; // 이 비트의 Low 길이 (~50us) 기록
        s_phase = DHT_BIT_HIGH;
        break;

    case DHT_BIT_HIGH:
    {
        // High 가 Low 보다 길면 1, 짧으면 0
        // (고정 임계값 대신 직전 Low 와 비교 -> 클럭 오차가 자동 상쇄된다)
        unsigned char bit_val = (elapsed > s_low_ref_us) ? 1U : 0U;
        unsigned char byte_i = (unsigned char)(s_bit_index >> 3);
        unsigned char bit_pos = (unsigned char)(7 - (s_bit_index & 7));

        if (bit_val)
            s_bytes[byte_i] |= (unsigned char)(1U << bit_pos);
        else
            s_bytes[byte_i] &= (unsigned char)~(1U << bit_pos);

        s_bit_index++;

        if (s_bit_index >= 40U)
        {
            unsigned char sum = (unsigned char)(s_bytes[0] + s_bytes[1] + s_bytes[2] + s_bytes[3]);

            if (sum == s_bytes[4])
            {
                s_humi_tenths = (short)((short)s_bytes[0] * 10 + (short)(s_bytes[1] % 10U));
                s_temp_tenths = (short)((short)s_bytes[2] * 10 + (short)(s_bytes[3] % 10U));
                s_phase = DHT_DONE;
            }
            else
            {
                s_phase = DHT_ERROR;
                s_fail_count++;
            }
        }
        else
        {
            s_phase = DHT_BIT_LOW;
        }
        break;
    }

    default:
        break; // IDLE / START_LOW / DONE / ERROR 상태의 엣지는 무시 (잡음)
    }
}

short Dht11_Get_Temp_Tenths(void) { return s_temp_tenths; }
short Dht11_Get_Humi_Tenths(void) { return s_humi_tenths; }
unsigned long Dht11_Get_Fail_Count(void) { return s_fail_count; }
