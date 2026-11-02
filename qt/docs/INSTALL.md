# 설치 및 빌드 가이드

Windows + MSYS2 환경 기준. Linux 빌드는 6장 참고.

## 요구사항

| 항목 | 버전 / 비고 |
|---|---|
| OS | Windows 10 이상 |
| 컴파일러 | MinGW-w64 UCRT64 (MSYS2) |
| Qt | 5.15.x — Widgets, Sql, SerialPort |
| OpenCV | 4.x + contrib — core, imgproc, videoio, objdetect, **face**, imgcodecs |
| 빌드 | qmake + mingw32-make |
| IDE | Qt Creator |
| 하드웨어 | 카메라 2대 (내장 = 운전자용, USB = 후방용), STM32 보드 |

---

## 1. MSYS2 설치와 패키지

1. https://www.msys2.org 에서 설치 프로그램을 받아 기본 경로(`C:\msys64`)에 설치한다.
2. 시작 메뉴에서 **MSYS2 UCRT64** 터미널을 연다. (MSYS 나 MINGW64 가 아니라 **UCRT64**)
3. 패키지를 갱신하고 설치한다. 갱신 중 터미널이 닫히면 다시 열고 한 번 더 실행한다.

```bash
pacman -Syu

pacman -S --needed \
  mingw-w64-ucrt-x86_64-toolchain \
  mingw-w64-ucrt-x86_64-qt5-base \
  mingw-w64-ucrt-x86_64-qt5-serialport \
  mingw-w64-ucrt-x86_64-make \
  mingw-w64-ucrt-x86_64-opencv \
  mingw-w64-ucrt-x86_64-qt-creator \
  mingw-w64-ucrt-x86_64-gdb
```

> MSYS2 터미널은 `Ctrl+V` 로 붙여넣기가 안 된다. **마우스 우클릭** 또는 `Shift+Insert` 를 쓴다.

### 설치 확인

```bash
g++ --version
qmake --version
ls /ucrt64/lib/libopencv_face*                              # 얼굴 인식(LBPH) 모듈
ls /ucrt64/share/opencv4/haarcascades/haarcascade_frontalface_default.xml
ls /ucrt64/share/qt5/plugins/sqldrivers/qsqlite.dll         # SQLite 드라이버
```

모두 결과가 나오면 준비 완료다. `libopencv_face` 가 없으면 contrib 모듈이 빠진 빌드라
얼굴 인식을 쓸 수 없고, `qsqlite.dll` 이 없으면 DB 연결이 실패한다.

라이브러리 이름에 `libopencv_core-4xx.dll.a` 처럼 버전 숫자가 붙어 있으면
`QLabel.pro` 의 `-lopencv_core` 도 그 이름에 맞춰 고친다.

### Qt Creator 실행

**MSYS2 UCRT64 터미널에서 실행한다.** 탐색기에서 exe 를 직접 열면 `PATH` 에
`C:\msys64\ucrt64\bin` 이 없어 컴파일러나 DLL 을 못 찾는다.

```bash
qtcreator &
```

첫 실행은 20~30초 걸릴 수 있다. 실행되면 **도구 → 옵션 → Kits** 에서 킷에 경고 아이콘이
없는지 확인한다. Compilers 에 `ucrt64/bin/g++.exe`, Qt Versions 에 `ucrt64/bin/qmake.exe`,
Debuggers 에 `ucrt64/bin/gdb.exe` 가 잡혀 있어야 한다. 자동 인식이 안 되면 각 탭에서
그 경로들을 직접 추가한 뒤 Kits 탭에서 조합한다.

바탕화면 바로가기를 만들려면 대상에 아래를 지정한다.

```
C:\msys64\msys2_shell.cmd -ucrt64 -defterm -no-start -where . -c "qtcreator"
```

---

## 2. 프로젝트 배치

프로젝트 폴더는 **경로에 한글이 없는 곳**에 둔다. 예: `D:\HMI-qt`

> MSYS2 의 make / moc / uic 는 비ASCII 경로에서 파일을 못 찾는 문제가 잦다.

소스 파일은 20개이며, `.pro` 는 **하나만** 있어야 한다.
여러 개가 섞여 있으면 Qt Creator 가 어느 것을 열었는지 헷갈려 엉뚱한 빌드가 나간다.

```
QLabel.pro
main.cpp
mainwindow.cpp / .h / .ui
seriallink.cpp / .h             STM32 UART 통신
databasemanager.cpp / .h        DB 연결, 스키마, 저장, 조회
capturethread.cpp / .h          카메라에서 최신 프레임 유지
displaythread.cpp / .h          프레임 -> QImage -> UI
framesource.h                   프레임 공급자 인터페이스
faceengine.cpp / .h             Haar 검출 + LBPH 인식
recognitionthread.cpp / .h      인식/등록 스레드
driverlistdialog.cpp / .h       운전자 조회·삭제
```

