#include "device_driver.h"
#include <stdio.h>

/*
 * main.c - 차량 제어 시스템 최상위 로직
 *
 * 구조 : 하드웨어 드라이버(motor/led/sonar/dht11/button/comm)는
 * 각자 파일로 분리하고, "무엇을 언제 할지" 결정하는 정책은 전부
 * 여기 main.c 의 정적 함수들이 담당한다. 메인 루프는 주기마다
 * 조건문으로 필요한 함수를 호출하는 형태로 구성했다.
 *
 * 전체 while(1) 안에 while 폴링 대기는 단 한 줄도 없다. 모든 시간
 * 판단은 Tick_Get_Ms() 값을 "비교"하는 방식이고, 실제 이벤트는
 * 전부 인터럽트(TIM4 1ms 틱, USART2 수신, EXTI 초음파/DHT11/버튼)로 들어온다.
 *
 * [정리 내역] 가변저항(ADC)과 공조 모터/스위치 관련 코드를 전부 제거했다.
 *   - adc.c 삭제
 *   - motor.c 에서 Motor_Fan_Set/Get, PB10 설정 제거
 *   - comm.c 에서 $F 명령, $C 응답(Comm_Send_Fan_State) 제거
 *   - 여기서 s_fan_mode/s_fan_on, FAN_*_TENTHS, On_Fan_Cmd, Recompute_Fan 제거
 */

#define TASK_FAST_MS     (20UL)   // 초음파/버튼/DHT11 진행/LED/페일세이프
#define SENSOR_PERIOD_MS (2500UL) // DHT11 재측정 요청 주기 (최소 1초 요구 대비 여유)
#define REPORT_PERIOD_MS (500UL)  // Qt 로 상태 패킷 전송 주기
#define LINK_TIMEOUT_MS  (500UL)  // 이 시간 무응답이면 안전 정지

#define WHEEL_SPEED_FIXED (100)   // 바퀴 전진/후진 고정 듀티비

static DriveState s_drive = DRIVE_STOP;
static int s_link_lost = 0;
static int s_test_mode = 0; // 1이면 페일세이프 끔 (Tera Term 수동 테스트용)
static int s_report_on = 1; // 0이면 주기 $D 보고를 안 보냄 (타이핑할 때 화면 밀림 방지)

static void Sys_Init(int baud);
static void Check_Failsafe(unsigned long now_ms);

void Main(void)
{
    unsigned long now, t_fast, t_sensor, t_report;

    Sys_Init(115200);
    Timer_Init();
    Motor_Init();
    Led_Init();
    Sonar_Init();
    Dht11_Init();
    Button_Init();
    Comm_Init();

    printf("\n========================================\n");
    printf(" Vehicle Control - Interrupt Driven\n");
    printf(" Wheels(L298N) + LED4 + Sonar + DHT11 + Button\n");
    printf("========================================\n");
    printf(" [Tera Term 수동 테스트]\n");
    printf(" 먼저 $X,1 을 입력하세요 (페일세이프 해제, 1회만)\n");
    printf(" 화면이 계속 밀리면 $R,0 으로 주기 보고를 끄세요\n");
    printf("   $M,F / B / L / R / S   주행\n");
    printf("   $E,1 / $E,0            비상등\n");
    printf("   $R,0 / $R,1            주기 $D 보고 끄기/켜기\n");
    printf("   $X,0                   테스트 종료 (페일세이프 복귀)\n");
    printf(" 명령 뒤에는 Enter. 입력한 글자가 안 보이면\n");
    printf(" Setup > Terminal > Local echo 를 켜세요.\n");
    printf("========================================\n");
    printf(" [버튼 진단] 버튼을 누르면 [BTN] 로그가 뜹니다.\n");
    printf(" 이 로그가 안 뜨면 배선/풀업 문제입니다.\n");
    printf("========================================\n");

    // 부팅 직후 기준 상태를 Qt 에 한 번 알려준다
    Led_Set_Drive_State(s_drive);
    Comm_Send_Drive_State('S');

    now = Tick_Get_Ms();
    t_fast = t_sensor = t_report = now;

    for (;;)
    {
        // 수신 파싱은 매 루프마다 최대한 자주
        Comm_Task();

        now = Tick_Get_Ms();

        // ---- 20ms : 빠른 주기 작업 ----
        if ((now - t_fast) >= TASK_FAST_MS)
        {
            t_fast = now;

            Sonar_Task(now);
            Button_Task(now);
            Dht11_Task(now);
            Led_Task(now);
            Check_Failsafe(now);

            Motor_Drive(s_drive, WHEEL_SPEED_FIXED);

            if (Button_Take_Press())
            {
                printf("[BTN] press detected -> sending $B\n"); // 진단용
                Comm_Send_Button();
            }
        }

        // ---- 2.5초 : 온습도 재측정 요청 ----
        if ((now - t_sensor) >= SENSOR_PERIOD_MS)
        {
            t_sensor = now;
            Dht11_Start(); // 비차단. 결과는 이후 EXTI 로 비동기 수신됨
        }

        // ---- 500ms : Qt 로 상태 패킷 전송 ----
        if ((now - t_report) >= REPORT_PERIOD_MS)
        {
            t_report = now;

            if (s_report_on)
            {
                Comm_Send_Report(Dht11_Get_Temp_Tenths(),
                                 Dht11_Get_Humi_Tenths(),
                                 Sonar_Get_Cm(),
                                 WHEEL_SPEED_FIXED);
            }
        }
    }
}

