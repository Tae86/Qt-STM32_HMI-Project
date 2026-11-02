#include "databasemanager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlQueryModel>
#include <QVariant>

namespace { const char kConnectionName[] = "vehicle_hmi_connection"; const char kFormat[] = "yyyy-MM-dd HH:mm:ss"; }

DatabaseManager::DatabaseManager() : m_database(QSqlDatabase::addDatabase("QSQLITE", kConnectionName)) {}

DatabaseManager::~DatabaseManager()
{
    if (m_database.isOpen()) m_database.close();
    m_database = QSqlDatabase();
    QSqlDatabase::removeDatabase(kConnectionName);
}

QString DatabaseManager::resolveDatabasePath() const
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = { QDir(appDir).filePath("vehicle_hmi_dummy.db"),
        QDir(appDir).filePath("../QLabel/vehicle_hmi_dummy.db"),
        QDir::current().filePath("vehicle_hmi_dummy.db"), QDir::current().filePath("QLabel/vehicle_hmi_dummy.db") };
    for (const QString &path : candidates)
        if (QFileInfo::exists(path)) return QFileInfo(path).absoluteFilePath();
    return QDir(appDir).filePath("vehicle_hmi_dummy.db");
}

bool DatabaseManager::openDatabase(QString *errorMessage)
{
    m_database.setDatabaseName(resolveDatabasePath());
    if (m_database.open()) return true;
    if (errorMessage) *errorMessage = m_database.lastError().text();
    return false;
}

bool DatabaseManager::initializeSchema(QString *errorMessage)
{
    const QStringList statements = {
        "CREATE TABLE IF NOT EXISTS system_event (id INTEGER PRIMARY KEY AUTOINCREMENT, timestamp TEXT NOT NULL DEFAULT (datetime('now','localtime')), event_type TEXT NOT NULL)",
        "CREATE TABLE IF NOT EXISTS vehicle_log (id INTEGER PRIMARY KEY AUTOINCREMENT, timestamp TEXT NOT NULL DEFAULT (datetime('now','localtime')), direction TEXT NOT NULL CHECK(direction IN ('FWD','BACK','LEFT','RIGHT','STOP')), speed INTEGER NOT NULL CHECK(speed BETWEEN 0 AND 100), distance_cm INTEGER)",
        "CREATE TABLE IF NOT EXISTS sensor_log (id INTEGER PRIMARY KEY AUTOINCREMENT, timestamp TEXT NOT NULL DEFAULT (datetime('now','localtime')), temperature REAL, humidity REAL, fan_state TEXT NOT NULL CHECK(fan_state IN ('ON','OFF')))",
        "CREATE INDEX IF NOT EXISTS idx_system_event_time ON system_event(timestamp)", "CREATE INDEX IF NOT EXISTS idx_vehicle_log_time ON vehicle_log(timestamp)",
        "CREATE INDEX IF NOT EXISTS idx_sensor_log_time ON sensor_log(timestamp)",
        // ---- 운전자 얼굴 인식 ----
        "CREATE TABLE IF NOT EXISTS driver (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL UNIQUE, created_at TEXT NOT NULL DEFAULT (datetime('now','localtime')))",
        "CREATE TABLE IF NOT EXISTS face_sample (id INTEGER PRIMARY KEY AUTOINCREMENT, driver_id INTEGER NOT NULL REFERENCES driver(id) ON DELETE CASCADE, path TEXT NOT NULL, created_at TEXT NOT NULL DEFAULT (datetime('now','localtime')))",
        "CREATE TABLE IF NOT EXISTS auth_log (id INTEGER PRIMARY KEY AUTOINCREMENT, timestamp TEXT NOT NULL DEFAULT (datetime('now','localtime')), driver_id INTEGER REFERENCES driver(id) ON DELETE SET NULL, confidence REAL)",
        "CREATE INDEX IF NOT EXISTS idx_face_sample_driver ON face_sample(driver_id)",
        "CREATE INDEX IF NOT EXISTS idx_auth_log_time ON auth_log(timestamp)" };
    for (const QString &statement : statements) {
        QSqlQuery query(m_database);
        if (!query.exec(statement)) {
            if (errorMessage)
                *errorMessage = query.lastError().text();
            return false;
        }
    }
    return migrateSchema(errorMessage);
}

/*
 * CREATE TABLE IF NOT EXISTS 는 이미 있는 테이블에 컬럼을 추가하지 않는다.
 * 예전 버전으로 만든 DB 파일을 그대로 쓰는 경우를 위해 부족한 컬럼만 채운다.
 */
