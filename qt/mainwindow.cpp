#include "mainwindow.h"
#include "capturethread.h"
#include "databasemanager.h"
#include "displaythread.h"
#include "driverlistdialog.h"
#include "faceengine.h"
#include "recognitionthread.h"
#include "seriallink.h"
#include "ui_mainwindow.h"

#include <QDate>
#include <QDir>
#include <QFileInfo>
#include <QCloseEvent>
#include <QDebug>
#include <QSignalBlocker>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QListWidget>
#include <QPixmap>
#include <opencv2/imgcodecs.hpp>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QSqlQueryModel>
#include <QStatusBar>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_databaseManager(new DatabaseManager)
{
    ui->setupUi(this);
    const QDate today = QDate::currentDate();
    ui->startDateTimeEdit->setDateTime(QDateTime(today.addDays(-1), QTime(0, 0)));
    ui->endDateTimeEdit->setDateTime(QDateTime(today, QTime(23, 59, 59)));
    ui->resultTableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    QString error;
    if (!m_databaseManager->openDatabase(&error) || !m_databaseManager->initializeSchema(&error)) {
        QMessageBox::critical(this, tr("DB 연결 오류"), error);
        ui->databaseTab->setEnabled(false);
        return;
    }
    statusBar()->showMessage(tr("DB 연결됨: %1").arg(m_databaseManager->databasePath()));
    connect(ui->searchButton, &QPushButton::clicked, this, &MainWindow::queryDatabase);

    m_databaseReady = true;
    if (!m_databaseManager->insertSystemEvent(QStringLiteral("SYSTEM_START_REQUEST"), &error)) {
        showDatabaseError(tr("시작 이벤트 저장"), error);
        return;
    }

    queryDatabase();

    setupCameras();
    setupFaceRecognition();
    setupSerial();
    setupDriveButtons();

    // 시나리오: 탑승 -> 운전자 인식 -> 인증 완료 -> 계기판
    // 시작 화면은 운전자 인식 탭이다. ('인식 시작' 버튼이 문 열림을 대신한다)
    goToPage(ui->pageDriver);
    setDoorOpen(false);   // 차량 버튼($B)을 받기 전에는 카메라도 인식도 시작하지 않는다
    setDrivingEnabled(false);   // 인증 전에는 주행 조작 불가
}

MainWindow::~MainWindow()
{
    // 소비자(표시) 먼저, 생산자(캡처) 나중에
    if (m_recognizer)  m_recognizer->stop();
    if (m_userView)    m_userView->stop();
    if (m_rearView)    m_rearView->stop();
    if (m_userCapture) m_userCapture->stop();
    if (m_rearCapture) m_rearCapture->stop();

    delete m_queryModel;
    delete m_faceEngine;
    delete m_databaseManager;
    delete ui;
}

void MainWindow::setupCameras()
{
    // ---- 사용자(내장) 카메라 : 운전자 인식 탭 ----
    m_userCapture = new CaptureThread(kUserCamSrc, 640, 480, this);
    m_userView = new DisplayThread(m_userCapture, 30, /*mirror=*/true, this);
    connect(m_userCapture, &CaptureThread::opened, this, &MainWindow::onCameraOpened);
    connect(m_userView, &DisplayThread::sendImage, this, [this](const QImage &image) {
        if (m_userVideoActive)   // 카메라를 끈 뒤 늦게 도착한 프레임은 버린다
            showImage(ui->lblCamInternal, image);
    });

    // ---- 후방(USB) 카메라 : 후방 카메라 탭 ----
    m_rearCapture = new CaptureThread(kRearCamSrc, 640, 480, this);
    m_rearView = new DisplayThread(m_rearCapture, 30, /*mirror=*/true, this);
    connect(m_rearCapture, &CaptureThread::opened, this, &MainWindow::onCameraOpened);
    connect(m_rearView, &DisplayThread::sendImage, this,
            [this](const QImage &image) { showImage(ui->lblCamRear, image); });

    // 사용자 카메라는 상시 동작하지 않는다.
    // $B(문 열림) 또는 인식/등록 버튼을 누를 때만 켠다. 소비자 스레드는 미리 띄워둔다.
    m_userView->start();
    m_rearCapture->start();
    m_rearView->start();

    ui->btnCamRearToggle->setChecked(true);
    ui->btnCamRearToggle->setText(tr("후방 카메라 끄기"));
    connect(ui->btnCamRearToggle, &QPushButton::toggled, this, &MainWindow::toggleRearCamera);
}

