#pragma once

#include <QDialog>
#include <QDateEdit>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDate>

class DateRangeDialog : public QDialog
{
    Q_OBJECT

public:
    explicit DateRangeDialog(QWidget* parent = nullptr);
    ~DateRangeDialog();

    QDate startDate() const;
    QDate endDate() const;

private:
    QDateEdit* m_startDate;
    QDateEdit* m_endDate;
};
