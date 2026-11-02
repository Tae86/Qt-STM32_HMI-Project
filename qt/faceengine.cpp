#include "faceengine.h"
#include "databasemanager.h"

#include <QCoreApplication>
#include <QFile>
#include <QMutexLocker>
#include <QStringList>
#include <algorithm>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace {
QString findCascadeFile()
{
    QStringList candidates;
    candidates << QCoreApplication::applicationDirPath() + "/haarcascade_frontalface_default.xml"
               << "C:/msys64/ucrt64/share/opencv4/haarcascades/haarcascade_frontalface_default.xml"
               << "/usr/share/opencv4/haarcascades/haarcascade_frontalface_default.xml"
               << "/usr/local/share/opencv4/haarcascades/haarcascade_frontalface_default.xml";
    for (const QString &path : candidates)
        if (QFile::exists(path))
            return path;
    return QString();
}
}

bool FaceEngine::initialize(const QString &modelPath, QString *errorMessage)
{
    m_modelPath = modelPath;

    const QString cascade = findCascadeFile();
    if (cascade.isEmpty() || !m_detector.load(cascade.toStdString())) {
        if (errorMessage)
            *errorMessage = QStringLiteral(
                "haarcascade_frontalface_default.xml 을 찾지 못했습니다.\n"
                "실행 파일과 같은 폴더에 복사하세요.");
        return false;
    }

    m_recognizer = cv::face::LBPHFaceRecognizer::create();

    // 이전에 학습해 둔 모델이 있으면 불러온다
    if (QFile::exists(m_modelPath)) {
        try {
            QMutexLocker locker(&m_mutex);
            m_recognizer->read(m_modelPath.toStdString());
            m_trained = true;
        } catch (const cv::Exception &) {
            m_trained = false;
        }
    }
    return true;
}

QVector<cv::Rect> FaceEngine::detect(const cv::Mat &bgrFrame, cv::Mat &grayOut)
{
    QVector<cv::Rect> result;
    if (bgrFrame.empty())
        return result;

    cv::cvtColor(bgrFrame, grayOut, cv::COLOR_BGR2GRAY);

    // 입력 해상도와 무관하게 검출용 이미지 가로를 kDetectWidth 로 맞춘다
    const double scale = std::min(1.0, double(kDetectWidth) / grayOut.cols);
    cv::Mat small;
    if (scale < 1.0)
        cv::resize(grayOut, small, cv::Size(), scale, scale, cv::INTER_AREA);
    else
        small = grayOut;
    cv::equalizeHist(small, small);

    const int minSide = std::max(24, small.cols / 8);
    std::vector<cv::Rect> faces;
    m_detector.detectMultiScale(small, faces, 1.1, 5, 0, cv::Size(minSide, minSide));

    const double inverse = 1.0 / scale;
    for (const cv::Rect &rect : faces)
        result.append(cv::Rect(int(rect.x * inverse), int(rect.y * inverse),
                               int(rect.width * inverse), int(rect.height * inverse)));
    return result;
}

cv::Rect FaceEngine::largest(const QVector<cv::Rect> &rects)
{
    cv::Rect best;
    for (const cv::Rect &rect : rects)
        if (rect.area() > best.area())
            best = rect;
    return best;   // 없으면 area() == 0
}

cv::Mat FaceEngine::cropFace(const cv::Mat &gray, const cv::Rect &rect)
{
    if (gray.empty() || rect.area() <= 0)
        return cv::Mat();

    const cv::Rect safe = rect & cv::Rect(0, 0, gray.cols, gray.rows);
    if (safe.area() <= 0)
        return cv::Mat();

    cv::Mat face;
    cv::resize(gray(safe), face, cv::Size(kFaceSize, kFaceSize));
    cv::equalizeHist(face, face);
    return face;
}

bool FaceEngine::train(const DatabaseManager &database, QString *errorMessage)
{
    std::vector<cv::Mat> images;
    std::vector<int> labels;

    const QVector<QPair<int, QString>> samples = database.faceSamplePaths();
    for (const QPair<int, QString> &sample : samples) {
        const cv::Mat image = cv::imread(sample.second.toStdString(), cv::IMREAD_GRAYSCALE);
        if (image.empty())
            continue;
        images.push_back(image);
        labels.push_back(sample.first);
    }

    QMutexLocker locker(&m_mutex);
    if (images.size() < 2) {
        m_trained = false;
        if (errorMessage)
            *errorMessage = QStringLiteral("학습할 얼굴 샘플이 부족합니다.");
        return false;
    }
    m_recognizer->train(images, labels);
    m_recognizer->write(m_modelPath.toStdString());
    m_trained = true;
    locker.unlock();

    setDriverNames(database.driverNames());
    return true;
}

bool FaceEngine::isTrained() const
{
    QMutexLocker locker(&m_mutex);
    return m_trained;
}

bool FaceEngine::identify(const cv::Mat &face, int *driverId, double *confidence)
{
    if (driverId) *driverId = -1;
    if (confidence) *confidence = -1.0;
    if (face.empty())
        return false;

    QMutexLocker locker(&m_mutex);
    if (!m_trained)
        return false;

    int label = -1;
    double distance = 0.0;
    m_recognizer->predict(face, label, distance);
    if (confidence) *confidence = distance;

    if (distance > m_threshold)
        return false;   // 미등록

    if (driverId) *driverId = label;
    return true;
}

void FaceEngine::setDriverNames(const QHash<int, QString> &names)
{
    QMutexLocker locker(&m_nameMutex);
    m_names = names;
}

QString FaceEngine::driverName(int driverId) const
{
    QMutexLocker locker(&m_nameMutex);
    return m_names.value(driverId, QStringLiteral("ID %1").arg(driverId));
}