void MainWindow::showImage(QLabel *label, const QImage &image)
{
    if (image.isNull())
        return;
    // 고정 크기로 스케일한다. label->size() 를 기준으로 삼으면
    // 픽스맵이 커질 때마다 라벨 sizeHint 도 커져 화면이 계속 확대된다.
    label->setPixmap(QPixmap::fromImage(image).scaled(m_videoSize, Qt::KeepAspectRatio,
                                                      Qt::SmoothTransformation));
}

void MainWindow::onCameraOpened(bool ok, const QString &message)
{
    auto *capture = qobject_cast<CaptureThread *>(sender());
    const bool isRear = capture && capture->source() == kRearCamSrc;

    if (ok) {
        logEvent(isRear ? QStringLiteral("REAR_CAMERA_ON") : QStringLiteral("USER_CAMERA_ON"));
    } else {
        (isRear ? ui->lblCamRear : ui->lblCamInternal)->setText(message);
        logEvent(QStringLiteral("CAMERA_ERROR"));
    }
    statusBar()->showMessage(message, 5000);
}

void MainWindow::toggleRearCamera(bool checked)
{
    m_rearView->setPaused(!checked);
    ui->btnCamRearToggle->setText(checked ? tr("후방 카메라 끄기") : tr("후방 카메라 켜기"));
    if (!checked)
        ui->lblCamRear->setText(tr("후방 카메라 꺼짐"));
    logEvent(checked ? QStringLiteral("REAR_CAMERA_ON") : QStringLiteral("REAR_CAMERA_OFF"));
}

void MainWindow::logEvent(const QString &eventType)
{
    if (!m_databaseReady)
        return;
    QString error;
    if (!m_databaseManager->insertSystemEvent(eventType, &error))
        showDatabaseError(tr("%1 이벤트 저장").arg(eventType), error);
    else
        refreshDatabaseView();
}

// ---------------- 얼굴 인식 ----------------

void MainWindow::setupFaceRecognition()
{
    m_faceEngine = new FaceEngine;
    QString error;
    if (!m_faceEngine->initialize(
            QFileInfo(m_databaseManager->databasePath()).absolutePath() + "/lbph_model.yml",
            &error)) {
        QMessageBox::warning(this, tr("얼굴 인식 초기화 실패"), error);
        ui->grpDriverManage->setEnabled(false);
        return;
    }
    m_faceEngine->setDriverNames(m_databaseManager->driverNames());
    m_faceEngine->train(*m_databaseManager);
    m_faceReady = true;

    m_recognizer = new RecognitionThread(m_userCapture, m_faceEngine, 15, this);
    connect(m_recognizer, &RecognitionThread::sendImage, this, [this](const QImage &image) {
        if (m_userVideoActive)
            showImage(ui->lblCamInternal, image);
    });
    connect(m_recognizer, &RecognitionThread::sendStatus, this,
            [this](const QString &text) { ui->lblRecogResult->setText(tr("인식 결과: %1").arg(text)); });
    connect(m_recognizer, &RecognitionThread::authConfirmed, this, &MainWindow::onAuthConfirmed);
    connect(m_recognizer, &RecognitionThread::sampleCaptured, this, &MainWindow::onSampleCaptured);
    connect(m_recognizer, &RecognitionThread::registerFinished, this, &MainWindow::onRegisterFinished);
    m_recognizer->start();

    connect(ui->btnRecogToggle, &QPushButton::toggled, this, &MainWindow::toggleRecognition);
    connect(ui->btnRegisterFace, &QPushButton::clicked, this, &MainWindow::startRegistration);
    connect(ui->btnDriverList, &QPushButton::clicked, this, &MainWindow::showDriverList);

    statusBar()->showMessage(tr("등록 운전자 %1명 / 얼굴 모델 학습됨: %2")
                                 .arg(m_databaseManager->listDrivers().size())
                                 .arg(m_faceEngine->isTrained() ? tr("예") : tr("아니오")), 5000);
}

