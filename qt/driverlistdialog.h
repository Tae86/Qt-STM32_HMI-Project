#ifndef DRIVERLISTDIALOG_H
#define DRIVERLISTDIALOG_H

#include <QDialog>

class DatabaseManager;
class QTableWidget;

// 등록된 운전자 조회 / 삭제
class DriverListDialog : public QDialog
{
    Q_OBJECT
public:
    explicit DriverListDialog(DatabaseManager *database, QWidget *parent = nullptr);

signals:
    void driverDeleted();   // MainWindow 가 받아 재학습

private:
    void reload();
    void deleteSelected();

    DatabaseManager *m_database;
    QTableWidget *m_table;
};

#endif // DRIVERLISTDIALOG_H