다른 PC 에서 받은 `build` 폴더나 `.pro.user` 가 섞여 있으면 지운다.

---

## 3. 빌드

1. Qt Creator → 파일 열기 → **`QLabel.pro`**
2. 킷 선택 → Configure Project
3. 빌드 → **Run qmake** → **Rebuild**

`.pro` 나 `.ui` 를 수정한 뒤에는 반드시 **Run qmake** 를 먼저 실행한다.
그냥 빌드하면 기존 Makefile 이 쓰여 변경이 반영되지 않는다.

커맨드라인으로 빌드하려면:

```bash
cd /d/HMI-qt
qmake QLabel.pro && mingw32-make -j4
```

---

## 4. 실행 전 확인

### 4-1. 카메라 인덱스

인덱스는 PC 마다 다르다. `mainwindow.h` 상단에서 지정한다.

```cpp
static constexpr int kUserCamSrc = 0;   // 내장 카메라 -> 운전자 인식
static constexpr int kRearCamSrc = 1;   // USB 카메라  -> 후방 카메라
```

화면이 서로 바뀌어 나오면 두 값을 맞바꾼다. USB 포트를 옮겨 꽂으면 번호가 달라질 수 있다.

카메라가 안 열리면 해당 라벨에 "카메라 N 열기 실패" 가 뜨고 `system_event` 에
`CAMERA_ERROR` 가 남는다. `capturethread.cpp` 의 백엔드 순서를 바꿔 시도해 본다.

```cpp
#if defined(Q_OS_WIN)
    const std::vector<int> backends{cv::CAP_MSMF, cv::CAP_DSHOW, cv::CAP_ANY};
#endif
```

### 4-2. 시리얼 포트

포트 이름을 지정하지 않으면 자동으로 탐색한다.

1. USB Vendor ID 가 `0x0483`(STMicroelectronics)인 포트를 우선 선택
2. 없으면 첫 번째 시리얼 포트를 시도

연결에 성공하면 상태바에 `STM32 연결됨: COM3` 이 표시되고 `system_event` 에
`STM32_CONNECTED` 가 기록된다. 특정 포트를 지정하려면 `mainwindow.cpp` 에서
`m_serial->open("COM3")` 처럼 이름을 넘긴다.

수신 확인은 애플리케이션 출력 창에서 할 수 있다. `$D` 를 제외한 모든 수신 줄이
`[RX] "$B"` 형태로 출력된다.

### 4-3. haarcascade 파일

`faceengine.cpp` 가 아래 순서로 찾으므로, MSYS2 기본 경로에 OpenCV 를 설치했다면
따로 복사할 필요가 없다.

1. 실행 파일과 같은 폴더
2. `C:/msys64/ucrt64/share/opencv4/haarcascades/`
3. 리눅스 기본 경로

못 찾으면 시작할 때 "얼굴 인식 초기화 실패" 경고가 뜨고 **등록·인식 버튼이 비활성화된다.**
그때는 `haarcascade_frontalface_default.xml` 을 실행 파일 옆에 복사한다.
다른 PC 에 배포할 때는 MSYS2 가 없으므로 실행 파일과 함께 넣어야 한다.

### 4-4. DB 파일

`vehicle_hmi_dummy.db` 가 없으면 실행 파일 폴더에 자동 생성되고 스키마도 만들어진다.
기존 파일이 있으면 그것을 이어 쓴다. 상태바에 `DB 연결됨: 경로` 로 실제 사용 중인
파일이 표시된다.

깨끗하게 시작하려면 그 파일을 지우고 실행한다. 얼굴 샘플 이미지는 DB 파일 옆
`faces/<driver_id>/` 에, 학습된 모델은 `lbph_model.yml` 로 저장된다.

### 4-5. DLL 경로

실행하자마자 창 없이 종료되면 DLL 을 못 찾는 것이다.
프로젝트 → **실행** → 환경 → `PATH` 에 `C:\msys64\ucrt64\bin` 을 추가한다.

---

## 5. 사용 방법

### 운전자 등록 (최초 1회)

STM32 버튼을 눌러야 등록·인식 버튼이 활성화된다.

1. 운전자 인식 탭에서 이름을 입력하고 **얼굴 등록**
2. 카메라를 보면 얼굴에 초록 사각형이 생기고 `홍길동 등록 중 7/20` 처럼 진행률이 오른다
3. 20장이 모이면 자동으로 재학습하고 완료 알림이 뜬다

수집 중에는 고개를 조금씩 좌우로 돌리고 표정도 바꾼다. 각도와 조명이 다양할수록
인식률이 올라간다. 진행률이 0 에서 멈추면 얼굴이 검출되지 않는 것이다 —
조명을 밝게 하고, 얼굴이 화면에서 충분히 크게(폭 80px 이상) 잡히도록 앉는다.

### 인증

