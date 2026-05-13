#pragma once

#include <QDialog>
#include <QCalendarWidget>
#include <QDateEdit>
#include <QTableWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <vector>
#include "DatabaseManager.h"

class HistoryDialog : public QDialog
{
    Q_OBJECT

public:
    explicit HistoryDialog(QWidget *parent = nullptr);

private slots:
    void onDateSelected(const QDate& date);
    void onSearchClicked();
    void onDeleteSelected();
    void onViewDetails();
    void onDateRangeChanged();

private:
    void setupUi();
    void loadHistory(const QDate& startDate, const QDate& endDate);
    void showTaskDetails(int taskId);

    QCalendarWidget* m_calendar;
    QDateEdit* m_startDate;
    QDateEdit* m_endDate;
    QComboBox* m_dateRangeCombo;
    QTableWidget* m_historyTable;
    QPushButton* m_btnSearch;
    QPushButton* m_btnDelete;
    QPushButton* m_btnViewDetails;
    QPushButton* m_btnClose;

    std::vector<DatabaseManager::TaskInfo> m_tasks;
};
