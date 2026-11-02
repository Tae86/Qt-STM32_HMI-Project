-- Qt HMI SQLite 스키마 (qt/databasemanager.cpp 의 initializeSchema() 와 동일)
-- 앱 실행 시 자동 생성되므로 참고/직접 조회용입니다.
PRAGMA foreign_keys = ON;

-- 시스템 / 주행 / 센서 로그
CREATE TABLE IF NOT EXISTS system_event (id INTEGER PRIMARY KEY AUTOINCREMENT, timestamp TEXT NOT NULL DEFAULT (datetime('now','localtime')), event_type TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS vehicle_log (id INTEGER PRIMARY KEY AUTOINCREMENT, timestamp TEXT NOT NULL DEFAULT (datetime('now','localtime')), direction TEXT NOT NULL CHECK(direction IN ('FWD','BACK','LEFT','RIGHT','STOP')), speed INTEGER NOT NULL CHECK(speed BETWEEN 0 AND 100), distance_cm INTEGER);
CREATE TABLE IF NOT EXISTS sensor_log (id INTEGER PRIMARY KEY AUTOINCREMENT, timestamp TEXT NOT NULL DEFAULT (datetime('now','localtime')), temperature REAL, humidity REAL, fan_state TEXT NOT NULL CHECK(fan_state IN ('ON','OFF')));
CREATE INDEX IF NOT EXISTS idx_system_event_time ON system_event(timestamp);
CREATE INDEX IF NOT EXISTS idx_vehicle_log_time ON vehicle_log(timestamp);
CREATE INDEX IF NOT EXISTS idx_sensor_log_time ON sensor_log(timestamp);

-- 운전자 얼굴 인식
CREATE TABLE IF NOT EXISTS driver (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL UNIQUE, created_at TEXT NOT NULL DEFAULT (datetime('now','localtime')));
CREATE TABLE IF NOT EXISTS face_sample (id INTEGER PRIMARY KEY AUTOINCREMENT, driver_id INTEGER NOT NULL REFERENCES driver(id) ON DELETE CASCADE, path TEXT NOT NULL, created_at TEXT NOT NULL DEFAULT (datetime('now','localtime')));
CREATE TABLE IF NOT EXISTS auth_log (id INTEGER PRIMARY KEY AUTOINCREMENT, timestamp TEXT NOT NULL DEFAULT (datetime('now','localtime')), driver_id INTEGER REFERENCES driver(id) ON DELETE SET NULL, confidence REAL);
CREATE INDEX IF NOT EXISTS idx_face_sample_driver ON face_sample(driver_id);
CREATE INDEX IF NOT EXISTS idx_auth_log_time ON auth_log(timestamp);