**인식 시작** 을 누르면 실시간 판별이 시작된다. 아래에 `홍길동 (score 42, 3/5)` 이 표시되는데
score 는 **낮을수록 유사**하고, `3/5` 는 연속 일치 횟수다. **5회 연속 같은 사람이면 인증 확정** 되어
`auth_log` 에 기록되고 계기판으로 넘어간다.

인증 후에는 운전자 인식 탭이 잠긴다. 재등록이나 재인증이 필요하면 프로그램을 다시 실행한다.

### 운전자 조회

**운전자 조회** 버튼으로 등록된 사람의 ID, 이름, 샘플 수, 등록일, 최근 인증 시각을 본다.
선택해서 삭제하면 얼굴 이미지 파일까지 지워지고 모델이 재학습된다.

### 로그 조회

DB 조회 탭에서 기간과 데이터 종류를 고르고 **조회** 를 누른다.
종류는 시스템 이벤트 / 차량 로그 / 환경 로그 / 인증 이력 네 가지다.

### 인식 임계값 조정

LBPH 에서 가장 중요한 설정이다. `faceengine.h` 의 값을 조정한다.

```cpp
static constexpr double kThreshold = 70.0;   // 이보다 크면 미등록으로 판단
```

본인인데 계속 "미등록 운전자" 로 나오면서 score 가 80~90 이면 값을 올리고,
등록하지 않은 사람이 통과하면 낮춘다. 등록자 전원과 미등록자의 score 를 실제로
측정해 두 분포 사이에 값을 정하는 것이 좋다.

---

## 6. Linux 에서 빌드 (참고)

```bash
sudo apt install qtbase5-dev qt5-qmake libqt5serialport5-dev \
                 libopencv-dev libopencv-contrib-dev
qmake QLabel.pro && make -j4
./QLabel
```

`QLabel.pro` 의 `unix` 블록이 `pkg-config` 로 OpenCV 를 찾으므로 경로 설정이 필요 없다.
카메라 장치는 `ls /dev/video*`, 시리얼 포트는 `ls /dev/ttyACM*` 로 확인한다.

---

## 7. 자주 겪는 오류

| 증상 | 원인과 해결 |
|---|---|
| `Cannot find file: ...\xxx.pro` | Qt Creator 가 기억한 경로와 파일명이 다름. `.pro.user` 삭제 후 다시 연다 |
| `Makefile.Debug: ..._ko_KR.qm Error 2` | `.pro` 의 `TRANSLATIONS` / `CONFIG += lrelease embed_translations` 삭제 |
| `gui_ui.h: No such file` | include 는 `ui_` 접두사. 폼이 `mainwindow.ui` 면 `ui_mainwindow.h` |
| `undefined reference to FaceEngine::...` | 해당 `.cpp` 가 SOURCES 에 없거나 파일이 폴더에 없음. `build` 삭제 후 Rebuild |
| `undefined reference to vtable for ...` | 헤더가 HEADERS 에 없어 moc 이 안 돌았다 |
| `undefined reference to cv::face::...` | `-lopencv_face` 누락 또는 contrib 없는 OpenCV |
| `QSerialPort: No such file` | `.pro` 에 `QT += serialport` 확인, `qt5-serialport` 패키지 설치 |
| `Driver not loaded` | `.pro` 에 `QT += sql` 확인, `qsqlite.dll` 존재 확인 |
| DB 탭이 회색 | DB 연결 실패로 비활성화됨. 시작 시 뜬 오류 메시지 확인 |
| 등록 버튼을 눌러도 무반응 | STM32 버튼(`$B`)을 아직 안 눌렀거나, cascade 파일을 못 찾아 초기화 실패 (4-3 참고) |
| 등록은 되는데 샘플 수가 0 | `main.cpp` 의 `qRegisterMetaType<cv::Mat>("cv::Mat")` 누락. 없으면 얼굴 전달 시그널이 조용히 버려진다 |
| 카메라를 껐다 켜면 안 켜짐 | `CaptureThread::run()` 시작 지점의 `m_running = true` 초기화 확인 |
| 영상이 조금씩 커짐 | `label->size()` 기준 스케일이 원인. 고정 크기(`m_videoSize`)로 스케일한다 |
| 카메라를 꺼도 마지막 화면이 남음 | 표시 차단 → 카메라 정지 → 화면 정리 순서 확인 |
| 탭과 화면이 어긋남 | `navList` 항목 수와 `stackMain` 페이지 수 불일치. 짝을 맞춰 삭제한다 |
| 초록 박스 위 글자가 `???` | `cv::putText` 는 한글을 못 그린다. 이름은 `lblRecogResult` 에 표시한다 |
| DSHOW 경고 후 카메라 실패 | 백엔드 순서 변경 (4-1 참고) |
| MSYS2 에 붙여넣기 안 됨 | 마우스 우클릭 또는 `Shift+Insert` |

`.pro` 를 고쳤는데 반영되지 않으면 항상 **Run qmake → Rebuild** 를 먼저 시도한다.