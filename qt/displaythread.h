#pragma once
#include <QImage>
#include <QThread>
#include <opencv2/core.hpp>

#include "framesource.h"

QImage matToQImage(const cv::Mat &bgr);   // BGR -> QImage (깊은 복사)

// 최신 프레임을 일정 주기로 UI 에 보내는 소비자.
class DisplayThread : public QThread
{
    Q_OBJECT
public:
    explicit DisplayThread(FrameSource *source, int fps = 30, bool mirror = false,
                           QObject *parent = nullptr);

    void setPaused(bool paused) { m_paused = paused; }
    void stop();

signals:
    void sendImage(const QImage &image);

protected:
    void run() override;

private:
    FrameSource *m_source;
    int m_intervalMs;
    bool m_mirror;
    bool m_running = true;
    bool m_paused = false;
};