void MainWindow::uncheckRecogToggle()
{
    // toggled(bool) 처리 중에 그대로 setChecked() 를 부르면 재진입이 발생한다.
    // 신호를 막고 상태만 되돌린 뒤 표시를 맞춘다.
    QSignalBlocker blocker(ui->btnRecogToggle);
    ui->btnRecogToggle->setChecked(false);
    ui->btnRecogToggle->setText(tr("인식 시작"));
}

void MainWindow::toggleRecognition(bool checked)
{
    if (!m_faceReady)
        return;

    if (checked) {
        if (!m_faceEngine->isTrained()) {
            statusBar()->showMessage(tr("등록된 얼굴이 없습니다. 먼저 얼굴을 등록하세요."), 5000);
            uncheckRecogToggle();
            return;
        }
        startUserCamera();                  // 필요할 때만 카메라를 연다
        m_userView->setPaused(true);        // 표시 스레드와 화면 충돌 방지
        m_recognizer->startRecognize();
        ui->btnRecogToggle->setText(tr("인식 중지"));
        logEvent(QStringLiteral("FACE_DETECTION_START"));
    } else {
        stopUserCamera();
        ui->lblRecogResult->setText(tr("인식 결과: -"));
        ui->btnRecogToggle->setText(tr("인식 시작"));
    }
}

void MainWindow::onAuthConfirmed(int driverId, double confidence)
{
    QString error;
    if (!m_databaseManager->insertAuthLog(driverId, confidence, &error)) {
        showDatabaseError(tr("인증 로그 저장"), error);
        return;
    }
    logEvent(QStringLiteral("FACE_DETECTED"));

    const QString name = m_databaseManager->driverName(driverId);
    ui->lblAuth->setText(tr("인증: %1").arg(name));
    statusBar()->showMessage(tr("%1 인증 완료 (score %2)").arg(name).arg(confidence, 0, 'f', 0), 5000);

    // 인증이 끝나면 인식을 멈추고 계기판으로 넘어간다.
    // setChecked(false) 가 toggleRecognition() 을 호출해 표시 스레드도 되살린다.
    ui->btnRecogToggle->setChecked(false);
    setDoorOpen(false);   // 인증이 끝나면 카메라를 끄고 다시 잠근다
    setDrivingEnabled(true);

    QMessageBox::information(this, tr("운전자 인증"),
                             tr("%1 님, 환영합니다.").arg(name));
    goToPage(ui->pageDashboard);
}

void MainWindow::startRegistration()
{
    if (!m_faceReady)
        return;

    const QString name = ui->editDriverName->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(this, tr("안내"), tr("운전자 이름을 입력하세요."));
        return;
    }
    if (m_recognizer->mode() == RecognitionThread::Register)
        return;

    QString error;
    m_registerDriverId = m_databaseManager->addDriver(name, &error);
    if (m_registerDriverId < 0) {
        QMessageBox::critical(this, tr("등록 실패"), error);
        return;
    }
    m_registerName = name;

    ui->btnRecogToggle->setChecked(false);
    ui->btnRecogToggle->setEnabled(false);
    startUserCamera();                  // 등록 시작과 함께 카메라를 연다
    m_userView->setPaused(true);
    m_recognizer->startRegister(kSampleTarget);
    ui->lblRecogResult->setText(tr("정면을 봐주세요 (0/%1)").arg(kSampleTarget));
}

