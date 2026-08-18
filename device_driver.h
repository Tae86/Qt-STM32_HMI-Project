#include "stm32f4xx.h"
#include "option.h"
#include "macro.h"
#include "malloc.h"

/*
 * device_driver.h - 모든 드라이버 모듈의 선언을 모으는 단일 헤더
 *
 * 이 프로젝트의 모든 .c 파일은 이 파일 하나만 include 합니다.
 * (실습 코드 중 개별 모듈 헤더 없이 device_driver.h 하나로 통일하는
 *  스타일을 그대로 따랐습니다.)
 */

// ============================================================
// Clock.c
// ============================================================
extern void Clock_Init(void);

// ============================================================
// Uart.c
// ============================================================
extern void Uart2_Init(int baud);
extern void Uart2_Send_Byte(char data);
extern char Uart2_Receive_Byte(void);
extern char Uart2_Get_Pressed(void);
extern void Uart2_RX_Interrupt_Enable(int en);

extern void Uart1_Init(int baud);
extern void Uart1_Send_Byte(char data);
extern void Uart1_Send_String(char *pt);

// ============================================================
// Timer.c - 1ms 틱(TIM4) + 1us 프리러닝 카운터(TIM3)
// ============================================================
extern void Timer_Init(void);
extern unsigned long Tick_Get_Ms(void);
extern unsigned short Tick_Get_Us(void);
extern void Tick_Spin_Us(unsigned short us); // 수십us 이하 하드웨어 셋업 펄스 전용
extern void Tick_ISR_Handler(void);          // exception.c 의 TIM4_IRQHandler 가 호출

// ============================================================
// Motor.c - 바퀴 2개(L298N)
//
// [배선] L298N 은 채널이 2개뿐이라 바퀴 2개로 전부 소진됩니다.
//   PA0,PA1     : TIM5 CH1/CH2 PWM -> L298N ENA/ENB
//   PB12,PB13   : L298N IN1/IN2 (좌 바퀴 방향)
//   PB14,PB15   : L298N IN3/IN4 (우 바퀴 방향)
//
// 전진/후진 : main.c 의 WHEEL_SPEED_FIXED(100) 고정 듀티
// 좌/우회전 : 스키드 조향. 도는 쪽만 motor.c 의 TURN_DUTY_FIXED(90),
//             반대쪽은 완전 정지(0)
// ============================================================
typedef enum
{
    DRIVE_STOP = 0,
    DRIVE_FORWARD,
    DRIVE_BACKWARD,
    DRIVE_LEFT,
    DRIVE_RIGHT
} DriveState;

extern void Motor_Init(void);
extern void Motor_Drive(DriveState state, int duty); // duty: 0~100 (전진/후진에만 적용됨)

// ============================================================
// Led.c - LED 4개 (Green x2, Red x2)
//   PC4:GL  PC5:GR  PC6:RL  PC7:RR
//   전진=Green만 ON, 후진=Red만 ON, 좌/우회전=해당 쪽만 toggle, 정지=전부 OFF
//   비상등이 켜지면 위 규칙과 무관하게 4개 전부 toggle, 꺼지면 원래대로 복귀
// ============================================================
extern void Led_Init(void);
extern void Led_Set_Drive_State(DriveState state);
extern void Led_Set_Hazard(int on);
extern int Led_Get_Hazard(void);
extern void Led_Task(unsigned long now_ms);

// ============================================================
// Sonar.c - 초음파 1개 (HC-SR04)
//   PC0:Trig(출력)  PC1:Echo(EXTI1)
//   Echo 는 5V 출력이므로 분압 저항(1k+2k)으로 낮춰서 연결할 것
// ============================================================
extern void Sonar_Init(void);
extern void Sonar_Task(unsigned long now_ms);
extern unsigned short Sonar_Get_Cm(void); // 999 = 미감지
extern void Sonar_Edge_ISR(void);         // exception.c 의 EXTI1_IRQHandler 가 호출

// ============================================================
// Dht11.c - 온습도 센서 (단선, EXTI 인터럽트로 완전 비차단 구현)
//   PA8 (EXTI8)
// ============================================================
extern void Dht11_Init(void);
extern void Dht11_Start(void);              // 새 측정 시작 (비차단)
extern void Dht11_Task(unsigned long now_ms);
extern void Dht11_Edge_ISR(void);           // exception.c 의 EXTI9_5_IRQHandler 가 호출
extern short Dht11_Get_Temp_Tenths(void);   // 231 = 23.1도
extern short Dht11_Get_Humi_Tenths(void);
extern unsigned long Dht11_Get_Fail_Count(void);

// ============================================================
// Button.c - 외부 버튼 1개 (회로에서 연결, 온보드 버튼 아님)
//   PC9 (EXTI9)
// ============================================================
extern void Button_Init(void);
extern void Button_Task(unsigned long now_ms);
extern void Button_Edge_ISR(void); // exception.c 의 EXTI9_5_IRQHandler 가 호출
extern int Button_Take_Press(void);

// ============================================================
// Comm.c - Qt 프로토콜 (USART2, 115200)
//
//  STM32 -> Qt
//    $V,<F|B|L|R|S>\n              주행 상태 변경 시
//    $D,<온도>,<습도>,<거리>,<속도>\n   500ms 주기 (main.c 의 REPORT_PERIOD_MS)
//    $B\n                          버튼 눌림
//
//  Qt -> STM32
//    $M,<F|B|L|R|S>\n   $E,<0|1>\n   $H\n(하트비트)
//    $X,<0|1>\n  테스트 모드 (1=페일세이프 끔, 0=원래대로). Tera Term 수동
//                테스트 전용. Qt 는 이 명령을 절대 보내지 않는다.
// ============================================================
extern void Comm_Init(void);
extern void Comm_Task(void);
extern void Comm_Send_Drive_State(char dir);
extern void Comm_Send_Report(short temp_tenths, short humi_tenths, unsigned short dist_cm, int speed_pct);
extern void Comm_Send_Button(void);
extern unsigned long Comm_Last_Rx_Ms(void);
extern void Comm_RX_Push(unsigned char data); // exception.c 의 USART2_IRQHandler 가 호출

// main.c 가 구현하는 콜백 (comm.c 가 호출)
extern void On_Drive_Cmd(char dir);
extern void On_Hazard_Cmd(int on);
extern void On_Test_Mode_Cmd(int on);
extern void On_Report_Toggle(int on); // $R,0 / $R,1 : 주기 $D 보고 껐다 켜기 (테스트 전용)
