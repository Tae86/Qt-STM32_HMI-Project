#include "device_driver.h"

#define RX_RING_SIZE (128U)
#define LINE_MAX     (32)

static volatile unsigned char s_ring[RX_RING_SIZE];
static volatile unsigned int s_head = 0;
static volatile unsigned int s_tail = 0;
static volatile unsigned long s_last_rx_ms = 0;

static char s_line[LINE_MAX];
static int s_line_len = 0;

void Comm_Init(void)
{
    s_head = 0;
    s_tail = 0;
    s_line_len = 0;
    s_last_rx_ms = Tick_Get_Ms();

    Uart2_RX_Interrupt_Enable(1);
}

void Comm_RX_Push(unsigned char data)
{
    unsigned int next = (s_head + 1U) % RX_RING_SIZE;

    if (next != s_tail) // 가득 차면 버린다 (덮어쓰기 방지)
    {
        s_ring[s_head] = data;
        s_head = next;
    }
    s_last_rx_ms = Tick_Get_Ms();
}

unsigned long Comm_Last_Rx_Ms(void) { return s_last_rx_ms; }

static int Parse_Int(const char *s)
{
    int v = 0;
    int neg = 0;

    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return neg ? -v : v;
}

static void Parse_Line(char *line)
{
    char type;
    const char *arg;

    if (line[0] != '$') return;

    type = line[1];
    arg = (line[2] == ',') ? &line[3] : 0;

    switch (type)
    {
    case 'M': if (arg) On_Drive_Cmd(arg[0]);            break;
    case 'E': if (arg) On_Hazard_Cmd(Parse_Int(arg));    break;
    case 'X': if (arg) On_Test_Mode_Cmd(Parse_Int(arg)); break;
    case 'R': if (arg) On_Report_Toggle(Parse_Int(arg)); break;
    case 'H': /* 수신 시각은 이미 Comm_RX_Push 가 기록함 */ break;
    default:  break;
    }
}

void Comm_Task(void)
{
    while (s_tail != s_head)
    {
        char c = (char)s_ring[s_tail];
        s_tail = (s_tail + 1U) % RX_RING_SIZE;

        if (c == '\n' || c == '\r')
        {
            if (s_line_len > 0)
            {
                s_line[s_line_len] = '\0';
                Parse_Line(s_line);
                s_line_len = 0;
            }
        }
        else if (s_line_len < (LINE_MAX - 1))
        {
            s_line[s_line_len++] = c;
        }
        else
        {
            s_line_len = 0; // 너무 길면 버린다
        }
    }
}

/* ---------------- 송신 ---------------- */

static void Send_Str(const char *s)
{
    while (*s) Uart2_Send_Byte(*s++);
}

// 부호 있는 정수 -> 문자열
static int Fmt_Int(char *dst, int v)
{
    char tmp[8];
    int n = 0, len = 0;
    unsigned int uv;

    if (v < 0) { dst[len++] = '-'; uv = (unsigned int)(-v); }
    else       { uv = (unsigned int)v; }

    do { tmp[n++] = (char)('0' + (uv % 10U)); uv /= 10U; } while (uv);
    while (n) dst[len++] = tmp[--n];
    return len;
}

// 0.1 단위 정수 -> "24.6" 형태
static int Fmt_Tenths(char *dst, short v)
{
    int len = 0;
    int whole, frac;

    if (v < 0) { dst[len++] = '-'; v = (short)(-v); }
    whole = v / 10;
    frac = v % 10;

    len += Fmt_Int(dst + len, whole);
    dst[len++] = '.';
    dst[len++] = (char)('0' + frac);
    return len;
}

void Comm_Send_Drive_State(char dir)
{
    char buf[8];
    int n = 0;
    buf[n++] = '$'; buf[n++] = 'V'; buf[n++] = ',';
    buf[n++] = dir; buf[n++] = '\n'; buf[n] = '\0';
    Send_Str(buf);
}

void Comm_Send_Report(short temp_tenths, short humi_tenths, unsigned short dist_cm, int speed_pct)
{
    char buf[40];
    int n = 0;
    buf[n++] = '$'; buf[n++] = 'D'; buf[n++] = ',';
    n += Fmt_Tenths(buf + n, temp_tenths);
    buf[n++] = ',';
    n += Fmt_Tenths(buf + n, humi_tenths);
    buf[n++] = ',';
    n += Fmt_Int(buf + n, (int)dist_cm);
    buf[n++] = ',';
    n += Fmt_Int(buf + n, speed_pct);
    buf[n++] = '\n'; buf[n] = '\0';
    Send_Str(buf);
}

void Comm_Send_Button(void)
{
    Send_Str("$B\n");
}