void MainWindow::onSampleCaptured(const cv::Mat &grayFace, int count, int target)
{
    // DB 쓰기와 파일 저장은 UI 스레드에서 처리한다
    const QString dir = QStringLiteral("%1/%2").arg(m_databaseManager->faceDirectory())
                            .arg(m_registerDriverId);
    QDir().mkpath(dir);
    const QString path = QStringLiteral("%1/%2.png").arg(dir)
                             .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz"));

    if (!cv::imwrite(path.toStdString(), grayFace)) {
        showDatabaseError(tr("얼굴 샘플 저장"), tr("이미지 파일 저장 실패: %1").arg(path));
        return;
    }
    QString error;
    if (!m_databaseManager->insertFaceSample(m_registerDriverId, path, &error)) {
        showDatabaseError(tr("얼굴 샘플 저장"), error);
        return;
    }
    ui->lblRecogResult->setText(tr("%1 등록 중 %2/%3").arg(m_registerName).arg(count).arg(target));
}

void MainWindow::onRegisterFinished(int count)
{
    ui->btnRecogToggle->setEnabled(true);
    stopUserCamera();          // 표시 정지 -> 카메라 정지 -> 화면 정리 순서로 처리한다
    retrainFaceModel();
    logEvent(QStringLiteral("DRIVER_REGISTERED"));
    ui->editDriverName->clear();
    ui->lblRecogResult->setText(tr("인식 결과: -"));
    QMessageBox::information(this, tr("등록 완료"),
                             tr("'%1' 얼굴 샘플 %2장을 등록했습니다.").arg(m_registerName).arg(count));
}

void MainWindow::showDriverList()
{
    DriverListDialog dialog(m_databaseManager, this);
    connect(&dialog, &DriverListDialog::driverDeleted, this, &MainWindow::retrainFaceModel);
    dialog.exec();
}

// ---------------- STM32 연동 ----------------

void MainWindow::setupSerial()
{
    m_serial = new SerialLink(this);

    connect(m_serial, &SerialLink::connected, this, &MainWindow::onSerialConnected);
    connect(m_serial, &SerialLink::disconnected, this, &MainWindow::onSerialDisconnected);
    connect(m_serial, &SerialLink::sensorReceived, this, &MainWindow::onSensorReceived);
    connect(m_serial, &SerialLink::driveStateReceived, this, &MainWindow::onDriveStateReceived);
    connect(m_serial, &SerialLink::fanStateReceived, this, &MainWindow::onFanStateReceived);
    connect(m_serial, &SerialLink::buttonPressed, this, &MainWindow::onDoorButtonPressed);
    connect(ui->btnHazard, &QPushButton::toggled, this, &MainWindow::onHazardToggled);

    m_serial->open();   // 포트 이름을 비우면 ST-Link 가상 COM 을 자동 탐색
}

void MainWindow::onSerialConnected(const QString &portName)
{
    logEvent(QStringLiteral("STM32_CONNECTED"));
    statusBar()->showMessage(tr("STM32 연결됨: %1").arg(portName), 5000);

    // 현재 비상등 상태를 STM32 에 맞춰 보낸다
    m_serial->sendHazard(ui->btnHazard->isChecked());
}

void MainWindow::onSerialDisconnected(const QString &reason)
{
    logEvent(QStringLiteral("SERIAL_ERROR"));
    statusBar()->showMessage(tr("STM32 연결 실패/해제: %1").arg(reason), 8000);
}

