#pragma warning(disable: 4996)
#include "mainwindow.h"
#include "ui_MainWindow.h"
#include "InferenceEngine.h"
#include "DatabaseManager.h"
#include "AnalysisAgent.h"
#include "HistoryDialog.h"

#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QPainter>
#include <QPen>
#include <QFont>
#include <QElapsedTimer>
#include <QDateTime>
#include <QStandardPaths>
#include <QScrollBar>
#include <QDialog>
#include <QVBoxLayout>
#include <QLabel>
#include <QKeyEvent>

void DetectionWorker::processImage(int index, const QString& imagePath)
{
    if (!m_engine || !m_engine->isLoaded()) {
        emit error(index, "推理引擎未加载");
        return;
    }

    QElapsedTimer timer;
    timer.start();

    std::vector<Detection> results = m_engine->detect(imagePath);
    qint64 elapsed = timer.elapsed();

    emit detectionComplete(index, results, elapsed);
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setupUiStyle();
    setupConnections();

    m_engine = new InferenceEngine(this);
    m_agent = new AnalysisAgent(this);

    m_autoFollowTimer = new QTimer(this);
    m_autoFollowTimer->setSingleShot(true);
    m_autoFollowTimer->setInterval(3000);
    connect(m_autoFollowTimer, &QTimer::timeout, this, &MainWindow::onAutoFollowTimeout);

    DatabaseManager::instance().initialize();

    QStringList enginePaths = {
        QCoreApplication::applicationDirPath() + "/models/yolov8s_150.engine",
        QCoreApplication::applicationDirPath() + "/../models/yolov8s_150.engine",
        QCoreApplication::applicationDirPath() + "/../../models/yolov8s_150.engine",
        QCoreApplication::applicationDirPath() + "/../../../models/yolov8s_150.engine"
    };

    QString enginePath;
    for (const QString& path : enginePaths) {
        if (QFileInfo::exists(path)) {
            enginePath = path;
            break;
        }
    }

    if (!enginePath.isEmpty()) {
        if (m_engine->loadEngine(enginePath)) {
            ui->lblStatusInfo->setText("模型已加载");
            qDebug() << "Engine loaded from:" << enginePath;
        } else {
            ui->lblStatusInfo->setText("模型加载失败");
        }
    } else {
        ui->lblStatusInfo->setText("未找到模型文件");
        qDebug() << "Searched paths:" << enginePaths;
    }

    setupDetectionThread();

    ui->progressBar->setVisible(false);
    ui->btnPauseDetection->setVisible(false);
    ui->btnStopDetection->setVisible(false);
    addAgentMessage("空中侦察分析员已就绪。支持查询：检测到了多少车辆？哪些区域行人密集？各类目标的数量统计？");
}

MainWindow::~MainWindow()
{
    if (m_detectionThread) {
        m_detectionThread->quit();
        m_detectionThread->wait();
    }
    delete ui;
}

void MainWindow::setupDetectionThread()
{
    m_detectionThread = new QThread(this);
    m_worker = new DetectionWorker(m_engine);
    m_worker->moveToThread(m_detectionThread);

    connect(this, &MainWindow::destroyed, m_detectionThread, &QThread::quit);
    connect(m_detectionThread, &QThread::finished, m_worker, &QObject::deleteLater);

    qRegisterMetaType<std::vector<Detection>>("std::vector<Detection>");

    connect(m_worker, &DetectionWorker::detectionComplete, this, &MainWindow::onDetectionComplete, Qt::QueuedConnection);
    connect(m_worker, &DetectionWorker::error, this, &MainWindow::onDetectionError, Qt::QueuedConnection);

    m_detectionThread->start();
}

