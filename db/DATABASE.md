# DB 스키마

SQLite 기반. 시스템 이벤트, 차량·환경 로그, 운전자 정보와 인증 이력을 저장한다.

| 항목 | 내용 |
|---|---|
| DBMS | SQLite (Qt `QSQLITE` 드라이버) |
| 파일명 | `vehicle_hmi_dummy.db` |
| 저장 위치 | 실행 파일 디렉터리 (없으면 자동 생성) |
| 시간 형식 | PC Local Time, `yyyy-MM-dd HH:mm:ss` (TEXT) |
| 테이블 수 | 6개 |

SQL 은 `DatabaseManager` 클래스에만 존재한다. 테이블과 인덱스는 실행 시
`initializeSchema()` 가 `CREATE TABLE IF NOT EXISTS` 로 생성한다.

---

## 1. 테이블 개요

| 테이블명 | 주요 내용 | 저장 시점 | 비고 |
|---|---|---|---|
| `system_event` | 시스템·카메라·통신·얼굴 관련 이벤트 | 이벤트 발생 시 | 독립 테이블 |
| `vehicle_log` | 주행 방향, 속도, 후방 초음파 거리 | 상태 변경 시 | 독립 테이블 |
| `sensor_log` | 온·습도 측정값 및 FAN 작동 상태 | 최대 1초 간격 | 독립 테이블 |
| `driver` | 등록된 운전자 기본 정보 | 운전자 등록 시 | 기준 정보 |
| `face_sample` | 운전자별 얼굴 학습 샘플 이미지 경로 | 등록 시 (1인당 20장) | `driver` 삭제 시 연쇄 삭제 (`CASCADE`) |
| `auth_log` | 얼굴 인증 이력 및 판정값 | 인증 확정 시 | `driver` 삭제 시 `NULL` 처리 (`SET NULL`) |

### 관계

```
driver ──┬── face_sample   (1:N, ON DELETE CASCADE)
         └── auth_log      (1:N, ON DELETE SET NULL)

system_event / vehicle_log / sensor_log   독립 로그
```

얼굴 이미지 자체는 DB 에 저장하지 않는다. 파일로 저장하고 경로만 기록한다.

---

## 2. 테이블별 상세 명세

### system_event (시스템 이벤트 로그)

> 시스템 동작, 카메라 제어, 통신 연결, 얼굴 인식 처리 등 이벤트 발생 이력을 기록

| 컬럼명 | 데이터 타입 | 제약 조건 / 기본값 | 설명 |
|---|---|---|---|
| `id` | INTEGER | PK, AUTOINCREMENT, NOT NULL | 이벤트 식별자 |
| `timestamp` | TEXT | NOT NULL, `datetime('now','localtime')` | 이벤트 발생 시각 |
| `event_type` | TEXT | NOT NULL | 이벤트 종류 |

* **인덱스**: `idx_system_event_time` (`timestamp`)

<br>

### vehicle_log (차량 상태 로그)

> 차량의 주행 상태(방향, 속도)와 후방 초음파 거리 정보를 기록

| 컬럼명 | 데이터 타입 | 제약 조건 / 기본값 | 설명 |
|---|---|---|---|
| `id` | INTEGER | PK, AUTOINCREMENT, NOT NULL | 차량 로그 식별자 |
| `timestamp` | TEXT | NOT NULL, `datetime('now','localtime')` | 저장 시각 |
| `direction` | TEXT (Enum) | NOT NULL, `CHECK` | 주행 방향 (`FWD`, `BACK`, `LEFT`, `RIGHT`, `STOP`) |
| `speed` | INTEGER | NOT NULL, `CHECK(speed BETWEEN 0 AND 100)` | 주행 속도 (0 ~ 100) |
| `distance_cm` | INTEGER | NULL | 후방 초음파 거리 (cm) |

* **인덱스**: `idx_vehicle_log_time` (`timestamp`)
* **저장 조건**: 방향이 변경되거나 속도가 직전 저장값 대비 5 이상 변화한 경우에만 저장

