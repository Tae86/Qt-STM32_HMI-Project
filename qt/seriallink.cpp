#include "seriallink.h"

#include <QDebug>
#include <QDebug>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QTimer>

SerialLink::SerialLink(QObject *parent)
    : QObject(parent), m_port(new QSerialPort(this)), m_heartbeat(new QTimer(this))
{
    connect(m_port, &QSerialPort::readyRead, this, &SerialLink::handleReadyRead);
    connect(m_port, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError error) {
        if (error == QSerialPort::NoError)
            return;
        const QString reason = m_port->errorString();
        if (m_port->isOpen())
            close();
        emit disconnected(reason);
    });

    // 하트비트를 끊으면 STM32 가 500ms 뒤 안전 정지에 들어간다
    m_heartbeat->setInterval(kHeartbeatMs);
    connect(m_heartbeat, &QTimer::timeout, this, [this]() { sendLine("$H"); });
}

SerialLink::~SerialLink()
{
    close();
}

bool SerialLink::open(const QString &portName)
{
    close();

    QString target = portName;
    if (target.isEmpty()) {
        // ST-Link 가상 COM 포트 자동 탐색 (STMicroelectronics VID = 0x0483)
        for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts()) {
            if (info.hasVendorIdentifier() && info.vendorIdentifier() == 0x0483) {
                target = info.portName();
                break;
            }
        }
        // 못 찾으면 첫 번째 포트를 시도한다
        if (target.isEmpty()) {
            const auto ports = QSerialPortInfo::availablePorts();
            if (!ports.isEmpty())
                target = ports.first().portName();
        }
    }

    if (target.isEmpty()) {
        emit disconnected(tr("사용 가능한 시리얼 포트가 없습니다."));
        return false;
    }

    m_port->setPortName(target);
    m_port->setBaudRate(kBaudRate);
    m_port->setDataBits(QSerialPort::Data8);
    m_port->setParity(QSerialPort::NoParity);
    m_port->setStopBits(QSerialPort::OneStop);
    m_port->setFlowControl(QSerialPort::NoFlowControl);

    if (!m_port->open(QIODevice::ReadWrite)) {
        emit disconnected(tr("%1 열기 실패: %2").arg(target, m_port->errorString()));
        return false;
    }

    m_buffer.clear();
    m_heartbeat->start();
    emit connected(target);
    return true;
}

void SerialLink::close()
{
    m_heartbeat->stop();
    if (m_port->isOpen())
        m_port->close();
}

bool SerialLink::isOpen() const { return m_port->isOpen(); }
QString SerialLink::portName() const { return m_port->portName(); }

void SerialLink::handleReadyRead()
{
    m_buffer.append(m_port->readAll());

    // 한 번의 readyRead 로 한 줄이 통째로 오지 않는다. 개행 단위로 잘라 쓴다.
    int index;
    while ((index = m_buffer.indexOf('\n')) >= 0) {
        QByteArray line = m_buffer.left(index);
        m_buffer.remove(0, index + 1);
        line = line.trimmed();
        if (!line.isEmpty())
            parseLine(line);
    }

    if (m_buffer.size() > 512)   // 개행 없이 계속 쌓이면 버린다
        m_buffer.clear();
}

void SerialLink::parseLine(const QByteArray &line)
{
    // $D 는 500ms 마다 오므로 로그에서 제외하고, 나머지 수신 줄은 모두 찍는다.
    // (STM32 버튼 $B 가 실제로 도착하는지 확인하는 용도)
    if (!line.startsWith("$D"))
        qDebug() << "[RX]" << line;

    // 부팅 메시지 등 '$' 로 시작하지 않는 줄은 무시한다
    if (!line.startsWith('$') || line.size() < 2)
        return;

    const char type = line.at(1);
    const QList<QByteArray> parts = line.mid(2).split(',');   // 첫 항목은 빈 값

    switch (type) {
    case 'D':   // $D,온도,습도,거리,속도
        if (parts.size() >= 5) {
            emit sensorReceived(parts.at(1).toDouble(), parts.at(2).toDouble(),
                                parts.at(3).toInt(), parts.at(4).toInt());
        }
        break;
    case 'V':   // $V,F
        if (parts.size() >= 2 && !parts.at(1).isEmpty())
            emit driveStateReceived(parts.at(1).at(0));
        break;
    case 'C':   // $C,mode,on
        if (parts.size() >= 3)
            emit fanStateReceived(parts.at(1).toInt(), parts.at(2).toInt() != 0);
        break;
    case 'B':
        emit buttonPressed();
        break;
    default:
        break;
    }
}

void SerialLink::sendLine(const QByteArray &line)
{
    if (!m_port->isOpen())
        return;
    m_port->write(line + '\n');
}

void SerialLink::sendHazard(bool on)
{
    sendLine(on ? "$E,1" : "$E,0");
}

void SerialLink::sendDrive(char direction)
{
    sendLine(QByteArray("$M,") + direction);
}

void SerialLink::sendFanMode(int mode)
{
    sendLine("$F," + QByteArray::number(mode));
}