bool DatabaseManager::migrateSchema(QString *errorMessage)
{
    QSqlQuery info(m_database);
    if (!info.exec(QStringLiteral("PRAGMA table_info(vehicle_log)"))) {
        if (errorMessage)
            *errorMessage = info.lastError().text();
        return false;
    }

    bool hasDistance = false;
    while (info.next()) {
        if (info.value(1).toString() == QLatin1String("distance_cm")) {
            hasDistance = true;
            break;
        }
    }
    if (hasDistance)
        return true;

    QSqlQuery alter(m_database);
    if (!alter.exec(QStringLiteral("ALTER TABLE vehicle_log ADD COLUMN distance_cm INTEGER"))) {
        if (errorMessage)
            *errorMessage = alter.lastError().text();
        return false;
    }
    return true;
}

namespace {
bool executeInsert(QSqlDatabase &database, const QString &sql, const QVariantList &values,
                   QString *errorMessage)
{
    QSqlQuery query(database);
    query.prepare(sql);
    for (const QVariant &value : values)
        query.addBindValue(value);
    if (query.exec())
        return true;
    if (errorMessage)
        *errorMessage = query.lastError().text();
    return false;
}
}

bool DatabaseManager::insertSystemEvent(const QString &eventType, QString *errorMessage)
{
    return executeInsert(m_database,
        QStringLiteral("INSERT INTO system_event (event_type) VALUES (?)"),
        {eventType}, errorMessage);
}

bool DatabaseManager::insertVehicleLog(const QString &direction, int speed, int distanceCm,
                                      QString *errorMessage)
{
    QSqlQuery latest(m_database);
    if (!latest.exec(QStringLiteral(
            "SELECT direction, speed FROM vehicle_log ORDER BY id DESC LIMIT 1"))) {
        if (errorMessage)
            *errorMessage = latest.lastError().text();
        return false;
    }
    if (latest.next() && latest.value(0).toString() == direction
            && latest.value(1).toInt() == speed) {
        return true;
    }

    return executeInsert(m_database,
        QStringLiteral("INSERT INTO vehicle_log (direction, speed, distance_cm) VALUES (?, ?, ?)"),
        {direction, speed, distanceCm}, errorMessage);
}

bool DatabaseManager::insertSensorLog(double temperature, double humidity,
                                      const QString &fanState, QString *errorMessage)
{
    return executeInsert(m_database,
        QStringLiteral("INSERT INTO sensor_log (temperature, humidity, fan_state) VALUES (?, ?, ?)"),
        {temperature, humidity, fanState}, errorMessage);
}

QSqlQueryModel *DatabaseManager::queryLogs(int logType, const QDateTime &start, const QDateTime &end,
                                           QObject *parent, QString *errorMessage)
{
    const QStringList sql = {
        "SELECT id AS 'ID', timestamp AS '시간', event_type AS '이벤트' FROM system_event WHERE timestamp BETWEEN ? AND ? ORDER BY timestamp DESC",
        "SELECT id AS 'ID', timestamp AS '시간', direction AS '방향', speed AS '속도', distance_cm AS '후방거리(cm)' FROM vehicle_log WHERE timestamp BETWEEN ? AND ? ORDER BY timestamp DESC",
        "SELECT id AS 'ID', timestamp AS '시간', temperature AS '온도 (°C)', humidity AS '습도 (%)', fan_state AS 'FAN' FROM sensor_log WHERE timestamp BETWEEN ? AND ? ORDER BY timestamp DESC",
        "SELECT a.id AS 'ID', a.timestamp AS '시간', IFNULL(d.name,'(삭제됨)') AS '운전자', ROUND(a.confidence,1) AS 'score' FROM auth_log a LEFT JOIN driver d ON d.id = a.driver_id WHERE a.timestamp BETWEEN ? AND ? ORDER BY a.timestamp DESC" };
    if (logType < 0 || logType >= sql.size()) { if (errorMessage) *errorMessage = QStringLiteral("지원하지 않는 데이터 종류입니다."); return nullptr; }
    QSqlQuery query(m_database); query.prepare(sql.at(logType)); query.addBindValue(start.toString(kFormat)); query.addBindValue(end.toString(kFormat));
    if (!query.exec()) { if (errorMessage) *errorMessage = query.lastError().text(); return nullptr; }
    auto *model = new QSqlQueryModel(parent); model->setQuery(query);
    if (model->lastError().isValid()) { if (errorMessage) *errorMessage = model->lastError().text(); delete model; return nullptr; }
    return model;
}

