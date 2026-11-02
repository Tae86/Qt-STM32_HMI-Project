#ifndef SERIALLINK_H
#define SERIALLINK_H

#include <QByteArray>
#include <QObject>
#include <QString>

class QSerialPort;
class QTimer;

// STM32 와의 USART2 통신 (115200-8-N-1).
//
// 프로토콜
//   STM32 -> Qt : $D,<온도>,<습도>,<거리>,<속도>   주기 보고 (500ms)
//                 $V,<F|B|L|R|S>                   주행 상태 변경
//                 $C,<mode>,<on>                   공조 상태 변경
//                 $B                               버튼 눌림
//   Qt -> STM32 : $E,<0|1>   비상등
//                 $M,<F|B|L|R|S>  주행,  $F,<0|1|2> 공조
//                 $H          하트비트 (200ms 주기. 500ms 무응답 시 STM32 가 안전 정지)
//
// QSerialPort 는 비동기(readyRead)로 동작하므로 별도 스레드가 필요 없다.
class SerialLink : public QObject
{
    Q_OBJECT
public:
    static constexpr int kBaudRate = 115200;
    static constexpr int kHeartbeatMs = 200;

    explicit SerialLink(QObject *parent = nullptr);
    ~SerialLink() override;

    // portName 이 비어 있으면 ST-Link 가상 COM 포트를 자동으로 찾는다.
    bool open(const QString &portName = QString());
    void close();
    bool isOpen() const;
    QString portName() const;

    void sendHazard(bool on);              // $E,0 / $E,1
    void sendDrive(char direction);        // $M,F  (F/B/L/R/S)
    void sendFanMode(int mode);            // $F,0 / $F,1 / $F,2

signals:
    void connected(const QString &portName);
    void disconnected(const QString &reason);
    // $D 수신. 온도(°C), 습도(%), 거리(cm), 속도(%)
    void sensorReceived(double temperature, double humidity, int distanceCm, int speedPercent);
    void driveStateReceived(char direction);        // $V
    void fanStateReceived(int mode, bool running);  // $C
    void buttonPressed();                           // $B

private:
    void handleReadyRead();
    void parseLine(const QByteArray &line);
    void sendLine(const QByteArray &line);

    QSerialPort *m_port;
    QTimer *m_heartbeat;
    QByteArray m_buffer;   // 개행 기준으로 모았다가 한 줄씩 처리
};

#endif // SERIALLINK_H