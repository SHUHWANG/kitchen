#include "DateRangeDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QDate>

DateRangeDialog::DateRangeDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("选择报告时间范围");
    setMinimumWidth(400);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // 开始日期
    QHBoxLayout* startLayout = new QHBoxLayout();
    startLayout->addWidget(new QLabel("开始日期："));
    m_startDate = new QDateEdit();
    m_startDate->setCalendarPopup(true);
    m_startDate->setDate(QDate::currentDate().addMonths(-1));  // 默认一个月前
    startLayout->addWidget(m_startDate);
    mainLayout->addLayout(startLayout);

    // 结束日期
    QHBoxLayout* endLayout = new QHBoxLayout();
    endLayout->addWidget(new QLabel("结束日期："));
    m_endDate = new QDateEdit();
    m_endDate->setCalendarPopup(true);
    m_endDate->setDate(QDate::currentDate());  // 默认今天
    endLayout->addWidget(m_endDate);
    mainLayout->addLayout(endLayout);

    // 快捷按钮
    QHBoxLayout* quickLayout = new QHBoxLayout();
    
    QPushButton* btnWeek = new QPushButton("最近一周");
    connect(btnWeek, &QPushButton::clicked, [this]() {
        m_startDate->setDate(QDate::currentDate().addDays(-7));
        m_endDate->setDate(QDate::currentDate());
    });
    quickLayout->addWidget(btnWeek);

    QPushButton* btnMonth = new QPushButton("最近一月");
    connect(btnMonth, &QPushButton::clicked, [this]() {
        m_startDate->setDate(QDate::currentDate().addMonths(-1));
        m_endDate->setDate(QDate::currentDate());
    });
    quickLayout->addWidget(btnMonth);

    QPushButton* btnAll = new QPushButton("全部数据");
    connect(btnAll, &QPushButton::clicked, [this]() {
        m_startDate->setDate(QDate(2020, 1, 1));
        m_endDate->setDate(QDate::currentDate());
    });
    quickLayout->addWidget(btnAll);

    mainLayout->addLayout(quickLayout);

    // 确认取消按钮
    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    
    QPushButton* btnOk = new QPushButton("确定");
    connect(btnOk, &QPushButton::clicked, this, &QDialog::accept);
    btnLayout->addWidget(btnOk);

    QPushButton* btnCancel = new QPushButton("取消");
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(btnCancel);

    mainLayout->addLayout(btnLayout);
}

DateRangeDialog::~DateRangeDialog()
{
}

QDate DateRangeDialog::startDate() const
{
    return m_startDate->date();
}

QDate DateRangeDialog::endDate() const
{
    return m_endDate->date();
}
