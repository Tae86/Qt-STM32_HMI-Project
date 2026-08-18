#include "device_driver.h"

/*
 * 클럭 초기화 - HSI(16MHz) -> PLL -> SYSCLK 96MHz
 *
 * [추가된 부분] 원본 실습 코드에는 없던 전압 스케일링(VOS) 설정을 넣었습니다.
 *
 * RM0383 기준 VOS[1:0] 리셋 기본값은 Scale 2이고 최대 84MHz까지만 보장합니다.
 * 96MHz로 올리려면 Scale 1(0b11)로 바꿔야 하며, 이 비트는 PLL이 꺼져 있을 때만
 * 바꿀 수 있습니다. 그래서 PWR 클럭을 켜고 PLL을 켜기 "전에" 먼저 처리합니다.
 *
 * 플래시 대기 상태도 마찬가지로, 클럭을 올리기 전에 먼저 3 WS로 맞춰야 합니다.
 * (RM0383 Table 5 : 2.7~3.6V, 90 < HCLK <= 100MHz 구간은 3 WS)
 * 순서를 반대로 하면 플래시가 새 클럭 속도를 못 따라가 하드폴트가 납니다.
 */
void Clock_Init(void)
{
    // 1. HSI 기동 확인
    Macro_Set_Bit(RCC->CR, 0);
    while (!Macro_Check_Bit_Set(RCC->CR, 1))
        ;

    // 2. 전압 스케일 1 로 (PLL이 꺼진 지금만 가능)
    Macro_Set_Bit(RCC->APB1ENR, 28);              // PWR 클럭 활성화
    Macro_Write_Block(PWR->CR, 0x3U, 0x3U, 14U);   // VOS = 11 (Scale 1)

    // 3. 플래시 캐시 리셋 후, 3 WS + 캐시/프리페치 활성화
    FLASH->ACR = (1 << 12) | (1 << 11);                       // ICRST, DCRST
    FLASH->ACR = (1 << 10) | (1 << 9) | (1 << 8) | (0x3 << 0); // DCEN, ICEN, PRFTEN, LATENCY=3

    // 4. PLL 설정 : HSI(16MHz) / PLLM(8) * PLLN(192) / PLLP(4) = 96MHz
    RCC->PLLCFGR = (8 << 24) | (0 << 22) | (1 << 16) | (192 << 6) | (8 << 0);

    Macro_Set_Bit(RCC->CR, 24);
    while (!Macro_Check_Bit_Set(RCC->CR, 25))
        ;

    // 5. 버스 분주비 : AHB /1(96MHz), APB1 /2(48MHz), APB2 /1(96MHz)
    RCC->CFGR = (0 << 13) | (4 << 10) | (0 << 4);

    // 6. 시스템 클럭을 PLL 로 전환
    Macro_Write_Block(RCC->CFGR, 0x3, 0x2, 0);
    while (Macro_Extract_Area(RCC->CFGR, 0x3, 2) != 0x2)
        ;
}
