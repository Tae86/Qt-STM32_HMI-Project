#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QDateTime>
#include <QMainWindow>
#include <QSize>
#include <QString>
#include <opencv2/core.hpp>

class CaptureThread;
class DatabaseManager;
class DisplayThread;
class FaceEngine;
class RecognitionThread;
class SerialLink;
class QCloseEvent;
class QImage;
class QLabel;
class QProgressBar;
class QSqlQueryModel;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // list_cameras 로 확인한 인덱스 (카메라 2대)
    static constexpr int kUserCamSrc = 0;   // 내장 카메라 -> 운전자 인식 탭
    static constexpr int kRearCamSrc = 1;   // USB 카메라  -> 후방 카메라 탭
    static constexpr int kSampleTarget = 20;   // 등록 시 모을 얼굴 샘플 수

    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void queryDatabase();
    void setupCameras();
    void showImage(QLabel *label, const QImage &image);
    void onCameraOpened(bool ok, const QString &message);
    void toggleRearCamera(bool checked);
    void logEvent(const QString &eventType);

    // ---- 얼굴 인식 ----
    void setupFaceRecognition();
    void toggleRecognition(bool checked);
    void uncheckRecogToggle();   // 재진입 없이 인식 토글을 해제
    void startRegistration();
    void onSampleCaptured(const cv::Mat &grayFace, int count, int target);
    void onRegisterFinished(int count);
    void onAuthConfirmed(int driverId, double confidence);
    void showDriverList();
    void retrainFaceModel();
    void goToPage(QWidget *page);   // nav 선택과 페이지 전환을 함께 처리

    // ---- STM32 연동 ----
    void setupSerial();
    void onSerialConnected(const QString &portName);
    void onSerialDisconnected(const QString &reason);
    void onSensorReceived(double temperature, double humidity, int distanceCm, int speedPercent);
    void updateDistance(int distanceCm);    // 계기판 + 후방 카메라 탭에 동시 표시
    void setupDriveButtons();               // 좌/우회전 버튼 -> $M 명령
    void sendDrive(char direction);
    void onDriveStateReceived(char direction);
    void onFanStateReceived(int mode, bool running);
    void onHazardToggled(bool checked);
    void onDoorButtonPressed();   // $B - 차량 문 열림. 카메라를 켜고 인증을 시작한다
    void startUserCamera();
    void stopUserCamera();
    void setDoorOpen(bool open);   // 문 열림 상태에서만 인식/등록 허용
    void logVehicleState();
    void setDrivingEnabled(bool enabled);   // 미인증 상태에서 주행 조작 차단
    void refreshDatabaseView();
    void showDatabaseError(const QString &operation, const QString &error);

    Ui::MainWindow *ui;
    DatabaseManager *m_databaseManager;
    CaptureThread *m_userCapture = nullptr;
    DisplayThread *m_userView = nullptr;
    CaptureThread *m_rearCapture = nullptr;
    DisplayThread *m_rearView = nullptr;
    // 영상 표시 크기를 고정한다. 라벨 크기에 맞춰 스케일하면
    // [픽스맵 -> sizeHint -> 라벨 확대] 순환이 생겨 화면이 점점 커진다.
    const QSize m_videoSize = QSize(480, 360);

    FaceEngine *m_faceEngine = nullptr;
    RecognitionThread *m_recognizer = nullptr;
    QString m_registerName;
    int m_registerDriverId = -1;
    bool m_faceReady = false;
    bool m_doorOpen = false;        // $B 를 받아야 true
    bool m_userVideoActive = false; // 카메라를 끈 뒤 늦게 도착한 프레임을 무시하기 위한 플래그
    bool m_authenticated = false;

    SerialLink *m_serial = nullptr;
    QDateTime m_lastSensorLog;      // 센서 로그 저장 주기 제한용

    // STM32 에서 받은 최신 상태
    QString m_fanState = QStringLiteral("OFF");   // $C 로 갱신
    QString m_direction = QStringLiteral("STOP"); // $V 로 갱신
    int m_speedPercent = 0;                       // $D 로 갱신
    int m_distanceCm = 0;                         // $D 로 갱신 (후방 초음파)
    int m_loggedSpeed = -1;                       // 마지막으로 DB 에 남긴 속도
    QString m_lastLoggedDirection;

    QSqlQueryModel *m_queryModel = nullptr;
    bool m_databaseReady = false;
    bool m_stopEventSaved = false;
};
#endif // MAINWINDOW_H