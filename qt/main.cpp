#include "mainwindow.h"
#include "recognitionthread.h"

#include <QApplication>
#include <QMetaType>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // cv::Mat 을 시그널 인자로 넘기기 위한 등록 (얼굴 샘플 전달에 필요)
    qRegisterMetaType<cv::Mat>("cv::Mat");
    MainWindow w;
    w.show();
    return a.exec();
}