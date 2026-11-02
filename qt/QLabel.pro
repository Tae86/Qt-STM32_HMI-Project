QT       += core gui sql serialport

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

msvc: QMAKE_CXXFLAGS += /utf-8

# ---------------- OpenCV ----------------
# MSYS2 UCRT64:  pacman -S mingw-w64-ucrt-x86_64-opencv
win32 {
    OPENCV_ROOT = C:/msys64/ucrt64
    INCLUDEPATH += $$OPENCV_ROOT/include/opencv4
    LIBS += -L$$OPENCV_ROOT/lib -lopencv_core -lopencv_imgproc -lopencv_videoio \
            -lopencv_objdetect -lopencv_face -lopencv_imgcodecs
}
unix {
    CONFIG    += link_pkgconfig
    PKGCONFIG += opencv4
}
# ----------------------------------------

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    capturethread.cpp \
    seriallink.cpp \
    driverlistdialog.cpp \
    faceengine.cpp \
    recognitionthread.cpp \
    databasemanager.cpp \
    displaythread.cpp \
    main.cpp \
    mainwindow.cpp

HEADERS += \
    capturethread.h \
    seriallink.h \
    driverlistdialog.h \
    faceengine.h \
    recognitionthread.h \
    databasemanager.h \
    displaythread.h \
    framesource.h \
    mainwindow.h

FORMS += \
    mainwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target