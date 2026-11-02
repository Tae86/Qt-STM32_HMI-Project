#include "recognitionthread.h"
#include "displaythread.h"   // matToQImage
#include "faceengine.h"

#include <QMutexLocker>
#include <opencv2/imgproc.hpp>

RecognitionThread::RecognitionThread(FrameSource *source, FaceEngine *engine, int fps,
                                     QObject *parent)
    : QThread(parent), m_source(source), m_engine(engine),
      m_intervalMs(qMax(1, 1000 / qMax(1, fps)))
{
}

void RecognitionThread::startRecognize()
{
    QMutexLocker locker(&m_mutex);
    m_pendingId = -1;
    m_streak = 0;
    m_authId = -1;
    m_boxLabel.clear();
    m_mode = Recognize;
}

void RecognitionThread::startRegister(int sampleTarget)
{
    QMutexLocker locker(&m_mutex);
    m_sampleTarget = sampleTarget;
    m_sampleCount = 0;
    m_boxLabel.clear();
    m_mode = Register;
}

void RecognitionThread::setIdle()
{
    QMutexLocker locker(&m_mutex);
    m_mode = Idle;
}

RecognitionThread::Mode RecognitionThread::mode() const
{
    QMutexLocker locker(&m_mutex);
    return m_mode;
}

void RecognitionThread::run()
{
    cv::Rect lastFace;

    while (m_running) {
        cv::Mat frame = m_source->readLatest();
        if (frame.empty()) {
            msleep(10);
            continue;
        }
        cv::flip(frame, frame, 1);   // 거울 모드 (사용자 카메라)

        ++m_frameNo;
        if (m_frameNo % kDetectEvery == 0) {
            cv::Mat gray;
            const QVector<cv::Rect> rects = m_engine->detect(frame, gray);
            lastFace = FaceEngine::largest(rects);
            ++m_detectNo;

            const Mode current = mode();
            if (lastFace.area() > 0) {
                const cv::Mat face = FaceEngine::cropFace(gray, lastFace);
                if (current == Register)
                    collectSample(face);
                else if (current == Recognize)
                    recognizeFace(face);
            } else {
                m_streak = 0;
                m_boxLabel.clear();
                if (current != Idle)
                    emit sendStatus(QStringLiteral("얼굴을 찾을 수 없습니다"));
            }
        }

        if (lastFace.area() > 0) {
            cv::rectangle(frame, lastFace, cv::Scalar(0, 200, 0), 2);
        }

        emit sendImage(matToQImage(frame));
        msleep(m_intervalMs);
    }
}

void RecognitionThread::recognizeFace(const cv::Mat &face)
{
    int driverId = -1;
    double confidence = -1.0;
    const bool matched = m_engine->identify(face, &driverId, &confidence);

    if (!matched) {
        m_streak = 0;
        m_pendingId = -1;
        m_boxLabel = QStringLiteral("unknown");
        emit sendStatus(confidence >= 0
                            ? QStringLiteral("미등록 운전자 (score %1)").arg(confidence, 0, 'f', 0)
                            : QStringLiteral("미등록 운전자"));
        return;
    }

    m_streak = (driverId == m_pendingId) ? m_streak + 1 : 1;
    m_pendingId = driverId;

    const QString name = m_engine->driverName(driverId);
    m_boxLabel = name;
    emit sendStatus(QStringLiteral("%1 (score %2, %3/%4)")
                        .arg(name).arg(confidence, 0, 'f', 0).arg(m_streak).arg(kMatchStreak));

    if (m_streak >= kMatchStreak && m_authId != driverId) {
        m_authId = driverId;
        emit authConfirmed(driverId, confidence);   // DB 기록은 UI 스레드에서
    }
}

void RecognitionThread::collectSample(const cv::Mat &face)
{
    if (face.empty() || m_detectNo % kSampleEvery != 0)
        return;

    ++m_sampleCount;
    m_boxLabel = QStringLiteral("%1/%2").arg(m_sampleCount).arg(m_sampleTarget);
    emit sampleCaptured(face.clone(), m_sampleCount, m_sampleTarget);

    if (m_sampleCount >= m_sampleTarget) {
        setIdle();
        emit registerFinished(m_sampleCount);
    }
}

void RecognitionThread::stop()
{
    m_running = false;
    wait(2000);
}
