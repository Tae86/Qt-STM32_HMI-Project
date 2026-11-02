#include "driverlistdialog.h"
#include "databasemanager.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

DriverListDialog::DriverListDialog(DatabaseManager *database, QWidget *parent)
    : QDialog(parent), m_database(database)
{
    setWindowTitle(tr("운전자 조회"));
    resize(560, 320);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({tr("ID"), tr("이름"), tr("샘플 수"),
                                        tr("등록일"), tr("최근 인증")});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);

    auto *deleteButton = new QPushButton(tr("선택 운전자 삭제"), this);
    auto *closeButton = new QPushButton(tr("닫기"), this);
    connect(deleteButton, &QPushButton::clicked, this, &DriverListDialog::deleteSelected);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(deleteButton);
    buttons->addStretch(1);
    buttons->addWidget(closeButton);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_table);
    layout->addLayout(buttons);

    reload();
}

void DriverListDialog::reload()
{
    const QVector<DriverRow> rows = m_database->listDrivers();
    m_table->setRowCount(rows.size());
    for (int r = 0; r < rows.size(); ++r) {
        const DriverRow &row = rows.at(r);
        const QStringList values{QString::number(row.id), row.name,
                                 QString::number(row.sampleCount), row.createdAt,
                                 row.lastAuth.isEmpty() ? QStringLiteral("-") : row.lastAuth};
        for (int c = 0; c < values.size(); ++c)
            m_table->setItem(r, c, new QTableWidgetItem(values.at(c)));
    }
}

void DriverListDialog::deleteSelected()
{
    const int row = m_table->currentRow();
    if (row < 0)
        return;

    const int driverId = m_table->item(row, 0)->text().toInt();
    const QString name = m_table->item(row, 1)->text();
    if (QMessageBox::question(this, tr("삭제 확인"),
                              tr("'%1' 운전자와 얼굴 샘플을 모두 삭제할까요?").arg(name))
            != QMessageBox::Yes) {
        return;
    }

    QString error;
    if (!m_database->deleteDriver(driverId, &error)) {
        QMessageBox::critical(this, tr("삭제 실패"), error);
        return;
    }
    reload();
    emit driverDeleted();
}