void MainWindow::setupConnections()
{
    connect(ui->btnImportImage, &QPushButton::clicked, this, &MainWindow::onImportImages);
    connect(ui->btnImportFolder, &QPushButton::clicked, this, &MainWindow::onImportFolder);
    connect(ui->btnStartDetection, &QPushButton::clicked, this, &MainWindow::onStartDetection);
    connect(ui->btnPauseDetection, &QPushButton::clicked, this, &MainWindow::onPauseDetection);
    connect(ui->btnStopDetection, &QPushButton::clicked, this, &MainWindow::onStopDetection);
    connect(ui->btnReset, &QPushButton::clicked, this, &MainWindow::onResetDetection);
    connect(ui->btnToggleMode, &QPushButton::clicked, this, &MainWindow::onToggleViewMode);
    connect(ui->sendButton, &QPushButton::clicked, this, &MainWindow::onSendMessage);
    connect(ui->messageInput, &QLineEdit::returnPressed, this, &MainWindow::onSendMessage);
    connect(ui->btnExportCSV, &QPushButton::clicked, this, &MainWindow::onExportCSV);
    connect(ui->btnGenerateReport, &QPushButton::clicked, this, &MainWindow::onGenerateReport);
    connect(ui->chkFilterDetections, &QCheckBox::checkStateChanged, this, &MainWindow::onFilterDetectionsChanged);
    connect(ui->imageListWidget, &QListWidget::itemClicked, this, &MainWindow::onImageListItemClicked);
    connect(ui->btnPrevImage, &QPushButton::clicked, this, &MainWindow::onPrevImage);
    connect(ui->btnNextImage, &QPushButton::clicked, this, &MainWindow::onNextImage);
    connect(ui->btnClearChat, &QPushButton::clicked, this, &MainWindow::onClearChat);
    connect(ui->btnHistory, &QPushButton::clicked, this, &MainWindow::onShowHistory);
    ui->imageViewer->installEventFilter(this);
    ui->imageViewer->setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
}