void MainWindow::onSensorReceived(double temperature, double humidity,
                                  int distanceCm, int speedPercent)
{
    m_speedPercent = speedPercent;
    m_distanceCm = distanceCm;
    updateDistance(distanceCm);
    logVehicleState();

    ui->lblCabinTemp->setText(QStringLiteral("%1 °C").arg(temperature, 0, 'f', 1));
    ui->lblHumidity->setText(tr("습도 %1 %").arg(humidity, 0, 'f', 1));
    ui->lblStripTemp->setText(QStringLiteral("%1 °C / %2 %")
                                  .arg(temperature, 0, 'f', 1).arg(humidity, 0, 'f', 1));

    // $D 는 500ms 주기로 오지만 DB 는 1초에 한 번만 남긴다
    const QDateTime now = QDateTime::currentDateTime();
    if (m_lastSensorLog.isValid() && m_lastSensorLog.msecsTo(now) < 1000)
        return;
    m_lastSensorLog = now;

    QString error;
    if (!m_databaseManager->insertSensorLog(temperature, humidity, m_fanState, &error))
        showDatabaseError(tr("센서 로그 저장"), error);
    else
        refreshDatabaseView();
}

void MainWindow::updateDistance(int distanceCm)
{
    // 초음파는 후방 1개만 사용한다.
    // (UI 의 objectName 은 lblUltraFront / pbUltraFront 지만 표시 항목은 '후방'이다)
    const bool valid = (distanceCm > 0);
    const QString text = valid ? tr("%1 cm").arg(distanceCm) : tr("--- cm");

    ui->lblUltraFront->setText(text);
    ui->pbUltraFront->setValue(qBound(ui->pbUltraFront->minimum(), distanceCm,
                                      ui->pbUltraFront->maximum()));

    ui->lblRearWarning->setText(tr("후방 거리: %1").arg(text));
    // 30cm 이내면 경고 색으로 표시한다
    ui->lblRearWarning->setStyleSheet(
        (valid && distanceCm <= 30) ? QStringLiteral("color: #e04b2a; font-weight: bold;")
                                    : QString());
}

void MainWindow::setupDriveButtons()
{
    // 현재는 좌/우회전만 STM32 로 보낸다.
    // $M,L / $M,R 을 받으면 STM32 가 해당 방향 LED 를 점멸시킨다 (led.c 의 DRIVE_LEFT/RIGHT)
    connect(ui->btnLeft,  &QPushButton::clicked, this, [this]() { sendDrive('L'); });
    connect(ui->btnRight, &QPushButton::clicked, this, [this]() { sendDrive('R'); });

    // 전진/후진/정지는 모터 연동이 준비되면 아래 주석을 풀면 된다
    connect(ui->btnUp,   &QPushButton::clicked, this, [this]() { sendDrive('F'); });
    connect(ui->btnDown, &QPushButton::clicked, this, [this]() { sendDrive('B'); });
    connect(ui->btnStop, &QPushButton::clicked, this, [this]() { sendDrive('S'); });
}

void MainWindow::sendDrive(char direction)
{
    if (!m_authenticated) {   // 미인증 상태에서는 주행 명령을 보내지 않는다
        statusBar()->showMessage(tr("운전자 인증이 필요합니다"), 3000);
        return;
    }
    m_serial->sendDrive(direction);
    // 실제 상태 표시는 STM32 가 보내는 $V 응답으로 갱신한다
}

void MainWindow::startUserCamera()
{
    qDebug() << "[CAM] startUserCamera 호출 - 현재 동작중:" << m_userCapture->isRunning();
    m_userVideoActive = true;
    if (m_userCapture->isRunning())
        return;

    // stop() 직후에는 스레드가 아직 종료 중일 수 있다. 확실히 끝난 뒤 다시 시작한다.
    m_userCapture->wait(2000);
    m_userCapture->start();
}