<br>

### sensor_log (환경 센서 로그)

> 온·습도 측정값과 FAN 동작 상태를 주기적으로 기록

| 컬럼명 | 데이터 타입 | 제약 조건 / 기본값 | 설명 |
|---|---|---|---|
| `id` | INTEGER | PK, AUTOINCREMENT, NOT NULL | 센서 로그 식별자 |
| `timestamp` | TEXT | NOT NULL, `datetime('now','localtime')` | 저장 시각 |
| `temperature` | REAL | NULL | 온도 (°C) |
| `humidity` | REAL | NULL | 습도 (%) |
| `fan_state` | TEXT (Enum) | NOT NULL, `CHECK` | FAN 작동 상태 (`ON`, `OFF`) |

* **인덱스**: `idx_sensor_log_time` (`timestamp`)
* **저장 조건**: STM32 가 500ms 주기로 전송하지만, DB 에는 최대 1초에 한 번만 기록

<br>

### driver (운전자 정보)

> 차량 사용자로 등록된 운전자의 식별 정보를 관리

| 컬럼명 | 데이터 타입 | 제약 조건 / 기본값 | 설명 |
|---|---|---|---|
| `id` | INTEGER | PK, AUTOINCREMENT, NOT NULL | 운전자 식별자 |
| `name` | TEXT | NOT NULL, UNIQUE | 운전자 이름 (중복 불가) |
| `created_at` | TEXT | NOT NULL, `datetime('now','localtime')` | 등록 시각 |

`id` 는 얼굴 인식 모델의 라벨로도 사용된다. 인식 결과의 라벨을 그대로
`driver` 조회에 쓸 수 있어 별도 매핑이 필요 없다.

<br>

### face_sample (얼굴 인식 샘플)

> 얼굴 인증 모델 학습에 사용되는 이미지 파일의 저장 경로를 관리

| 컬럼명 | 데이터 타입 | 제약 조건 / 기본값 | 설명 |
|---|---|---|---|
| `id` | INTEGER | PK, AUTOINCREMENT, NOT NULL | 얼굴 샘플 식별자 |
| `driver_id` | INTEGER | NOT NULL, FK (`driver.id`) | 소속 운전자 ID |
| `path` | TEXT | NOT NULL | 얼굴 이미지 파일 경로 |
| `created_at` | TEXT | NOT NULL, `datetime('now','localtime')` | 저장 시각 |

* **인덱스**: `idx_face_sample_driver` (`driver_id`)
* **외래키 제약**: `ON DELETE CASCADE` — `driver` 삭제 시 관련 샘플 기록도 함께 삭제
* **파일 위치**: `faces/<driver_id>/*.png` (200×200 흑백)

<br>

### auth_log (얼굴 인증 이력)

> 인증이 확정된 시각, 대상 운전자, 판정값을 기록

| 컬럼명 | 데이터 타입 | 제약 조건 / 기본값 | 설명 |
|---|---|---|---|
| `id` | INTEGER | PK, AUTOINCREMENT, NOT NULL | 인증 로그 식별자 |
| `timestamp` | TEXT | NOT NULL, `datetime('now','localtime')` | 인증 시각 |
| `driver_id` | INTEGER | NULL, FK (`driver.id`) | 인증된 운전자 ID |
| `confidence` | REAL | NULL | 얼굴 인증 판정값 (거리, 낮을수록 유사) |

* **인덱스**: `idx_auth_log_time` (`timestamp`)
* **외래키 제약**: `ON DELETE SET NULL` — 운전자가 삭제되어도 인증 이력은 보존

---

## 3. 이벤트 코드

`system_event.event_type` 에 저장되는 값이다.