void MainWindow::setupUiStyle()
{
    QString qss = R"(
        QWidget {
            font-family: "Microsoft YaHei", "Segoe UI", "Consolas";
            font-size: 13px;
            color: #E0E0E0;
            background-color: #0B1120;
        }
        QMainWindow { background-color: #0B1120; }
        #topToolBar {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #0D1B2A, stop:0.5 #112240, stop:1 #0D1B2A);
            border-bottom: 1px solid rgba(0,229,255,0.3);
        }
        #lblLogo { font-size: 16px; font-weight: bold; color: #00E5FF; padding: 4px 8px; }
        #lblStatusInfo { color: #00FF88; font-size: 12px; }
        QFrame[frameShape="5"] { color: rgba(0,229,255,0.3); max-width: 1px; }
        #leftMainPanel { background-color: #0F1923; border: 1px solid rgba(0,229,255,0.15); border-radius: 8px; }
        #imageListPanel { background-color: #111D2E; border: 1px solid rgba(0,229,255,0.1); border-radius: 6px; }
        #lblImageListTitle, #lblImageCount { color: #8892A0; font-size: 12px; }
        #centerPanel { background-color: #111D2E; border: 1px solid rgba(0,229,255,0.1); border-radius: 6px; }
        #lblImageName { color: #00E5FF; font-size: 13px; font-weight: bold; }
        #lblImageIndex { color: #8892A0; font-size: 12px; }
        #imageViewer { background-color: #0A1018; border: 1px dashed rgba(0,229,255,0.2); border-radius: 4px; color: #4A5568; font-size: 14px; }
        #detectionInfoBar { background: rgba(0,229,255,0.05); border: 1px solid rgba(0,229,255,0.15); border-radius: 4px; }
        #lblDetectionSummary { color: #E0E0E0; }
        #lblObjectCount, #lblInferenceTime { color: #00E5FF; font-size: 12px; }
        #rightPanel { background-color: #0F1923; border: 1px solid rgba(0,229,255,0.2); border-radius: 8px; }
        #dashboardFrame { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 rgba(0,229,255,0.08), stop:1 rgba(0,229,255,0.02)); border: 1px solid rgba(0,229,255,0.2); border-radius: 8px; }
        #lblDashboardTitle { font-size: 14px; font-weight: bold; color: #00E5FF; padding-bottom: 4px; border-bottom: 1px solid rgba(0,229,255,0.15); }
        #statCardTotal, #statCardObjects, #statCardClasses, #statCardConfidence { background: rgba(15, 25, 40, 0.8); border: 1px solid rgba(0,229,255,0.15); border-radius: 6px; padding: 8px; }
        #lblStatTotalLabel, #lblStatObjectsLabel, #lblStatClassesLabel, #lblStatConfLabel { color: #8892A0; font-size: 11px; }
        #lblStatTotalValue, #lblStatObjectsValue, #lblStatClassesValue { color: #00E5FF; font-size: 20px; font-weight: bold; }
        #lblStatConfValue { color: #00FF88; font-size: 20px; font-weight: bold; }
        #lblTopCategory { color: #FFB000; font-size: 12px; padding-top: 4px; border-top: 1px solid rgba(0,229,255,0.1); }
        #chatFrame { background: rgba(15, 25, 40, 0.6); border: 1px solid rgba(0,229,255,0.15); border-radius: 8px; }
        #lblChatTitle { font-size: 14px; font-weight: bold; color: #00E5FF; padding-bottom: 4px; border-bottom: 1px solid rgba(0,229,255,0.15); }
        #conversationView { background-color: #0A1018; border: 1px solid rgba(0,229,255,0.1); border-radius: 6px; padding: 8px; color: #E0E0E0; }
        #snapshotFrame { background: rgba(15, 25, 40, 0.6); border: 1px solid rgba(0,229,255,0.15); border-radius: 8px; }
        #lblSnapshotTitle { font-size: 14px; font-weight: bold; color: #00E5FF; }
        #lblSnapshotCount { color: #8892A0; font-size: 12px; }
        QPushButton { background: transparent; border: 1px solid #00E5FF; border-radius: 6px; color: #00E5FF; padding: 6px 14px; font-size: 12px; }
        QPushButton:hover { background: rgba(0,229,255,0.12); border-color: #33ffff; }
        QPushButton:pressed { background: rgba(0,229,255,0.25); }
        QPushButton:disabled { color: #4A5568; border-color: #4A5568; }
        #btnStartDetection { border: 1px solid #00FF88; color: #00FF88; font-weight: bold; }
        #btnStartDetection:hover { background: rgba(0,255,136,0.12); border-color: #66ffb2; }
        #btnPauseDetection { border: 1px solid #FFB000; color: #FFB000; }
        #btnPauseDetection:hover { background: rgba(255,176,0,0.12); }
        #btnStopDetection { border: 1px solid #FF6B6B; color: #FF6B6B; }
        #btnStopDetection:hover { background: rgba(255,107,107,0.12); }
        #btnReset { border: 1px solid #FF6B6B; color: #FF6B6B; }
        #btnReset:hover { background: rgba(255,107,107,0.12); }
        #btnPrevImage, #btnNextImage { border: 1px solid rgba(0,229,255,0.5); color: #00E5FF; padding: 4px 8px; font-size: 14px; }
        QLineEdit { background: #1A2332; border: 1px solid rgba(0,229,255,0.3); border-radius: 6px; padding: 6px 10px; color: white; selection-background-color: #00E5FF; }
        QLineEdit:focus { border-color: #00E5FF; }
        QProgressBar { background: #1A2332; border: none; border-radius: 4px; height: 8px; text-align: center; color: #00E5FF; font-size: 10px; }
        QProgressBar::chunk { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #00E5FF, stop:1 #00FF88); border-radius: 4px; }
        QCheckBox { color: #8892A0; spacing: 6px; }
        QCheckBox::indicator { width: 14px; height: 14px; border: 1px solid rgba(0,229,255,0.3); border-radius: 3px; background: #1A2332; }
        QCheckBox::indicator:checked { background: #00E5FF; border-color: #00E5FF; }
        QListWidget { background-color: #0A1018; border: 1px solid rgba(0,229,255,0.1); border-radius: 4px; outline: none; }
        QListWidget::item { background: transparent; border: 1px solid transparent; border-radius: 4px; padding: 4px; }
        QListWidget::item:selected { background: rgba(0,229,255,0.15); border-color: rgba(0,229,255,0.3); }
        QListWidget::item:hover { background: rgba(0,229,255,0.08); }
        QScrollArea { border: none; background: transparent; }
        QScrollBar:vertical { border: none; background: rgba(255,255,255,0.03); width: 6px; margin: 0px; }
        QScrollBar::handle:vertical { background: rgba(0,229,255,0.25); border-radius: 3px; min-height: 20px; }
        QScrollBar::handle:vertical:hover { background: rgba(0,229,255,0.5); }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }
        QScrollBar:horizontal { border: none; background: rgba(255,255,255,0.03); height: 6px; margin: 0px; }
        QScrollBar::handle:horizontal { background: rgba(0,229,255,0.25); border-radius: 3px; min-width: 20px; }
        QScrollBar::handle:horizontal:hover { background: rgba(0,229,255,0.5); }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }
        QToolTip { background-color: #1A2332; color: #E0E0E0; border: 1px solid #00E5FF; border-radius: 4px; padding: 4px 8px; }
        QSplitter::handle { background: rgba(0,229,255,0.2); width: 3px; }
        QSplitter::handle:hover { background: rgba(0,229,255,0.5); }
    )";
    this->setStyleSheet(qss);
}

void MainWindow::onImportImages()
{
    QStringList files = QFileDialog::getOpenFileNames(
        this, "选择图片", "",
        "图片文件 (*.jpg *.jpeg *.png *.bmp *.tiff);;所有文件 (*)"
    );

    if (files.empty()) return;

    m_images.clear();
    ui->imageListWidget->clear();

    for (const QString& file : files) {
        QImage img(file);
        if (img.isNull()) continue;

        ImageInfo info;
        info.filePath = file;
        info.fileName = QFileInfo(file).fileName();
        info.width = img.width();
        info.height = img.height();
        m_images.push_back(info);
    }

    loadImageList();
    ui->lblStatusInfo->setText(QString("已导入 %1 张图片").arg(m_images.size()));

    if (!m_images.empty()) {
        ui->imageListWidget->setCurrentRow(0);
        displayImage(0);
    }
}

void MainWindow::onImportFolder()
{
    QString dir = QFileDialog::getExistingDirectory(this, "选择图片文件夹");
    if (dir.isEmpty()) return;

    QDir folder(dir);
    QStringList filters;
    filters << "*.jpg" << "*.jpeg" << "*.png" << "*.bmp" << "*.tiff";
    QFileInfoList fileList = folder.entryInfoList(filters, QDir::Files, QDir::Name);

    if (fileList.isEmpty()) {
        QMessageBox::information(this, "提示", "文件夹中没有找到图片文件");
        return;
    }

    m_images.clear();
    ui->imageListWidget->clear();

    for (const QFileInfo& fi : fileList) {
        QImage img(fi.absoluteFilePath());
        if (img.isNull()) continue;

        ImageInfo info;
        info.filePath = fi.absoluteFilePath();
        info.fileName = fi.fileName();
        info.width = img.width();
        info.height = img.height();
        m_images.push_back(info);
    }

    loadImageList();
    ui->lblStatusInfo->setText(QString("已导入 %1 张图片").arg(m_images.size()));

    if (!m_images.empty()) {
        ui->imageListWidget->setCurrentRow(0);
        displayImage(0);
    }
}

void MainWindow::loadImageList()
{
    ui->imageListWidget->clear();
    ui->lblImageCount->setText(QString("共 %1 张").arg(m_images.size()));

    QStringList imageNames;
    for (size_t i = 0; i < m_images.size(); i++) {
        const auto& img = m_images[i];
        QListWidgetItem* item = new QListWidgetItem();
        item->setText(img.fileName);
        item->setData(Qt::UserRole, static_cast<int>(i));
        item->setToolTip(img.filePath);
        item->setFlags(item->flags() | Qt::ItemIsSelectable);
        ui->imageListWidget->addItem(item);
        imageNames.append(img.fileName);
    }

    m_agent->setImageList(imageNames);
}

void MainWindow::displayImage(int index)
{
    if (index < 0 || index >= static_cast<int>(m_images.size())) return;

    m_currentIndex = index;
    const auto& img = m_images[index];

    ui->lblImageName->setText(img.fileName);
    ui->lblImageIndex->setText(QString("%1 / %2").arg(index + 1).arg(m_images.size()));
    ui->imageListWidget->setCurrentRow(index);

    m_agent->setCurrentDetections(img.detections);

    if (img.detected && m_showAnnotated) {
        QImage annotated(img.filePath);
        drawDetections(annotated, img.detections);
        QPixmap pixmap = QPixmap::fromImage(annotated);
        ui->imageViewer->setPixmap(pixmap.scaled(ui->imageViewer->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));

        ui->lblDetectionSummary->setText(QString("检测概要：检测到 %1 个目标").arg(img.detections.size()));
        ui->lblObjectCount->setText(QString("目标数：%1").arg(img.detections.size()));
        ui->lblInferenceTime->setText(QString("推理耗时：%1ms").arg(m_lastInferenceMs));

        updateSnapshotBar(img.detections);
    } else {
        QPixmap pixmap(img.filePath);
        ui->imageViewer->setPixmap(pixmap.scaled(ui->imageViewer->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));

        if (img.detected) {
            ui->lblDetectionSummary->setText("当前显示：原始图片");
        } else {
            ui->lblDetectionSummary->setText("检测概要：等待检测...");
        }
        ui->lblObjectCount->setText(QString("目标数：%1").arg(img.detected ? img.detections.size() : 0));
    }
}

void MainWindow::drawDetections(QImage& image, const std::vector<Detection>& detections)
{
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);

    const auto& config = DetectionConfig::instance();

    for (const auto& det : detections) {
        QColor color = Detection::getClassColor(det.classId);

        QPen pen(color, 2);
        painter.setPen(pen);

        QRectF rect = det.bbox;
        painter.drawRect(rect);

        QString label = QString("%1 %2%").arg(det.className).arg(det.confidence * 100, 0, 'f', 0);

        QFont font("Microsoft YaHei", 10, QFont::Bold);
        painter.setFont(font);

        QFontMetrics fm(font);
        int textWidth = fm.horizontalAdvance(label) + 8;
        int textHeight = fm.height() + 4;

        QRectF labelRect(rect.x(), rect.y() - textHeight, textWidth, textHeight);
        if (labelRect.y() < 0) labelRect.moveTop(rect.y());

        painter.fillRect(labelRect, color);
        painter.setPen(QColor(255, 255, 255));
        painter.drawText(labelRect, Qt::AlignCenter, label);
    }

    painter.end();
}

void MainWindow::onStartDetection()
{
    if (m_images.empty()) {
        QMessageBox::information(this, "提示", "请先导入图片");
        return;
    }

    if (!m_engine->isLoaded()) {
        QMessageBox::warning(this, "错误", "推理引擎未加载，请检查模型文件");
        return;
    }

    DatabaseManager::instance().clearAll();

    m_completedCount = 0;
    m_totalCount = m_images.size();
    m_nextDetectIndex = 0;
    m_isPaused = false;
    m_isDetecting = true;

    for (auto& img : m_images) {
        img.detected = false;
        img.detections.clear();
    }

    ui->progressBar->setVisible(true);
    ui->progressBar->setValue(0);
    ui->lblStatusInfo->setText("检测中...");
    ui->btnStartDetection->setEnabled(false);
    ui->btnStartDetection->setVisible(false);
    ui->btnPauseDetection->setVisible(true);
    ui->btnPauseDetection->setEnabled(true);
    ui->btnStopDetection->setVisible(true);
    ui->btnStopDetection->setEnabled(true);
    ui->btnPauseDetection->setText("暂停");

    m_autoFollowTimer->start();
    processNextImage();
}

void MainWindow::processNextImage()
{
    if (!m_isDetecting || m_isPaused) return;
    if (m_nextDetectIndex >= m_totalCount) return;

    QMetaObject::invokeMethod(m_worker, "processImage", Qt::QueuedConnection,
        Q_ARG(int, m_nextDetectIndex),
        Q_ARG(QString, m_images[m_nextDetectIndex].filePath)
    );
    m_nextDetectIndex++;
}

void MainWindow::onPauseDetection()
{
    if (m_isPaused) {
        m_isPaused = false;
        ui->btnPauseDetection->setText("暂停");
        ui->lblStatusInfo->setText("检测中...");
        processNextImage();
    } else {
        m_isPaused = true;
        ui->btnPauseDetection->setText("继续");
        ui->lblStatusInfo->setText("已暂停");
    }
}

void MainWindow::onStopDetection()
{
    m_isDetecting = false;
    m_isPaused = false;
    m_autoFollowTimer->stop();

    ui->btnStartDetection->setEnabled(true);
    ui->btnStartDetection->setVisible(true);
    ui->btnPauseDetection->setVisible(false);
    ui->btnStopDetection->setVisible(false);
    ui->lblStatusInfo->setText(QString("已停止，完成 %1/%2").arg(m_completedCount).arg(m_totalCount));

    updateDashboard();
}

void MainWindow::onDetectionComplete(int index, const std::vector<Detection>& results, qint64 elapsedMs)
{
    if (index < 0 || index >= static_cast<int>(m_images.size())) return;

    m_images[index].detections = results;
    m_images[index].detected = true;

    int taskId = DatabaseManager::instance().createTask(
        m_images[index].filePath, m_images[index].width, m_images[index].height
    );

    float totalConf = 0;
    for (const auto& det : results) {
        DatabaseManager::instance().insertDetection(taskId, det);
        totalConf += det.confidence;
    }
    float avgConf = results.empty() ? 0 : totalConf / results.size();
    DatabaseManager::instance().updateTaskResult(taskId, results.size(), avgConf, elapsedMs);

    m_completedCount++;
    m_lastInferenceMs = elapsedMs;
    updateProgressBar(m_completedCount, m_totalCount);

    if (!m_userInteracting) {
        displayImage(index);
    }

    if (m_completedCount == m_totalCount) {
        m_isDetecting = false;
        ui->lblStatusInfo->setText(QString("检测完成，共 %1 张图片").arg(m_totalCount));
        ui->btnStartDetection->setEnabled(true);
        ui->btnStartDetection->setVisible(true);
        ui->btnPauseDetection->setVisible(false);
        ui->btnStopDetection->setVisible(false);
        updateDashboard();
        addAgentMessage(QString("检测完成！共处理 %1 张图片，检测到 %2 个目标。")
            .arg(m_totalCount)
            .arg(DatabaseManager::instance().getTotalObjects()));
    } else if (m_isDetecting && !m_isPaused) {
        processNextImage();
    }
}

void MainWindow::onDetectionError(int index, const QString& message)
{
    Q_UNUSED(index);
    Q_UNUSED(message);
    m_completedCount++;
    updateProgressBar(m_completedCount, m_totalCount);

    if (m_completedCount == m_totalCount) {
        m_isDetecting = false;
        ui->lblStatusInfo->setText("检测完成（部分失败）");
        ui->btnStartDetection->setEnabled(true);
        ui->btnStartDetection->setVisible(true);
        ui->btnPauseDetection->setVisible(false);
        ui->btnStopDetection->setVisible(false);
    } else if (m_isDetecting && !m_isPaused) {
        processNextImage();
    }
}

void MainWindow::updateProgressBar(int current, int total)
{
    int percent = total > 0 ? (current * 100 / total) : 0;
    ui->progressBar->setValue(percent);
    ui->lblStatusInfo->setText(QString("检测中... %1/%2").arg(current).arg(total));
}

void MainWindow::updateDashboard()
{
    auto& db = DatabaseManager::instance();

    int totalTasks = db.getTotalTasks();
    int totalObjects = db.getTotalObjects();
    float avgConf = db.getAverageConfidence();
    QString topClass = db.getTopClass();

    ui->lblStatTotalValue->setText(QString::number(totalTasks));
    ui->lblStatObjectsValue->setText(QString::number(totalObjects));

    auto stats = db.getClassStatistics();
    ui->lblStatClassesValue->setText(QString::number(stats.size()));
    ui->lblStatConfValue->setText(QString("%1%").arg(avgConf * 100, 0, 'f', 1));
    ui->lblTopCategory->setText(QString("最多检测类别：%1").arg(topClass));
}

void MainWindow::updateSnapshotBar(const std::vector<Detection>& detections)
{
    QLayout* layout = ui->snapshotContainer->layout();
    if (!layout) {
        layout = new QVBoxLayout(ui->snapshotContainer);
        ui->snapshotContainer->setLayout(layout);
    }

    while (layout->count() > 0) {
        QLayoutItem* item = layout->takeAt(0);
        if (item->widget()) {
            delete item->widget();
        }
        delete item;
    }

    if (detections.empty()) {
        ui->lblSnapshotCount->setText("0个目标");
        QLabel* emptyLabel = new QLabel("暂无检测目标");
        emptyLabel->setStyleSheet("color: #8892A0; padding: 10px;");
        emptyLabel->setAlignment(Qt::AlignCenter);
        layout->addWidget(emptyLabel);
        return;
    }

    ui->lblSnapshotCount->setText(QString("%1个目标").arg(detections.size()));

    QString imagePath = m_images[m_currentIndex].filePath;
    QImage sourceImage(imagePath);

    for (size_t i = 0; i < detections.size() && i < 20; i++) {
        const auto& det = detections[i];

        QFrame* card = new QFrame();
        card->setStyleSheet("QFrame { background: rgba(0,229,255,0.05); border: 1px solid rgba(0,229,255,0.15); border-radius: 4px; padding: 4px; }");
        card->setMaximumHeight(80);

        QHBoxLayout* cardLayout = new QHBoxLayout(card);
        cardLayout->setContentsMargins(4, 4, 4, 4);
        cardLayout->setSpacing(8);

        QLabel* thumbLabel = new QLabel();
        thumbLabel->setFixedSize(60, 60);
        thumbLabel->setStyleSheet("border: 1px solid rgba(0,229,255,0.3); border-radius: 3px;");

        QRectF bbox = det.bbox;
        int x = std::max(0, static_cast<int>(bbox.x()));
        int y = std::max(0, static_cast<int>(bbox.y()));
        int w = std::min(static_cast<int>(bbox.width()), sourceImage.width() - x);
        int h = std::min(static_cast<int>(bbox.height()), sourceImage.height() - y);

        if (w > 0 && h > 0) {
            QImage cropped = sourceImage.copy(x, y, w, h).scaled(60, 60, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            thumbLabel->setPixmap(QPixmap::fromImage(cropped));
        }

        QLabel* infoLabel = new QLabel(QString("%1\n%2%").arg(det.className).arg(det.confidence * 100, 0, 'f', 1));
        infoLabel->setStyleSheet("color: #E0E0E0; font-size: 11px;");
        infoLabel->setWordWrap(true);

        cardLayout->addWidget(thumbLabel);
        cardLayout->addWidget(infoLabel, 1);

        layout->addWidget(card);
    }

    layout->addItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));
}

void MainWindow::onResetDetection()
{
    m_images.clear();
    m_currentIndex = -1;
    m_completedCount = 0;
    m_totalCount = 0;
    m_isDetecting = false;
    m_isPaused = false;
    m_autoFollowTimer->stop();

    ui->imageListWidget->clear();
    ui->imageViewer->clear();
    ui->imageViewer->setText("请导入图片开始检测");

    ui->progressBar->setValue(0);
    ui->progressBar->setVisible(false);
    ui->lblStatusInfo->setText("就绪");
    ui->lblImageName->setText("未选择图片");
    ui->lblImageIndex->setText("0 / 0");
    ui->lblImageCount->setText("共 0 张");
    ui->lblDetectionSummary->setText("检测概要：等待检测...");
    ui->lblObjectCount->setText("目标数：0");
    ui->lblInferenceTime->setText("推理耗时：--ms");

    ui->lblStatTotalValue->setText("0");
    ui->lblStatObjectsValue->setText("0");
    ui->lblStatClassesValue->setText("0");
    ui->lblStatConfValue->setText("--");
    ui->lblTopCategory->setText("最多检测类别：--");

    ui->chkFilterDetections->setChecked(false);
    ui->btnStartDetection->setEnabled(true);
    ui->btnStartDetection->setVisible(true);
    ui->btnPauseDetection->setVisible(false);
    ui->btnStopDetection->setVisible(false);
}

void MainWindow::onToggleViewMode()
{
    m_showAnnotated = !m_showAnnotated;
    if (m_currentIndex >= 0) {
        displayImage(m_currentIndex);
    }
}

void MainWindow::onPrevImage()
{
    if (m_images.empty()) return;
    m_userInteracting = true;
    m_autoFollowTimer->start();
    int newIndex = m_currentIndex - 1;
    if (newIndex < 0) newIndex = static_cast<int>(m_images.size()) - 1;
    displayImage(newIndex);
}

void MainWindow::onNextImage()
{
    if (m_images.empty()) return;
    m_userInteracting = true;
    m_autoFollowTimer->start();
    int newIndex = m_currentIndex + 1;
    if (newIndex >= static_cast<int>(m_images.size())) newIndex = 0;
    displayImage(newIndex);
}

void MainWindow::onAutoFollowTimeout()
{
    m_userInteracting = false;
    if (m_isDetecting && m_completedCount > 0) {
        displayImage(m_completedCount - 1);
    }
}

void MainWindow::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Left) {
        onPrevImage();
    } else if (event->key() == Qt::Key_Right) {
        onNextImage();
    } else {
        QMainWindow::keyPressEvent(event);
    }
}

void MainWindow::onSendMessage()
{
    QString text = ui->messageInput->text().trimmed();
    if (text.isEmpty()) return;

    QString userHtml = QString(
        "<div style='text-align: right; margin-bottom: 8px;'>"
        "<span style='background-color: #0052CC; padding: 8px 14px; border-radius: 10px; color: white; display: inline-block; max-width: 80%;'>"
        "%1"
        "</span></div>"
    ).arg(text.toHtmlEscaped());

    ui->conversationView->append(userHtml);
    ui->messageInput->clear();

    QString response = m_agent->processQuery(text);
    addAgentMessage(response);
}

void MainWindow::addAgentMessage(const QString& message)
{
    QString html = QString(
        "<div style='margin-bottom: 8px;'>"
        "<span style='background-color: #1a2740; padding: 8px 14px; border-radius: 10px; color: #E0E0E0; display: inline-block; border: 1px solid rgba(0,229,255,0.2);'>"
        "%1"
        "</span></div>"
    ).arg(message.toHtmlEscaped().replace("\n", "<br>"));

    ui->conversationView->append(html);

    QScrollBar* sb = ui->conversationView->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void MainWindow::onExportCSV()
{
    QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation) + "/detection_results.csv";
    QString filePath = QFileDialog::getSaveFileName(this, "导出CSV", defaultPath, "CSV文件 (*.csv)");

    if (filePath.isEmpty()) return;

    if (DatabaseManager::instance().exportToCSV(filePath)) {
        QMessageBox::information(this, "成功", "CSV文件已导出到：\n" + filePath);
    } else {
        QMessageBox::warning(this, "错误", "导出CSV失败");
    }
}

void MainWindow::onGenerateReport()
{
    QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation) + "/detection_report.html";
    QString filePath = QFileDialog::getSaveFileName(this, "生成报告", defaultPath, "HTML文件 (*.html)");

    if (filePath.isEmpty()) return;

    auto& db = DatabaseManager::instance();
    auto stats = db.getClassStatistics();

    QString html = "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<title>检测报告</title>"
        "<style>body{font-family:'Microsoft YaHei';background:#0B1120;color:#E0E0E0;padding:20px;}"
        "h1{color:#00E5FF;border-bottom:2px solid #00E5FF;padding-bottom:10px;}"
        "table{width:100%;border-collapse:collapse;margin:20px 0;}"
        "th,td{padding:10px;border:1px solid rgba(0,229,255,0.3);text-align:left;}"
        "th{background:rgba(0,229,255,0.1);color:#00E5FF;}"
        ".stat{display:inline-block;padding:15px 20px;margin:10px;background:rgba(0,229,255,0.05);border:1px solid rgba(0,229,255,0.2);border-radius:8px;}"
        ".stat-value{font-size:24px;font-weight:bold;color:#00E5FF;}"
        ".stat-label{font-size:12px;color:#8892A0;}</style></head><body>";

    html += "<h1>无人机航拍目标检测报告</h1>";
    html += QString("<p>生成时间：%1</p>").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));

    html += "<div class='stat'><div class='stat-value'>" + QString::number(db.getTotalTasks()) + "</div><div class='stat-label'>检测图片数</div></div>";
    html += "<div class='stat'><div class='stat-value'>" + QString::number(db.getTotalObjects()) + "</div><div class='stat-label'>检测目标数</div></div>";
    html += "<div class='stat'><div class='stat-value'>" + QString::number(stats.size()) + "</div><div class='stat-label'>目标类别数</div></div>";
    html += "<div class='stat'><div class='stat-value'>" + QString("%1%").arg(db.getAverageConfidence() * 100, 0, 'f', 1) + "</div><div class='stat-label'>平均置信度</div></div>";

    html += "<h2>类别统计</h2><table><tr><th>类别</th><th>数量</th><th>平均置信度</th></tr>";
    for (const auto& stat : stats) {
        html += QString("<tr><td>%1</td><td>%2</td><td>%3%</td></tr>")
            .arg(stat.className)
            .arg(stat.count)
            .arg(stat.avgConfidence * 100, 0, 'f', 1);
    }
    html += "</table></body></html>";

    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out.setEncoding(QStringConverter::Utf8);
        out << html;
        file.close();
        QMessageBox::information(this, "成功", "报告已生成到：\n" + filePath);
    } else {
        QMessageBox::warning(this, "错误", "生成报告失败");
    }
}

void MainWindow::onFilterDetectionsChanged(Qt::CheckState state)
{
    ui->imageListWidget->clear();

    for (size_t i = 0; i < m_images.size(); i++) {
        if (state == Qt::Checked && !m_images[i].detected) continue;

        QListWidgetItem* item = new QListWidgetItem();
        item->setText(m_images[i].fileName);
        item->setData(Qt::UserRole, static_cast<int>(i));
        ui->imageListWidget->addItem(item);
    }
}

void MainWindow::onImageListItemClicked(QListWidgetItem* item)
{
    int index = item->data(Qt::UserRole).toInt();
    m_userInteracting = true;
    m_autoFollowTimer->start();
    displayImage(index);
}

void MainWindow::onImageViewerClicked()
{
    if (m_currentIndex < 0 || m_currentIndex >= static_cast<int>(m_images.size())) return;

    const auto& img = m_images[m_currentIndex];
    QImage image(img.filePath);

    if (img.detected && m_showAnnotated) {
        drawDetections(image, img.detections);
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QString("图片预览 - %1").arg(img.fileName));
    dialog.setMinimumSize(800, 600);
    dialog.resize(image.width(), image.height());

    QVBoxLayout* layout = new QVBoxLayout(&dialog);

    QLabel* imageLabel = new QLabel();
    imageLabel->setAlignment(Qt::AlignCenter);
    imageLabel->setPixmap(QPixmap::fromImage(image).scaled(dialog.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));

    layout->addWidget(imageLabel);
    dialog.exec();
}

void MainWindow::onClearChat()
{
    ui->conversationView->clear();
    addAgentMessage("对话历史已清空。空中侦察分析员已就绪，请问有什么可以帮助您的？");
}

void MainWindow::onShowHistory()
{
    HistoryDialog dialog(this);
    dialog.exec();
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == ui->imageViewer && event->type() == QEvent::MouseButtonPress) {
        onImageViewerClicked();
        return true;
    }
    return QMainWindow::eventFilter(obj, event);
}