void MainWindow::stopUserCamera()
{
    qDebug() << "[CAM] stopUserCamera 호출";

    // 순서가 중요하다. 먼저 표시를 끊어야 마지막 프레임이 화면에 남지 않는다.
    m_userVideoActive = false;
    m_userView->setPaused(true);
    m_recognizer->setIdle();

    if (m_userCapture->isRunning())
        m_userCapture->stop();

    ui->lblCamInternal->clear();                 // 남아있는 픽스맵 제거
    ui->lblCamInternal->setText(tr("대기 중"));
}

void MainWindow::setDoorOpen(bool open)
{
    m_doorOpen = open;

    // 문이 열리기 전에는 인식/등록 버튼을 쓸 수 없다
    ui->btnRecogToggle->setEnabled(open);
    ui->btnRegisterFace->setEnabled(open);
    ui->editDriverName->setEnabled(open);

    if (!open) {
        ui->btnRecogToggle->setChecked(false);
        stopUserCamera();
        ui->lblRecogResult->clear();   // 문이 닫힌 상태의 화면은 아무도 보지 않는다
    }
}

void MainWindow::onDoorButtonPressed()
{
    // 수신 여부를 바로 확인할 수 있도록 먼저 표시한다
    statusBar()->showMessage(tr("차량 문 열림 신호 수신 ($B)"), 3000);
    qDebug() << "[$B] 문 열림 버튼 수신 - 인증 상태:" << m_authenticated
             << "/ 얼굴 모델 학습됨:" << (m_faceEngine && m_faceEngine->isTrained());
    logEvent(QStringLiteral("DOOR_BUTTON_PRESSED"));

    if (m_authenticated) {   // 이미 인증된 상태면 다시 인증하지 않는다
        statusBar()->showMessage(tr("이미 인증된 상태입니다"), 3000);
        return;
    }

    // 인식을 바로 시작하지는 않는다. 등록이 필요한 경우도 있으므로
    // 운전자 인식 화면을 열고 버튼만 사용할 수 있게 풀어준다.
    goToPage(ui->pageDriver);
    setDoorOpen(true);
}

void MainWindow::onDriveStateReceived(char direction)
{
    switch (direction) {
    case 'F': m_direction = QStringLiteral("FWD");   break;
    case 'B': m_direction = QStringLiteral("BACK");  break;
    case 'L': m_direction = QStringLiteral("LEFT");  break;
    case 'R': m_direction = QStringLiteral("RIGHT"); break;
    case 'S': m_direction = QStringLiteral("STOP");  break;
    default: return;
    }
    ui->lblSpeed->setText(tr("%1  %2 cm/s").arg(m_direction).arg(m_speedPercent));
    logVehicleState();
}

void MainWindow::onFanStateReceived(int mode, bool running)
{
    m_fanState = running ? QStringLiteral("ON") : QStringLiteral("OFF");

    // 0=OFF 1=ON 2=AUTO. UI 버튼 상태를 STM32 보고에 맞춘다
    QPushButton *button = (mode == 1) ? ui->btnFanOn : (mode == 2) ? ui->btnFanAuto : ui->btnFanOff;
    if (button && !button->isChecked())
        button->setChecked(true);
}

void MainWindow::logVehicleState()
{
    if (!m_databaseReady)
        return;

    // 방향이 바뀌었거나 속도가 5% 이상 달라졌을 때만 기록한다.
    // ($D 는 500ms 마다 오므로 매번 저장하면 로그가 의미 없이 쌓인다)
    if (m_loggedSpeed >= 0 && qAbs(m_speedPercent - m_loggedSpeed) < 5
            && m_direction == m_lastLoggedDirection) {
        return;
    }
    m_loggedSpeed = m_speedPercent;
    m_lastLoggedDirection = m_direction;

    QString error;
    if (!m_databaseManager->insertVehicleLog(m_direction, m_speedPercent, m_distanceCm, &error))
        showDatabaseError(tr("차량 로그 저장"), error);
    else
        refreshDatabaseView();
}