| 코드 | 시점 |
|---|---|
| `SYSTEM_START_REQUEST` | 앱 시작 |
| `SYSTEM_STOP` | 정상 종료 |
| `STM32_CONNECTED` | 시리얼 포트 연결 성공 |
| `SERIAL_ERROR` | 시리얼 연결 실패 또는 해제 |
| `DOOR_BUTTON_PRESSED` | STM32 차량 버튼(`$B`) 수신 |
| `USER_CAMERA_ON` / `REAR_CAMERA_ON` | 카메라 연결 성공 |
| `REAR_CAMERA_OFF` | 후방 카메라 토글 끔 |
| `CAMERA_ERROR` | 카메라 열기 실패 |
| `FACE_DETECTION_START` | 인식 시작 |
| `FACE_DETECTED` | 인증 확정 |
| `DRIVER_REGISTERED` | 얼굴 등록 완료 |

---

## 4. 조회

DB 조회 탭에서 **기간 + 데이터 종류** 를 조건으로 최신순 조회한다.

| 종류 | 조회 대상 | 결과 항목 |
|---|---|---|
| 시스템 이벤트 | `system_event` | ID / 시간 / 이벤트 |
| 차량 로그 | `vehicle_log` | ID / 시간 / 방향 / 속도 / 후방거리 |
| 환경 로그 | `sensor_log` | ID / 시간 / 온도 / 습도 / FAN |
| 인증 이력 | `auth_log` + `driver` | ID / 시간 / 운전자 / score |

모든 조회는 아래 조건을 사용한다.

```sql
WHERE timestamp BETWEEN ? AND ?
ORDER BY timestamp DESC
```

`?` 는 `QSqlQuery::prepare()` 와 `addBindValue()` 로 바인딩한다.
문자열을 직접 이어 붙이지 않는 이유는 값에 따옴표나 SQL 구문이 섞였을 때
쿼리가 깨지는 것을 막기 위해서다.

인증 이력만 두 테이블을 함께 읽는다. 삭제된 운전자의 이력은 이름이
`(삭제됨)` 으로 표시된다.

```sql
SELECT a.id, a.timestamp, IFNULL(d.name, '(삭제됨)'), ROUND(a.confidence, 1)
FROM auth_log a
LEFT JOIN driver d ON d.id = a.driver_id
WHERE a.timestamp BETWEEN ? AND ?
ORDER BY a.timestamp DESC;
```

---

## 5. 구현 참고

### 스키마 생성과 마이그레이션

`CREATE TABLE IF NOT EXISTS` 는 **이미 존재하는 테이블을 변경하지 않는다.**
따라서 나중에 추가한 컬럼은 기존 DB 파일에 생기지 않는다.

`migrateSchema()` 가 실행 시 컬럼 존재 여부를 확인하고 없으면 추가한다.

```sql
PRAGMA table_info(vehicle_log)                              -- 컬럼 목록 확인
ALTER TABLE vehicle_log ADD COLUMN distance_cm INTEGER      -- 없으면 추가
```

기존 행의 값은 `NULL` 로 남고, 이후 저장부터 값이 채워진다.

### 스레드 제약

`QSqlDatabase` 커넥션은 **생성한 스레드에서만** 사용할 수 있다.
따라서 얼굴 인식 스레드는 DB 를 직접 호출하지 않고, 저장할 데이터를
시그널로 UI 스레드에 넘기며 실제 기록은 `MainWindow` 가 수행한다.

### 데이터 초기화

`vehicle_hmi_dummy.db` 파일을 삭제하면 다음 실행 시 새로 생성된다.
얼굴 등록까지 지우려면 `faces` 폴더와 `lbph_model.yml` 도 함께 삭제한다.

### 내용 확인

[DB Browser for SQLite](https://sqlitebrowser.org) 로 파일을 열면 테이블 내용을
표로 볼 수 있다. 저장이 안 될 때 UI 문제인지 DB 문제인지 구분하는 데 유용하다.

---

## 6. 향후 검토

- DB 파일명 변경 — `dummy` 는 초기 개발 단계의 명칭
- 로그 보존 기간 정책 — `sensor_log` 기준 1초에 한 행이 쌓인다
- 얼굴 인증을 벡터(임베딩) 방식으로 전환할 경우 `face_sample` 을
  `face_embedding`(벡터 BLOB, 차원, 모델명)으로 대체