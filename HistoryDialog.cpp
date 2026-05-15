#include "HistoryDialog.h"
#include <QHeaderView>
#include <QMessageBox>
#include <QDateTime>
#include <QSplitter>
#include <QFileInfo>

HistoryDialog::HistoryDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("检测历史记录");
    setMinimumSize(900, 600);
    setupUi();

    connect(m_calendar, &QCalendarWidget::clicked, this, &HistoryDialog::onDateSelected);
    connect(m_btnSearch, &QPushButton::clicked, this, &HistoryDialog::onSearchClicked);
    connect(m_btnDelete, &QPushButton::clicked, this, &HistoryDialog::onDeleteSelected);
    connect(m_btnViewDetails, &QPushButton::clicked, this, &HistoryDialog::onViewDetails);
    connect(m_btnClose, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_dateRangeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &HistoryDialog::onDateRangeChanged);

    loadHistory(QDate::currentDate(), QDate::currentDate());
}

void HistoryDialog::setupUi()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    QSplitter* splitter = new QSplitter(Qt::Horizontal);

    QWidget* leftPanel = new QWidget();
    QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);

    QLabel* calendarLabel = new QLabel("选择日期：");
    leftLayout->addWidget(calendarLabel);

    m_calendar = new QCalendarWidget();
    m_calendar->setGridVisible(true);
    leftLayout->addWidget(m_calendar);

    QWidget* dateRangeWidget = new QWidget();
    QHBoxLayout* dateRangeLayout = new QHBoxLayout(dateRangeWidget);
    dateRangeLayout->setContentsMargins(0, 0, 0, 0);

    QLabel* rangeLabel = new QLabel("时间范围：");
    dateRangeLayout->addWidget(rangeLabel);

    m_dateRangeCombo = new QComboBox();
    m_dateRangeCombo->addItems({"今天", "最近3天", "最近一周", "最近一个月", "自定义"});
    dateRangeLayout->addWidget(m_dateRangeCombo);

    leftLayout->addWidget(dateRangeWidget);

    QWidget* customDateWidget = new QWidget();
    QHBoxLayout* customDateLayout = new QHBoxLayout(customDateWidget);
    customDateLayout->setContentsMargins(0, 0, 0, 0);

    QLabel* fromLabel = new QLabel("从：");
    customDateLayout->addWidget(fromLabel);
    m_startDate = new QDateEdit(QDate::currentDate());
    m_startDate->setCalendarPopup(true);
    customDateLayout->addWidget(m_startDate);

    QLabel* toLabel = new QLabel("到：");
    customDateLayout->addWidget(toLabel);
    m_endDate = new QDateEdit(QDate::currentDate());
    m_endDate->setCalendarPopup(true);
    customDateLayout->addWidget(m_endDate);

    leftLayout->addWidget(customDateWidget);

    m_btnSearch = new QPushButton("查询");
    leftLayout->addWidget(m_btnSearch);

    leftLayout->addStretch();

    splitter->addWidget(leftPanel);

    QWidget* rightPanel = new QWidget();
    QVBoxLayout* rightLayout = new QVBoxLayout(rightPanel);

    QLabel* tableLabel = new QLabel("检测记录：");
    rightLayout->addWidget(tableLabel);

    m_historyTable = new QTableWidget();
    m_historyTable->setColumnCount(7);
    m_historyTable->setHorizontalHeaderLabels({"ID", "类型", "时间", "文件名", "目标数", "置信度/帧数", "耗时/类别"});
    m_historyTable->horizontalHeader()->setStretchLastSection(true);
    m_historyTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_historyTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_historyTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    rightLayout->addWidget(m_historyTable);

    QWidget* buttonWidget = new QWidget();
    QHBoxLayout* buttonLayout = new QHBoxLayout(buttonWidget);
    buttonLayout->setContentsMargins(0, 0, 0, 0);

    m_btnViewDetails = new QPushButton("查看详情");
    buttonLayout->addWidget(m_btnViewDetails);

    m_btnDelete = new QPushButton("删除选中");
    m_btnDelete->setStyleSheet("QPushButton { border: 1px solid #FF6B6B; color: #FF6B6B; }");
    buttonLayout->addWidget(m_btnDelete);

    buttonLayout->addStretch();

    m_btnClose = new QPushButton("关闭");
    buttonLayout->addWidget(m_btnClose);

    rightLayout->addWidget(buttonWidget);

    splitter->addWidget(rightPanel);

    mainLayout->addWidget(splitter);

    QString styleSheet = R"(
        QDialog { background-color: #0B1120; color: #E0E0E0; }
        QLabel { color: #E0E0E0; }
        QCalendarWidget { background-color: #1A2332; }
        QCalendarWidget QToolButton { color: #00E5FF; background-color: #1A2332; }
        QCalendarWidget QMenu { background-color: #1A2332; color: #E0E0E0; }
        QCalendarWidget QAbstractItemView { background-color: #0A1018; color: #E0E0E0; selection-background-color: rgba(0,229,255,0.3); }
        QDateEdit { background: #1A2332; border: 1px solid rgba(0,229,255,0.3); border-radius: 4px; padding: 4px; color: #E0E0E0; }
        QComboBox { background: #1A2332; border: 1px solid rgba(0,229,255,0.3); border-radius: 4px; padding: 4px; color: #E0E0E0; }
        QComboBox QAbstractItemView { background-color: #1A2332; color: #E0E0E0; selection-background-color: rgba(0,229,255,0.3); }
        QTableWidget { background-color: #0A1018; border: 1px solid rgba(0,229,255,0.1); color: #E0E0E0; gridline-color: rgba(0,229,255,0.1); }
        QTableWidget::item:selected { background: rgba(0,229,255,0.2); }
        QHeaderView::section { background: #1A2332; color: #00E5FF; border: 1px solid rgba(0,229,255,0.2); padding: 4px; }
        QPushButton { background: transparent; border: 1px solid #00E5FF; border-radius: 4px; color: #00E5FF; padding: 6px 12px; }
        QPushButton:hover { background: rgba(0,229,255,0.12); }
    )";
    setStyleSheet(styleSheet);
}

void HistoryDialog::onDateSelected(const QDate& date)
{
    m_startDate->setDate(date);
    m_endDate->setDate(date);
    loadHistory(date, date);
}

void HistoryDialog::onDateRangeChanged()
{
    QDate today = QDate::currentDate();
    switch (m_dateRangeCombo->currentIndex()) {
    case 0:
        m_startDate->setDate(today);
        m_endDate->setDate(today);
        break;
    case 1:
        m_startDate->setDate(today.addDays(-2));
        m_endDate->setDate(today);
        break;
    case 2:
        m_startDate->setDate(today.addDays(-6));
        m_endDate->setDate(today);
        break;
    case 3:
        m_startDate->setDate(today.addMonths(-1));
        m_endDate->setDate(today);
        break;
    default:
        break;
    }
}

void HistoryDialog::onSearchClicked()
{
    loadHistory(m_startDate->date(), m_endDate->date());
}

void HistoryDialog::loadHistory(const QDate& startDate, const QDate& endDate)
{
    m_tasks = DatabaseManager::instance().getTasksByDateRange(startDate, endDate);
    auto videoResults = DatabaseManager::instance().getVideoResultsByDateRange(startDate, endDate);

    int totalRows = static_cast<int>(m_tasks.size() + videoResults.size());
    m_historyTable->setRowCount(totalRows);

    int row = 0;
    for (size_t i = 0; i < m_tasks.size(); i++) {
        const auto& task = m_tasks[i];

        m_historyTable->setItem(row, 0, new QTableWidgetItem(QString::number(task.id)));
        m_historyTable->setItem(row, 1, new QTableWidgetItem("图片"));
        m_historyTable->setItem(row, 2, new QTableWidgetItem(task.createdAt));
        m_historyTable->setItem(row, 3, new QTableWidgetItem(QFileInfo(task.imagePath).fileName()));
        m_historyTable->setItem(row, 4, new QTableWidgetItem(QString::number(task.objectCount)));
        m_historyTable->setItem(row, 5, new QTableWidgetItem(QString("%1%").arg(task.avgConfidence * 100, 0, 'f', 1)));
        m_historyTable->setItem(row, 6, new QTableWidgetItem(QString("%1ms").arg(task.inferenceMs)));
        row++;
    }

    for (const auto& video : videoResults) {
        m_historyTable->setItem(row, 0, new QTableWidgetItem(QString::number(video.id)));
        m_historyTable->setItem(row, 1, new QTableWidgetItem("视频"));
        m_historyTable->setItem(row, 2, new QTableWidgetItem(video.createdAt));
        m_historyTable->setItem(row, 3, new QTableWidgetItem(QFileInfo(video.videoPath).fileName()));
        m_historyTable->setItem(row, 4, new QTableWidgetItem(QString::number(video.totalObjects)));
        m_historyTable->setItem(row, 5, new QTableWidgetItem(QString("%1帧").arg(video.totalFrames)));
        m_historyTable->setItem(row, 6, new QTableWidgetItem(video.classCounts));
        row++;
    }

    m_historyTable->resizeColumnsToContents();
}

void HistoryDialog::onDeleteSelected()
{
    QList<QTableWidgetItem*> selectedItems = m_historyTable->selectedItems();
    if (selectedItems.isEmpty()) {
        QMessageBox::information(this, "提示", "请先选择要删除的记录");
        return;
    }

    QSet<int> selectedRows;
    for (QTableWidgetItem* item : selectedItems) {
        selectedRows.insert(item->row());
    }

    int result = QMessageBox::question(this, "确认删除",
        QString("确定要删除选中的 %1 条记录吗？").arg(selectedRows.size()));

    if (result == QMessageBox::Yes) {
        for (int row : selectedRows) {
            int id = m_historyTable->item(row, 0)->text().toInt();
            QString type = m_historyTable->item(row, 1)->text();
            
            if (type == "视频") {
                DatabaseManager::instance().deleteVideoResult(id);
            } else {
                DatabaseManager::instance().deleteTask(id);
            }
        }
        loadHistory(m_startDate->date(), m_endDate->date());
    }
}

void HistoryDialog::onViewDetails()
{
    QList<QTableWidgetItem*> selectedItems = m_historyTable->selectedItems();
    if (selectedItems.isEmpty()) {
        QMessageBox::information(this, "提示", "请先选择要查看的记录");
        return;
    }

    int row = selectedItems.first()->row();
    int taskId = m_historyTable->item(row, 0)->text().toInt();
    showTaskDetails(taskId);
}

void HistoryDialog::showTaskDetails(int taskId)
{
    auto detections = DatabaseManager::instance().getDetectionsByTaskId(taskId);

    QDialog detailDialog(this);
    detailDialog.setWindowTitle(QString("检测详情 - 任务 %1").arg(taskId));
    detailDialog.setMinimumSize(500, 400);

    QVBoxLayout* layout = new QVBoxLayout(&detailDialog);

    QTableWidget* detailTable = new QTableWidget();
    detailTable->setColumnCount(6);
    detailTable->setHorizontalHeaderLabels({"类别", "置信度", "X", "Y", "宽度", "高度"});
    detailTable->setRowCount(static_cast<int>(detections.size()));

    for (size_t i = 0; i < detections.size(); i++) {
        const auto& det = detections[i];
        detailTable->setItem(i, 0, new QTableWidgetItem(det.className));
        detailTable->setItem(i, 1, new QTableWidgetItem(QString("%1%").arg(det.confidence * 100, 0, 'f', 1)));
        detailTable->setItem(i, 2, new QTableWidgetItem(QString::number(det.bbox.x(), 'f', 1)));
        detailTable->setItem(i, 3, new QTableWidgetItem(QString::number(det.bbox.y(), 'f', 1)));
        detailTable->setItem(i, 4, new QTableWidgetItem(QString::number(det.bbox.width(), 'f', 1)));
        detailTable->setItem(i, 5, new QTableWidgetItem(QString::number(det.bbox.height(), 'f', 1)));
    }

    detailTable->resizeColumnsToContents();
    layout->addWidget(detailTable);

    QPushButton* closeBtn = new QPushButton("关闭");
    connect(closeBtn, &QPushButton::clicked, &detailDialog, &QDialog::accept);
    layout->addWidget(closeBtn);

    detailDialog.exec();
}