static void Sys_Init(int baud)
{
    SCB->CPACR |= (0x3 << 10 * 2) | (0x3 << 11 * 2); // FPU 활성화
    Clock_Init();
    Uart2_Init(baud);
    setvbuf(stdout, NULL, _IONBF, 0);
}

/* ================= Qt 명령 콜백 (comm.c 가 호출) ================= */

void On_Drive_Cmd(char dir)
{
    DriveState new_state;

    switch (dir)
    {
    case 'F': new_state = DRIVE_FORWARD;  break;
    case 'B': new_state = DRIVE_BACKWARD; break;
    case 'L': new_state = DRIVE_LEFT;     break;
    case 'R': new_state = DRIVE_RIGHT;    break;
    case 'S': new_state = DRIVE_STOP;     break;
    default:  return; // 알 수 없는 값은 무시
    }

    printf("[RX] M,%c\n", dir); // 수신 확인 (테라텀에서 바로 보임)

    if (new_state == s_drive) return; // 변화 없으면 아무 것도 안 한다

    s_drive = new_state;
    Led_Set_Drive_State(s_drive);
    Comm_Send_Drive_State(dir);
}

void On_Hazard_Cmd(int on)
{
    printf("[RX] E,%d\n", on);
    Led_Set_Hazard(on);
}

/*
 * 테스트 모드 - Tera Term에서 사람이 손으로 타이핑할 때만 쓴다.
 *
 * 원래 페일세이프는 Qt 가 200ms 마다 하트비트를 계속 보내는 걸 전제로
 * 짰기 때문에, 사람이 명령 하나 치고 결과를 지켜보는 순간 500ms 가
 * 지나 자동으로 정지해버린다. $X,1 로 이 검사를 잠깐 꺼서 수동 테스트를
 * 편하게 하고, 실제 Qt 연동 시에는 절대 이 명령을 보내지 않으면 된다.
 */
void On_Test_Mode_Cmd(int on)
{
    s_test_mode = on ? 1 : 0;
    printf("[RX] X,%d -> test mode %s (failsafe %s)\n",
           on, s_test_mode ? "ON" : "OFF", s_test_mode ? "disabled" : "active");
}

/*
 * 주기 $D 보고 껐다 켜기 - 순전히 Tera Term 화면이 안 밀리게 하려는 용도.
 *
 * 타이핑하는 도중에도 500ms마다 $D 패킷이 계속 들어오면 입력 중인 줄이
 * 자꾸 밀려서 치기 어렵다. $R,0 으로 잠깐 끄고 명령을 입력한 뒤,
 * 값 변화를 다시 보고 싶으면 $R,1 로 켠다. 페일세이프와는 무관하고
 * 주기 보고 자체만 켜고 끈다.
 */
void On_Report_Toggle(int on)
{
    s_report_on = on ? 1 : 0;
    printf("[RX] R,%d -> periodic report %s\n", on, s_report_on ? "ON" : "OFF");
}

/* ================= 내부 로직 ================= */

/*
 * 통신 두절 안전 정지.
 *
 * STM32 는 마지막으로 받은 명령을 계속 수행하므로, USB 가 빠지거나
 * Qt 가 죽으면 "전진" 상태의 차가 그대로 계속 움직인다. 500ms 동안
 * 아무 수신도 없으면 강제로 정지 상태로 되돌린다.
 */
static void Check_Failsafe(unsigned long now_ms)
{
    if (s_test_mode) return; // Tera Term 수동 테스트 중에는 검사하지 않는다

    if ((now_ms - Comm_Last_Rx_Ms()) > LINK_TIMEOUT_MS)
    {
        if (!s_link_lost)
        {
            s_link_lost = 1;

            if (s_drive != DRIVE_STOP)
            {
                s_drive = DRIVE_STOP;
                Led_Set_Drive_State(s_drive);
                Comm_Send_Drive_State('S');
            }
        }
    }
    else
    {
        s_link_lost = 0;
    }
}
