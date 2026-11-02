#ifndef RECOGNITIONTHREAD_H
#define RECOGNITIONTHREAD_H

#include <QImage>
#include <QMetaType>
#include <QMutex>
#include <QThread>
#include <opencv2/core.hpp>

#include "framesource.h"

class FaceEngine;

// 사용자 카메라의 CaptureThread 를 공유해서 얼굴을 검출/인식한다.
// DB 는 직접 건드리지 않는다. QSqlDatabase 커넥션은 만든 스레드에서만 써야 하므로,
// 저장할 얼굴과 인증 결과는 시그널로 UI 스레드에 넘긴다.
class RecognitionThread : public QThread
{
    Q_OBJECT
public:
    enum Mode { Idle, Recognize, Register };

    static constexpr int kDetectEvery = 2;    // N 프레임마다 1회 검출
    static constexpr int kSampleEvery = 3;    // 등록 시 검출 N회마다 1장 저장
    static constexpr int kMatchStreak = 5;    // 연속 N회 같은 사람이면 인증 확정

    RecognitionThread(FrameSource *source, FaceEngine *engine, int fps = 15,
                      QObject *parent = nullptr);

    void startRecognize();
    void startRegister(int sampleTarget);
    void setIdle();
    Mode mode() const;
    void stop();

signals:
    void sendImage(const QImage &image);
    void sendStatus(const QString &text);
    void authConfirmed(int driverId, double confidence);
    void sampleCaptured(const cv::Mat &grayFace, int count, int target);
    void registerFinished(int count);

protected:
    void run() override;

private:
    void recognizeFace(const cv::Mat &face);
    void collectSample(const cv::Mat &face);

    FrameSource *m_source;
    FaceEngine *m_engine;
    int m_intervalMs;
    bool m_running = true;

    mutable QMutex m_mutex;
    Mode m_mode = Idle;
    int m_sampleTarget = 20;
    int m_sampleCount = 0;

    int m_frameNo = 0;
    int m_detectNo = 0;
    int m_pendingId = -1;
    int m_streak = 0;
    int m_authId = -1;
    QString m_boxLabel;
};

// 스레드 경계를 넘는 시그널 인자로 쓰려면 메타타입 등록이 필요하다.
// 이게 없으면 queued connection 이 조용히 실패해서 슬롯이 호출되지 않는다.
Q_DECLARE_METATYPE(cv::Mat)

#endif // RECOGNITIONTHREAD_H