void MainWindow::onHazardToggled(bool checked)
{
    m_serial->sendHazard(checked);
    ui->btnHazard->setText(checked ? tr("비상등 끄기") : tr("비상등"));
    statusBar()->showMessage(checked ? tr("비상등 ON") : tr("비상등 OFF"), 3000);
}

void MainWindow::goToPage(QWidget *page)
{
    const int index = ui->stackMain->indexOf(page);
    if (index < 0)
        return;
    ui->navList->setCurrentRow(index);   // nav 선택이 바뀌면 stackMain 도 따라간다
}

void MainWindow::setDrivingEnabled(bool enabled)
{
    m_authenticated = enabled;

    // 인증 = 시동. 인증 전에는 운전자 인식 탭 외에는 아무것도 쓸 수 없다.
    // 페이지 단위로 잠그므로 나중에 위젯을 추가해도 이 함수는 그대로 둔다.
    for (int i = 0; i < ui->stackMain->count(); ++i) {
        QWidget *page = ui->stackMain->widget(i);

        // 인증 전에는 운전자 인식 탭만, 인증 후에는 그 탭만 잠근다.
        // 인증이 끝나면 다시 인식할 필요가 없기 때문이다.
        const bool pageEnabled = (page == ui->pageDriver) ? !enabled : enabled;
        page->setEnabled(pageEnabled);

        // 잠긴 페이지는 목록에서도 눌리지 않게 한다
        if (QListWidgetItem *item = ui->navList->item(i)) {
            Qt::ItemFlags flags = item->flags();
            flags.setFlag(Qt::ItemIsEnabled, pageEnabled);
            item->setFlags(flags);
        }
    }

    ui->lblMode->setText(enabled ? tr("모드: 수동") : tr("모드: 시동 꺼짐 (미인증)"));
    if (!enabled)
        ui->lblAuth->setText(tr("인증: 미인증"));
}

void MainWindow::retrainFaceModel()
{
    QString error;
    const bool ok = m_faceEngine->train(*m_databaseManager, &error);
    m_faceEngine->setDriverNames(m_databaseManager->driverNames());
    statusBar()->showMessage(ok ? tr("얼굴 모델 재학습 완료") : error, 5000);
}

void MainWindow::queryDatabase()
{
    if (ui->startDateTimeEdit->dateTime() > ui->endDateTimeEdit->dateTime()) {
        QMessageBox::warning(this, tr("조회 조건 오류"), tr("시작 시간은 종료 시간보다 늦을 수 없습니다."));
        return;
    }
    QString error;
    QSqlQueryModel *model = m_databaseManager->queryLogs(ui->logTypeComboBox->currentIndex(),
        ui->startDateTimeEdit->dateTime(), ui->endDateTimeEdit->dateTime(), this, &error);
    if (!model) { QMessageBox::critical(this, tr("DB 조회 오류"), error); return; }
    delete m_queryModel;
    m_queryModel = model;
    ui->resultTableView->setModel(m_queryModel);
    ui->resultCountLabel->setText(tr("조회 결과: %1건").arg(m_queryModel->rowCount()));
}

void MainWindow::refreshDatabaseView()
{
    if (ui->stackMain->currentWidget() == ui->databaseTab)
        queryDatabase();
}

void MainWindow::showDatabaseError(const QString &operation, const QString &error)
{
    m_databaseReady = false;
    QMessageBox::critical(this, tr("DB 저장 오류"), tr("%1에 실패했습니다.\n%2").arg(operation, error));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_databaseReady && !m_stopEventSaved) {
        QString error;
        if (!m_databaseManager->insertSystemEvent(QStringLiteral("SYSTEM_STOP"), &error)) {
            QMessageBox::critical(this, tr("DB 저장 오류"),
                                  tr("종료 이벤트 저장에 실패했습니다.\n%1").arg(error));
            event->ignore();
            return;
        }
        m_stopEventSaved = true;
    }
    QMainWindow::closeEvent(event);
}
