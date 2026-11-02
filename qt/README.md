# Qt HMI 소스 (PC 측)

팀원 [Kevin-jwd](https://github.com/Kevin-jwd)이 작성한 Qt HMI 소스를 팀 프로젝트 전체 보관용으로 함께 수록했습니다.
상세 문서는 [`docs/`](docs/)에 함께 두었습니다: [설치](docs/INSTALL.md) · [DB](../db/) · [얼굴 인식](docs/FACE_RECOGNITION_GUIDE.md).
최신 버전은 원본 저장소 **[Kevin-jwd/HMI-qt](https://github.com/Kevin-jwd/HMI-qt)** 를 기준으로 합니다.

- 환경: C++17, Qt 5.15 (Widgets, SerialPort, Sql), OpenCV 4.x + contrib
- 빌드: `qmake QLabel.pro && make` (OpenCV 경로는 `QLabel.pro`에서 본인 환경에 맞게 수정)
- 구조 요약은 상위 [README](../README.md#-qt-hmi-pc-구현) 참고