// ---------------- 운전자 / 얼굴 ----------------

int DatabaseManager::addDriver(const QString &name, QString *errorMessage)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("운전자 이름이 비어 있습니다.");
        return -1;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT id FROM driver WHERE name = ?"));
    query.addBindValue(trimmed);
    if (query.exec() && query.next())
        return query.value(0).toInt();

    query.prepare(QStringLiteral("INSERT INTO driver (name) VALUES (?)"));
    query.addBindValue(trimmed);
    if (!query.exec()) {
        if (errorMessage) *errorMessage = query.lastError().text();
        return -1;
    }
    return query.lastInsertId().toInt();
}

QString DatabaseManager::driverName(int driverId) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT name FROM driver WHERE id = ?"));
    query.addBindValue(driverId);
    if (query.exec() && query.next())
        return query.value(0).toString();
    return QString();
}

QHash<int, QString> DatabaseManager::driverNames() const
{
    QHash<int, QString> names;
    QSqlQuery query(m_database);
    if (query.exec(QStringLiteral("SELECT id, name FROM driver")))
        while (query.next())
            names.insert(query.value(0).toInt(), query.value(1).toString());
    return names;
}

QVector<DriverRow> DatabaseManager::listDrivers() const
{
    QVector<DriverRow> rows;
    QSqlQuery query(m_database);
    const bool ok = query.exec(QStringLiteral(
        "SELECT d.id, d.name, d.created_at, COUNT(DISTINCT s.id), MAX(a.timestamp) "
        "FROM driver d "
        "LEFT JOIN face_sample s ON s.driver_id = d.id "
        "LEFT JOIN auth_log    a ON a.driver_id = d.id "
        "GROUP BY d.id ORDER BY d.id"));
    if (!ok)
        return rows;

    while (query.next()) {
        DriverRow row;
        row.id = query.value(0).toInt();
        row.name = query.value(1).toString();
        row.createdAt = query.value(2).toString();
        row.sampleCount = query.value(3).toInt();
        row.lastAuth = query.value(4).toString();
        rows.append(row);
    }
    return rows;
}

bool DatabaseManager::deleteDriver(int driverId, QString *errorMessage)
{
    // 저장된 샘플 이미지 파일도 같이 지운다
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT path FROM face_sample WHERE driver_id = ?"));
    query.addBindValue(driverId);
    if (query.exec())
        while (query.next())
            QFile::remove(query.value(0).toString());

    query.prepare(QStringLiteral("DELETE FROM face_sample WHERE driver_id = ?"));
    query.addBindValue(driverId);
    query.exec();

    query.prepare(QStringLiteral("DELETE FROM driver WHERE id = ?"));
    query.addBindValue(driverId);
    if (!query.exec()) {
        if (errorMessage) *errorMessage = query.lastError().text();
        return false;
    }
    return true;
}

bool DatabaseManager::insertFaceSample(int driverId, const QString &path, QString *errorMessage)
{
    return executeInsert(m_database,
        QStringLiteral("INSERT INTO face_sample (driver_id, path) VALUES (?, ?)"),
        {driverId, path}, errorMessage);
}

QVector<QPair<int, QString>> DatabaseManager::faceSamplePaths() const
{
    QVector<QPair<int, QString>> samples;
    QSqlQuery query(m_database);
    if (query.exec(QStringLiteral("SELECT driver_id, path FROM face_sample")))
        while (query.next())
            samples.append({query.value(0).toInt(), query.value(1).toString()});
    return samples;
}

int DatabaseManager::faceSampleCount(int driverId) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM face_sample WHERE driver_id = ?"));
    query.addBindValue(driverId);
    if (query.exec() && query.next())
        return query.value(0).toInt();
    return 0;
}

bool DatabaseManager::insertAuthLog(int driverId, double confidence, QString *errorMessage)
{
    return executeInsert(m_database,
        QStringLiteral("INSERT INTO auth_log (driver_id, confidence) VALUES (?, ?)"),
        {driverId, confidence}, errorMessage);
}

QString DatabaseManager::faceDirectory() const
{
    const QString dir = QFileInfo(m_database.databaseName()).absolutePath() + "/faces";
    QDir().mkpath(dir);
    return dir;
}

QString DatabaseManager::databasePath() const { return m_database.databaseName(); }