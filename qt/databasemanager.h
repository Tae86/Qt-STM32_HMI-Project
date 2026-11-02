#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QDateTime>
#include <QHash>
#include <QPair>
#include <QSqlDatabase>
#include <QVector>

class QSqlQueryModel;
class QObject;

// 운전자 조회 화면에 뿌릴 한 줄
struct DriverRow
{
    int id = 0;
    QString name;
    QString createdAt;
    int sampleCount = 0;
    QString lastAuth;
};

class DatabaseManager
{
public:
    DatabaseManager();
    ~DatabaseManager();
    bool openDatabase(QString *errorMessage = nullptr);
    bool initializeSchema(QString *errorMessage = nullptr);
    bool insertSystemEvent(const QString &eventType, QString *errorMessage = nullptr);
    bool insertVehicleLog(const QString &direction, int speed, int distanceCm,
                          QString *errorMessage = nullptr);
    bool insertSensorLog(double temperature, double humidity, const QString &fanState,
                         QString *errorMessage = nullptr);
    QSqlQueryModel *queryLogs(int logType, const QDateTime &start, const QDateTime &end,
                              QObject *parent, QString *errorMessage = nullptr);
    QString databasePath() const;

    // ---- 운전자 / 얼굴 ----
    int addDriver(const QString &name, QString *errorMessage = nullptr);   // 있으면 기존 id
    QString driverName(int driverId) const;
    QHash<int, QString> driverNames() const;
    QVector<DriverRow> listDrivers() const;
    bool deleteDriver(int driverId, QString *errorMessage = nullptr);

    bool insertFaceSample(int driverId, const QString &path, QString *errorMessage = nullptr);
    // (driver_id, 이미지 경로) 목록. 이미지 로딩은 FaceEngine 이 담당한다.
    QVector<QPair<int, QString>> faceSamplePaths() const;
    int faceSampleCount(int driverId) const;

    bool insertAuthLog(int driverId, double confidence, QString *errorMessage = nullptr);

    QString faceDirectory() const;   // DB 파일 옆의 faces 폴더

private:
    QString resolveDatabasePath() const;
    bool migrateSchema(QString *errorMessage);   // 기존 DB 에 새 컬럼 추가
    QSqlDatabase m_database;
};

#endif // DATABASEMANAGER_H