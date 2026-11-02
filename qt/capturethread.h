#pragma once
#include <QMutex>
#include <QThread>
#include <opencv2/core.hpp>

#include "framesource.h"

// 카메라 한 대에서 계속 읽어 '최신 프레임'만 유지하는 생산자.
// 여러 소비자(표시 / 나중에 인식)가 같은 인스턴스를 공유할 수 있다.
class CaptureThread : public QThread, public FrameSource
{
    Q_OBJECT
public:
    static constexpr int kOpenRetries = 3;   // 열기 재시도 횟수

    explicit CaptureThread(int src, int width = 640, int height = 480, QObject *parent = nullptr);

    cv::Mat readLatest() const override;   // 복사본 반환
    void stop();
    int source() const { return m_src; }

signals:
    void opened(bool ok, const QString &message);

protected:
    void run() override;

private:
    int m_src;
    int m_width;
    int m_height;
    bool m_running = true;
    mutable QMutex m_mutex;
    cv::Mat m_frame;
};
