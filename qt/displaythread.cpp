#include "displaythread.h"

#include <opencv2/imgproc.hpp>

QImage matToQImage(const cv::Mat &bgr)
{
    if (bgr.empty())
        return QImage();

    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    // cv::Mat 이 먼저 소멸해도 안전하도록 반드시 copy()
    return QImage(rgb.data, rgb.cols, rgb.rows,
                  static_cast<int>(rgb.step), QImage::Format_RGB888).copy();
}

DisplayThread::DisplayThread(FrameSource *source, int fps, bool mirror, QObject *parent)
    : QThread(parent), m_source(source), m_intervalMs(qMax(1, 1000 / qMax(1, fps))),
      m_mirror(mirror)
{
}

void DisplayThread::run()
{
    while (m_running) {
        if (m_paused) {
            msleep(100);
            continue;
        }
        cv::Mat frame = m_source->readLatest();
        if (frame.empty()) {
            msleep(10);
            continue;
        }
        if (m_mirror)
            cv::flip(frame, frame, 1);

        emit sendImage(matToQImage(frame));
        msleep(m_intervalMs);
    }
}

void DisplayThread::stop()
{
    m_running = false;
    wait(2000);
}
