#ifndef FACEENGINE_H
#define FACEENGINE_H

#include <QHash>
#include <QMutex>
#include <QString>
#include <QVector>
#include <opencv2/core.hpp>
#include <opencv2/face.hpp>
#include <opencv2/objdetect.hpp>

class DatabaseManager;

// 얼굴 검출(Haar) + 얼굴 인식(LBPH).
// identify() 는 인식 스레드에서, train() 은 UI 스레드에서 호출되므로 뮤텍스로 보호한다.
class FaceEngine
{
public:
    static constexpr int kFaceSize = 200;       // 학습/인식용 정규화 크기
    static constexpr int kDetectWidth = 320;    // 검출용으로 줄일 가로 크기
    static constexpr double kThreshold = 70.0;  // 이보다 크면 미등록 (LBPH 는 작을수록 유사)

    bool initialize(const QString &modelPath, QString *errorMessage = nullptr);

    // 프레임에서 얼굴 사각형 목록을 찾는다. grayOut 에는 원본 크기 흑백 이미지가 담긴다.
    QVector<cv::Rect> detect(const cv::Mat &bgrFrame, cv::Mat &grayOut);
    static cv::Rect largest(const QVector<cv::Rect> &rects);
    static cv::Mat cropFace(const cv::Mat &gray, const cv::Rect &rect);

    bool train(const DatabaseManager &database, QString *errorMessage = nullptr);
    bool isTrained() const;

    // 매칭되면 true. confidence 는 매칭 여부와 관계없이 채워진다.
    bool identify(const cv::Mat &face, int *driverId, double *confidence);

    void setDriverNames(const QHash<int, QString> &names);
    QString driverName(int driverId) const;

    void setThreshold(double threshold) { m_threshold = threshold; }

private:
    cv::CascadeClassifier m_detector;
    cv::Ptr<cv::face::LBPHFaceRecognizer> m_recognizer;
    QString m_modelPath;
    double m_threshold = kThreshold;
    bool m_trained = false;

    mutable QMutex m_mutex;       // m_recognizer / m_trained
    mutable QMutex m_nameMutex;   // m_names
    QHash<int, QString> m_names;
};

#endif // FACEENGINE_H
