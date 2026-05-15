#pragma warning(disable: 4996)
#include "mainwindow.h"
#include "ui_MainWindow.h"
#include "InferenceEngine.h"
#include "DatabaseManager.h"
#include "AnalysisAgent.h"
#include "LlmClient.h"
#include "LlmSettingsDialog.h"
#include "DateRangeDialog.h"
#include "HistoryDialog.h"
#include "ImagePreviewDialog.h"
#include "DetectionThreadPool.h"
#include "VideoDetectionManager.h"
#include "VideoPreviewDialog.h"
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QBarSeries>
#include <QtCharts/QBarSet>
#include <QtCharts/QBarCategoryAxis>
#include <QtCharts/QValueAxis>
#include <QtCharts/QPieSeries>
#include <QtCharts/QPieSlice>

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
#include <QClipboard>
#include <QApplication>
#include <QMenu>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setupUiStyle();
    setupConnections();

    m_engine = new InferenceEngine(this);
    m_agent = new AnalysisAgent(this);
    m_agent->setMainWindow(this);
    
    // 初始化大模型客户端
    m_llmClient = new LlmClient(this);
    
    // 从配置文件加载设置
    auto llmConfigs = LlmSettingsDialog::loadConfigs();
    qDebug() << "Loaded" << llmConfigs.size() << "LLM configs";
    if (!llmConfigs.isEmpty()) {
        qDebug() << "Default LLM:" << llmConfigs[0].name << "apiKey:" << (llmConfigs[0].apiKey.isEmpty() ? "empty" : "set");
        LlmConfig llmConfig;
        llmConfig.provider = llmConfigs[0].provider;
        llmConfig.apiKey = llmConfigs[0].apiKey;
        llmConfig.baseUrl = llmConfigs[0].baseUrl;
        llmConfig.modelName = llmConfigs[0].modelName;
        m_llmClient->setConfig(llmConfig);
    }
    m_agent->setLlmClient(m_llmClient);
    
    // 初始化大模型下拉框
    for (const auto& config : llmConfigs) {
        ui->comboLlmModel->addItem(config.name);
    }
    if (!llmConfigs.isEmpty()) {
        ui->comboLlmModel->setCurrentIndex(0);
    }

    m_autoFollowTimer = new QTimer(this);
    m_autoFollowTimer->setSingleShot(true);
    m_autoFollowTimer->setInterval(3000);
    connect(m_autoFollowTimer, &QTimer::timeout, this, &MainWindow::onAutoFollowTimeout);

    DatabaseManager::instance().initialize();

    loadModelList();

    // 设置 splitter 的 stretch factor，左右比例约 3:2
    ui->mainSplitter->setStretchFactor(0, 3);  // 左边面板占 3/5
    ui->mainSplitter->setStretchFactor(1, 2);  // 右边面板占 2/5

    // 设置右侧三个面板的初始高度比例：仪表盘:对话:快照 = 1:2:1
    ui->rightSplitter->setStretchFactor(0, 1);  // 仪表盘
    ui->rightSplitter->setStretchFactor(1, 2);  // 对话区域（默认较高）
    ui->rightSplitter->setStretchFactor(2, 1);  // 快照区域

    ui->progressBar->setVisible(false);
    ui->btnPauseDetection->setVisible(false);
    ui->btnStopDetection->setVisible(false);
    addAgentMessage("空中侦察分析员已就绪。支持查询：检测到了多少车辆？各类目标的数量统计？设置置信度阈值为0.25");
}

MainWindow::~MainWindow()
{
    if (m_detectionManager) {
        m_detectionManager->stop();
    }
    delete ui;
}

void MainWindow::setConfidenceThreshold(float threshold)
{
    m_confidenceThreshold = threshold;
    if (m_engine) {
        m_engine->setConfidenceThreshold(threshold);
    }
    ui->lblStatusInfo->setText(QString("置信度阈值已设置为 %1").arg(threshold));
}

void MainWindow::loadModelList()
{
    QStringList searchPaths = {
        QCoreApplication::applicationDirPath() + "/models/",
        QCoreApplication::applicationDirPath() + "/../models/",
        QCoreApplication::applicationDirPath() + "/../../models/",
        QCoreApplication::applicationDirPath() + "/../../../models/"
    };

    m_modelPaths.clear();
    ui->comboModel->clear();

    for (const QString& dirPath : searchPaths) {
        QDir dir(dirPath);
        if (dir.exists()) {
            QFileInfoList fileList = dir.entryInfoList(QStringList() << "*.engine", QDir::Files, QDir::Name);
            for (const QFileInfo& fi : fileList) {
                QString absolutePath = fi.absoluteFilePath();
                if (!m_modelPaths.contains(absolutePath)) {
                    m_modelPaths.append(absolutePath);
                    ui->comboModel->addItem(fi.fileName(), absolutePath);
                }
            }
        }
    }

    // 自动选择 yolo11s 模型
    int defaultIndex = -1;
    for (int i = 0; i < ui->comboModel->count(); i++) {
        QString name = ui->comboModel->itemText(i).toLower();
        if (name.contains("yolo11s") || name.contains("11s")) {
            defaultIndex = i;
            break;
        }
    }
    
    // 如果没找到 yolo11s，选择第一个模型
    if (defaultIndex < 0 && ui->comboModel->count() > 0) {
        defaultIndex = 0;
    }
    
    if (defaultIndex >= 0) {
        ui->comboModel->setCurrentIndex(defaultIndex);
        QString modelPath = ui->comboModel->itemData(defaultIndex).toString();
        if (m_engine->loadEngine(modelPath)) {
            ui->lblStatusInfo->setText(QString("模型已加载: %1").arg(ui->comboModel->currentText()));
        }
    } else {
        ui->lblStatusInfo->setText("未找到模型文件");
    }
    
    connect(ui->comboModel, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onModelChanged);
}

void MainWindow::onModelChanged(int index)
{
    if (index < 0) return;

    QString modelPath = ui->comboModel->itemData(index).toString();
    if (modelPath.isEmpty()) return;

    if (m_engine->loadEngine(modelPath)) {
        m_engine->setConfidenceThreshold(m_confidenceThreshold);
        ui->lblStatusInfo->setText(QString("模型已切换: %1").arg(ui->comboModel->currentText()));
    } else {
        ui->lblStatusInfo->setText("模型加载失败");
    }
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
    connect(ui->btnSaveDatabase, &QPushButton::clicked, this, &MainWindow::saveToDatabase);
    connect(ui->btnExportCSV, &QPushButton::clicked, this, &MainWindow::onExportCSV);
    connect(ui->btnGenerateReport, &QPushButton::clicked, this, &MainWindow::onGenerateReport);
    connect(ui->btnExportAnnotated, &QPushButton::clicked, this, &MainWindow::onExportAnnotated);
    connect(ui->btnExportVideo, &QPushButton::clicked, this, &MainWindow::onExportVideo);
    connect(ui->chkFilterDetections, &QCheckBox::checkStateChanged, this, &MainWindow::onFilterDetectionsChanged);
    connect(ui->imageListWidget, &QListWidget::itemClicked, this, &MainWindow::onImageListItemClicked);
    connect(ui->btnPrevImage, &QPushButton::clicked, this, &MainWindow::onPrevImage);
    connect(ui->btnNextImage, &QPushButton::clicked, this, &MainWindow::onNextImage);
    connect(ui->btnClearChat, &QPushButton::clicked, this, &MainWindow::onClearChat);
    connect(ui->btnHistory, &QPushButton::clicked, this, &MainWindow::onShowHistory);
    connect(ui->chkUseLlm, &QCheckBox::toggled, this, &MainWindow::onUseLlmToggled);
    connect(ui->comboLlmModel, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onLlmModelChanged);
    connect(ui->btnLlmSettings, &QPushButton::clicked, this, &MainWindow::onLlmSettingsClicked);
    
    // 连接大模型响应信号（确保在主线程执行）
    connect(m_agent, &AnalysisAgent::llmResponseReceived, this, [this](const QString& response) {
        qDebug() << "MainWindow: Received LLM response:" << response.left(50);
        addAgentMessage(response);
    }, Qt::QueuedConnection);
    connect(m_agent, &AnalysisAgent::llmErrorOccurred, this, [this](const QString& error) {
        qDebug() << "MainWindow: Received LLM error:" << error;
        addAgentMessage("大模型错误：" + error);
    }, Qt::QueuedConnection);
    connect(ui->btnImportVideo, &QPushButton::clicked, this, &MainWindow::onImportVideo);
    connect(ui->videoSlider, &QSlider::valueChanged, this, &MainWindow::onVideoSliderChanged);
    connect(ui->btnVideoPlay, &QPushButton::clicked, this, [this]() {
        if (m_videoManager) {
            if (m_videoManager->isPlaying()) {
                m_videoManager->pausePlayback();
                ui->btnVideoPlay->setText(m_videoManager->isPaused() ? "继续" : "播放");
            } else {
                m_videoManager->startPlayback();
                ui->btnVideoPlay->setText("暂停");
            }
        }
    });
    connect(ui->btnVideoStop, &QPushButton::clicked, this, [this]() {
        if (m_videoManager) {
            m_videoManager->stopPlayback();
            ui->btnVideoPlay->setText("播放");
        }
    });

    ui->imageListWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->imageListWidget, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QListWidgetItem* item = ui->imageListWidget->itemAt(pos);
        if (item) {
            QMenu menu(this);
            QAction* copyAction = menu.addAction("复制图片名");
            connect(copyAction, &QAction::triggered, this, &MainWindow::onCopyImageName);
            menu.exec(ui->imageListWidget->mapToGlobal(pos));
        }
    });

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
        #lblModel { color: #8892A0; font-size: 12px; }
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
        QComboBox { background: #1A2332; border: 1px solid rgba(0,229,255,0.3); border-radius: 4px; padding: 4px 8px; color: #E0E0E0; min-width: 100px; }
        QComboBox:hover { border-color: #00E5FF; }
        QComboBox::drop-down { border: none; }
        QComboBox QAbstractItemView { background-color: #1A2332; color: #E0E0E0; selection-background-color: rgba(0,229,255,0.3); border: 1px solid rgba(0,229,255,0.3); }
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
        #videoControlBar { background: rgba(0,229,255,0.05); border: 1px solid rgba(0,229,255,0.15); border-radius: 4px; }
        #lblVideoFrame { color: #00E5FF; font-size: 12px; }
        #videoSlider::groove:horizontal { background: #1A2332; height: 6px; border-radius: 3px; }
        #videoSlider::handle:horizontal { background: #00E5FF; width: 16px; margin: -5px 0; border-radius: 8px; }
        #videoSlider::sub-page:horizontal { background: rgba(0,229,255,0.3); border-radius: 3px; }
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
        item->setToolTip(img.fileName + "\n右键可复制文件名");
        ui->imageListWidget->addItem(item);
        imageNames.append(img.fileName);
    }

    m_agent->setImageList(imageNames);
}

void MainWindow::onCopyImageName()
{
    QListWidgetItem* item = ui->imageListWidget->currentItem();
    if (item) {
        QApplication::clipboard()->setText(item->text());
        ui->lblStatusInfo->setText(QString("已复制: %1").arg(item->text()));
    }
}

void MainWindow::displayImage(int index)
{
    if (index < 0 || index >= static_cast<int>(m_images.size())) return;

    m_currentIndex = index;
    const auto& img = m_images[index];

    ui->lblImageName->setText(img.fileName);
    ui->lblImageIndex->setText(QString("%1 / %2").arg(index + 1).arg(m_images.size()));
    ui->imageListWidget->setCurrentRow(index);

    // 获取检测结果（从内存或数据库）
    std::vector<Detection> detections;
    if (m_savedToDatabase) {
        // 从数据库读取
        detections = DatabaseManager::instance().getDetectionsByImageName(img.fileName);
    } else {
        // 从内存读取
        detections = img.detections;
    }

    m_agent->setCurrentDetections(detections);

    if (!detections.empty() && m_showAnnotated) {
        QImage annotated(img.filePath);
        drawDetections(annotated, detections);
        QPixmap pixmap = QPixmap::fromImage(annotated);
        ui->imageViewer->setPixmap(pixmap.scaled(ui->imageViewer->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));

        // 计算类别统计
        QMap<QString, int> classCounts;
        float totalConf = 0;
        for (const auto& det : detections) {
            classCounts[det.className]++;
            totalConf += det.confidence;
        }
        float avgConf = detections.empty() ? 0 : totalConf / detections.size();
        
        // 生成类别统计字符串
        QString classStats;
        for (auto it = classCounts.begin(); it != classCounts.end(); ++it) {
            if (!classStats.isEmpty()) classStats += " ";
            classStats += QString("%1:%2").arg(it.key()).arg(it.value());
        }

        ui->lblDetectionSummary->setText(QString("检测概要：检测到 %1 个目标").arg(detections.size()));
        ui->lblObjectCount->setText(QString("目标数：%1 | 平均置信度: %2%").arg(detections.size()).arg(avgConf * 100, 0, 'f', 1));
        ui->lblInferenceTime->setText(QString("推理耗时：%1ms | 类别: %2").arg(img.elapsedMs).arg(classStats));

        updateSnapshotBar(detections);
    } else {
        QPixmap pixmap(img.filePath);
        ui->imageViewer->setPixmap(pixmap.scaled(ui->imageViewer->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));

        if (m_savedToDatabase && img.detected) {
            ui->lblDetectionSummary->setText("当前显示：原始图片（已保存到数据库）");
        } else if (img.detected) {
            ui->lblDetectionSummary->setText("当前显示：原始图片");
        } else {
            ui->lblDetectionSummary->setText("检测概要：等待检测...");
        }
        ui->lblObjectCount->setText(QString("目标数：%1").arg(detections.size()));
    }
}

void MainWindow::drawDetections(QImage& image, const std::vector<Detection>& detections)
{
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);

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
    if (m_videoManager && m_videoManager->isPlaying()) {
        m_videoManager->stopPlayback();
    }

    if (m_videoManager && m_videoManager->totalFrames() > 0) {
        m_isDetecting = true;
        ui->progressBar->setVisible(true);
        ui->progressBar->setValue(0);
        ui->lblStatusInfo->setText("视频检测中...");
        ui->btnStartDetection->setEnabled(false);
        ui->btnStartDetection->setVisible(false);
        ui->btnPauseDetection->setVisible(true);
        ui->btnPauseDetection->setEnabled(true);
        ui->btnStopDetection->setVisible(true);
        ui->btnStopDetection->setEnabled(true);
        ui->btnPauseDetection->setText("暂停");
        ui->videoControlBar->setVisible(false);
        
        m_videoManager->startDetection();
        return;
    }

    if (m_images.empty()) {
        QMessageBox::information(this, "提示", "请先导入图片或视频");
        return;
    }

    if (!m_engine->isLoaded()) {
        QMessageBox::warning(this, "错误", "推理引擎未加载，请检查模型文件");
        return;
    }

    DatabaseManager::instance().clearAll();

    m_completedCount = 0;
    m_totalCount = m_images.size();
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

    if (!m_detectionManager) {
        m_detectionManager = new DetectionManager(m_engine, this);

        connect(m_detectionManager, &DetectionManager::detectionComplete, this, [this](int index, const std::vector<Detection>& results, qint64 elapsedMs) {
            if (index < 0 || index >= static_cast<int>(m_images.size())) return;

            // 只保存结果到内存，不写数据库
            m_images[index].detections = results;
            m_images[index].detected = true;
            m_images[index].elapsedMs = elapsedMs;

            m_completedCount++;
            m_lastInferenceMs = elapsedMs;

            // 每10张更新一次进度条和仪表盘
            if (m_completedCount % 10 == 0 || m_completedCount == m_totalCount) {
                updateProgressBar(m_completedCount, m_totalCount);
                updateDashboard();  // 实时更新仪表盘
            }

            // 每5张切换一次预览
            if (m_completedCount % 5 == 0) {
                displayImage(index);
                m_currentIndex = index;
            }
        });

        connect(m_detectionManager, &DetectionManager::allTasksComplete, this, [this]() {
            m_isDetecting = false;
            ui->lblStatusInfo->setText(QString("检测完成，共 %1 张图片").arg(m_totalCount));
            ui->btnStartDetection->setEnabled(true);
            ui->btnStartDetection->setVisible(true);
            ui->btnPauseDetection->setVisible(false);
            ui->btnStopDetection->setVisible(false);
            updateDashboard();
            
            // 计算统计信息
            int totalObjects = 0;
            QMap<QString, int> classCounts;
            float totalConf = 0;
            int confCount = 0;
            
            for (const auto& img : m_images) {
                totalObjects += static_cast<int>(img.detections.size());
                for (const auto& det : img.detections) {
                    classCounts[det.className]++;
                    totalConf += det.confidence;
                    confCount++;
                }
            }
            
            float avgConf = confCount > 0 ? totalConf / confCount : 0;
            
            // 生成类别统计字符串
            QString classStats;
            for (auto it = classCounts.begin(); it != classCounts.end(); ++it) {
                if (!classStats.isEmpty()) classStats += ", ";
                classStats += QString("%1:%2").arg(it.key()).arg(it.value());
            }
            
            addAgentMessage(QString("检测完成！共处理 %1 张图片，检测到 %2 个目标。\n平均置信度: %3%\n类别统计: %4\n点击\"保存到数据库\"按钮可保存结果。")
                .arg(m_totalCount)
                .arg(totalObjects)
                .arg(avgConf * 100, 0, 'f', 1)
                .arg(classStats));
        });
    }

    QStringList imagePaths;
    for (const auto& img : m_images) {
        imagePaths.append(img.filePath);
    }

    m_detectionManager->startDetection(imagePaths);
}

void MainWindow::processNextImage()
{
}

void MainWindow::onPauseDetection()
{
    if (m_videoManager && m_videoManager->isDetecting()) {
        m_videoManager->pauseDetection();
        ui->btnPauseDetection->setText(m_videoManager->isPaused() ? "继续" : "暂停");
        ui->lblStatusInfo->setText(m_videoManager->isPaused() ? "视频检测已暂停" : "视频检测中...");
        return;
    }

    if (m_videoManager && m_videoManager->isPlaying()) {
        m_videoManager->pausePlayback();
        ui->btnPauseDetection->setText(m_videoManager->isPaused() ? "继续播放" : "暂停播放");
        return;
    }

    if (!m_detectionManager) return;

    if (m_detectionManager->isRunning()) {
        m_detectionManager->stop();
        ui->btnPauseDetection->setText("继续");
        ui->lblStatusInfo->setText("已暂停");
    } else {
        QStringList imagePaths;
        int startIndex = -1;
        for (int i = 0; i < static_cast<int>(m_images.size()); i++) {
            if (!m_images[i].detected) {
                if (startIndex == -1) startIndex = i;
                imagePaths.append(m_images[i].filePath);
            }
        }
        if (!imagePaths.isEmpty() && startIndex >= 0) {
            m_detectionManager->startDetection(imagePaths, startIndex);
            ui->btnPauseDetection->setText("暂停");
            ui->lblStatusInfo->setText("检测中...");
        }
    }
}

void MainWindow::onStopDetection()
{
    if (m_videoManager && m_videoManager->isDetecting()) {
        m_videoManager->stopDetection();
        m_isDetecting = false;
        ui->btnStartDetection->setEnabled(true);
        ui->btnStartDetection->setVisible(true);
        ui->btnPauseDetection->setVisible(false);
        ui->btnStopDetection->setVisible(false);
        ui->lblStatusInfo->setText("视频检测已停止");
        ui->videoControlBar->setVisible(true);
        return;
    }

    if (m_videoManager && m_videoManager->isPlaying()) {
        m_videoManager->stopPlayback();
        ui->lblStatusInfo->setText("视频播放已停止");
        return;
    }

    if (m_detectionManager) {
        m_detectionManager->stop();
    }

    m_isDetecting = false;

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
    int totalTasks = 0;
    int totalObjects = 0;
    float avgConf = 0;
    QString topClass = "--";
    int classCount = 0;

    if (m_savedToDatabase) {
        // 从数据库读取
        auto& db = DatabaseManager::instance();
        totalTasks = db.getTotalTasks();
        totalObjects = db.getTotalObjects();
        avgConf = db.getAverageConfidence();
        topClass = db.getTopClass();
        auto stats = db.getClassStatistics();
        classCount = static_cast<int>(stats.size());
    } else {
        // 从内存计算
        float totalConf = 0;
        int confCount = 0;
        QMap<QString, int> classCounts;

        for (const auto& img : m_images) {
            if (img.detected && !img.detections.empty()) {
                totalTasks++;
                totalObjects += static_cast<int>(img.detections.size());
                for (const auto& det : img.detections) {
                    classCounts[det.className]++;
                    totalConf += det.confidence;
                    confCount++;
                }
            }
        }

        avgConf = confCount > 0 ? totalConf / confCount : 0;
        classCount = static_cast<int>(classCounts.size());

        // 找出最多的类别
        int maxCount = 0;
        for (auto it = classCounts.begin(); it != classCounts.end(); ++it) {
            if (it.value() > maxCount) {
                maxCount = it.value();
                topClass = it.key();
            }
        }
    }

    ui->lblStatTotalValue->setText(QString::number(totalTasks));
    ui->lblStatObjectsValue->setText(QString::number(totalObjects));
    ui->lblStatClassesValue->setText(QString::number(classCount));
    ui->lblStatConfValue->setText(totalObjects > 0 ? QString("%1%").arg(avgConf * 100, 0, 'f', 1) : "--");
    ui->lblTopCategory->setText(topClass != "--" ? QString("最多检测类别：%1").arg(topClass) : "最多检测类别：--");
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

    // 获取当前图片（支持图片和视频模式）
    QImage sourceImage;
    if (m_currentIndex >= 0 && m_currentIndex < static_cast<int>(m_images.size())) {
        sourceImage = QImage(m_images[m_currentIndex].filePath);
    } else if (m_videoManager) {
        sourceImage = m_videoManager->getCurrentFrame();
    }

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

        // 只在有有效图片时裁剪缩略图
        if (!sourceImage.isNull()) {
            QRectF bbox = det.bbox;
            int x = std::max(0, static_cast<int>(bbox.x()));
            int y = std::max(0, static_cast<int>(bbox.y()));
            int w = std::min(static_cast<int>(bbox.width()), sourceImage.width() - x);
            int h = std::min(static_cast<int>(bbox.height()), sourceImage.height() - y);

            if (w > 0 && h > 0) {
                QImage cropped = sourceImage.copy(x, y, w, h).scaled(60, 60, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                thumbLabel->setPixmap(QPixmap::fromImage(cropped));
            }
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
    // 停止视频播放和检测
    if (m_videoManager) {
        m_videoManager->stopPlayback();
        m_videoManager->reset();
    }

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
    
    // 隐藏视频控制栏
    ui->videoControlBar->setVisible(false);
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
    qDebug() << "addAgentMessage called with:" << message.left(50);
    QString html = QString(
        "<div style='margin-bottom: 8px;'>"
        "<span style='background-color: #1a2740; padding: 8px 14px; border-radius: 10px; color: #E0E0E0; display: inline-block; border: 1px solid rgba(0,229,255,0.2);'>"
        "%1"
        "</span></div>"
    ).arg(message.toHtmlEscaped().replace("\n", "<br>"));

    ui->conversationView->append(html);
    qDebug() << "addAgentMessage: appended to conversationView";

    QScrollBar* sb = ui->conversationView->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void MainWindow::saveToDatabase()
{
    bool hasImageResults = false;
    bool hasVideoResults = false;
    
    for (const auto& img : m_images) {
        if (img.detected) {
            hasImageResults = true;
            break;
        }
    }
    
    if (m_videoManager && m_videoManager->hasDetectionResults()) {
        hasVideoResults = true;
    }
    
    if (!hasImageResults && !hasVideoResults) {
        QMessageBox::information(this, "提示", "没有检测结果可保存");
        return;
    }

    // 显示进度条
    ui->progressBar->setVisible(true);
    ui->progressBar->setValue(0);
    ui->lblStatusInfo->setText("正在保存到数据库...");
    
    // 清空数据库
    DatabaseManager::instance().clearAll();

    int savedCount = 0;
    
    // 保存图片检测结果
    if (hasImageResults) {
        int detectedCount = 0;
        for (const auto& img : m_images) {
            if (img.detected) detectedCount++;
        }
        
        for (int i = 0; i < static_cast<int>(m_images.size()); i++) {
            const auto& img = m_images[i];
            if (!img.detected) continue;

            int taskId = DatabaseManager::instance().createTask(img.filePath, img.width, img.height);

            float totalConf = 0;
            for (const auto& det : img.detections) {
                DatabaseManager::instance().insertDetection(taskId, det);
                totalConf += det.confidence;
            }
            float avgConf = img.detections.empty() ? 0 : totalConf / img.detections.size();
            DatabaseManager::instance().updateTaskResult(taskId, static_cast<int>(img.detections.size()), avgConf, img.elapsedMs);

            savedCount++;
            ui->progressBar->setValue((savedCount * 80) / detectedCount);
            
            if (savedCount % 10 == 0) {
                QApplication::processEvents();
            }
        }
    }
    
    // 保存视频检测结果
    if (hasVideoResults) {
        ui->lblStatusInfo->setText("正在保存视频检测结果...");
        DatabaseManager::instance().saveVideoResult(
            m_videoManager->videoPath(),
            m_videoManager->totalFrames(),
            m_videoManager->getTotalObjects(),
            m_videoManager->getClassCounts()
        );
        ui->progressBar->setValue(90);
    }

    ui->progressBar->setValue(100);
    ui->lblStatusInfo->setText(QString("已保存 %1 条检测结果到数据库").arg(savedCount + (hasVideoResults ? 1 : 0)));
    
    // 标记已保存到数据库，释放内存中的检测结果
    m_savedToDatabase = true;
    for (auto& img : m_images) {
        img.detections.clear();
        img.detections.shrink_to_fit();
    }
    
    QTimer::singleShot(1000, this, [this]() {
        ui->progressBar->setVisible(false);
    });
    
    addAgentMessage(QString("已保存 %1 张检测结果到数据库并释放内存。可使用导出CSV或生成报告功能。").arg(savedCount));
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
    // 弹出时间段选择对话框
    DateRangeDialog dateDialog(this);
    if (dateDialog.exec() != QDialog::Accepted) {
        return;
    }

    QDate startDate = dateDialog.startDate();
    QDate endDate = dateDialog.endDate();

    QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation) + "/detection_report.html";
    QString filePath = QFileDialog::getSaveFileName(this, "生成报告", defaultPath, "HTML文件 (*.html)");

    if (filePath.isEmpty()) return;

    // 收集检测结果（根据时间段过滤）
    std::vector<Detection> allDetections;
    QStringList imageNames;

    // 从数据库获取历史数据
    auto& db = DatabaseManager::instance();
    auto tasks = db.getTasksByDateRange(startDate, endDate);
    for (const auto& task : tasks) {
        auto results = db.getDetectionsByTaskId(task.id);
        for (const auto& det : results) {
            allDetections.push_back(det);
        }
        imageNames.append(task.imagePath);
    }

    // 如果没有历史数据，使用当前内存中的数据
    if (allDetections.empty()) {
        for (const auto& img : m_images) {
            for (const auto& det : img.detections) {
                allDetections.push_back(det);
            }
            imageNames.append(img.fileName);
        }
    }

    if (allDetections.empty()) {
        QMessageBox::information(this, "提示", "该时间段没有检测数据可生成报告");
        return;
    }

    ui->progressBar->setValue(10);
    ui->lblStatusInfo->setText("正在收集统计数据...");

    // 收集统计数据
    QMap<QString, int> classCounts;
    QMap<QString, float> classConfSums;
    for (const auto& det : allDetections) {
        classCounts[det.className]++;
        classConfSums[det.className] += det.confidence;
    }

    int totalObjects = allDetections.size();
    int totalImages = imageNames.size();
    int totalClasses = classCounts.size();

    float totalConf = 0;
    for (const auto& det : allDetections) {
        totalConf += det.confidence;
    }
    float avgConfidence = totalObjects > 0 ? totalConf / totalObjects : 0;

    // 找出最多的类别
    QString topCategory;
    int maxCount = 0;
    for (auto it = classCounts.begin(); it != classCounts.end(); ++it) {
        if (it.value() > maxCount) {
            maxCount = it.value();
            topCategory = it.key();
        }
    }

    ui->progressBar->setValue(30);
    ui->lblStatusInfo->setText("正在调用大模型生成分析...");

    // 调用大模型生成分析文本
    QString analysisText;
    if (m_llmClient && !m_llmClient->getConfig().apiKey.isEmpty()) {
        QString prompt = QString(
            "请根据以下无人机航拍目标检测数据生成一份专业的分析报告：\n\n"
            "检测统计：\n"
            "- 总检测图片数：%1\n"
            "- 总检测目标数：%2\n"
            "- 目标类别数：%3\n"
            "- 平均置信度：%.2f%%\n\n"
            "各类别数量：\n"
        ).arg(totalImages).arg(totalObjects).arg(totalClasses).arg(avgConfidence * 100);

        for (auto it = classCounts.begin(); it != classCounts.end(); ++it) {
            float pct = totalObjects > 0 ? (float)it.value() / totalObjects * 100 : 0;
            prompt += QString("- %1: %2个 (%.1f%%)\n").arg(it.key()).arg(it.value()).arg(pct);
        }

        prompt += "\n请生成包含以下内容的分析报告（使用Markdown格式）：\n"
                  "## 检测概况总结\n"
                  "## 主要发现和特点\n"
                  "## 数据分析和趋势\n"
                  "## 建议和注意事项\n"
                  "请使用专业但易懂的中文语言，回答要完整详细。";

        analysisText = m_llmClient->chatSync(prompt);
    }

    ui->progressBar->setValue(90);
    ui->lblStatusInfo->setText("正在生成HTML报告...");

    // 生成HTML
    QString html = R"(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <title>无人机航拍目标检测报告</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: 'Microsoft YaHei', 'PingFang SC', sans-serif;
            background: linear-gradient(135deg, #0B1120 0%, #1a2740 100%);
            color: #E0E0E0;
            min-height: 100vh;
            padding: 40px;
        }
        .container {
            max-width: 1000px;
            margin: 0 auto;
            background: rgba(255,255,255,0.05);
            border-radius: 20px;
            padding: 40px;
            border: 1px solid rgba(0,229,255,0.2);
            box-shadow: 0 20px 60px rgba(0,0,0,0.3);
        }
        h1 {
            text-align: center;
            color: #00E5FF;
            font-size: 2em;
            margin-bottom: 10px;
            text-shadow: 0 0 20px rgba(0,229,255,0.5);
        }
        .subtitle {
            text-align: center;
            color: #8892A0;
            margin-bottom: 30px;
        }
        .divider {
            height: 2px;
            background: linear-gradient(90deg, transparent, #00E5FF, transparent);
            margin: 30px 0;
        }
        h2 {
            color: #00E5FF;
            font-size: 1.4em;
            margin: 30px 0 20px 0;
            padding-bottom: 10px;
            border-bottom: 1px solid rgba(0,229,255,0.3);
        }
        .stats-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
            gap: 20px;
            margin: 20px 0;
        }
        .stat-card {
            background: rgba(0,229,255,0.05);
            border: 1px solid rgba(0,229,255,0.2);
            border-radius: 12px;
            padding: 20px;
            text-align: center;
        }
        .stat-value {
            font-size: 2em;
            color: #00E5FF;
            font-weight: bold;
        }
        .stat-label {
            color: #8892A0;
            font-size: 0.9em;
            margin-top: 5px;
        }
        table {
            width: 100%;
            border-collapse: collapse;
            margin: 20px 0;
        }
        th {
            background: rgba(0,229,255,0.1);
            color: #00E5FF;
            padding: 15px;
            text-align: left;
            border-bottom: 2px solid rgba(0,229,255,0.3);
        }
        td {
            padding: 12px 15px;
            border-bottom: 1px solid rgba(255,255,255,0.1);
        }
        tr:hover {
            background: rgba(0,229,255,0.05);
        }
        .chart-container {
            text-align: center;
            margin: 30px 0;
        }
        .chart-container img {
            max-width: 100%;
            border-radius: 12px;
            border: 1px solid rgba(0,229,255,0.2);
        }
        .analysis {
            background: rgba(0,255,136,0.05);
            border: 1px solid rgba(0,255,136,0.2);
            border-radius: 12px;
            padding: 30px;
            margin: 20px 0;
            line-height: 1.8;
        }
        .analysis h3 {
            color: #00FF88;
            margin: 20px 0 10px 0;
        }
        .analysis p {
            margin-bottom: 15px;
        }
        .analysis ul {
            margin: 10px 0 10px 30px;
        }
        .analysis li {
            margin-bottom: 8px;
        }
        .footer {
            text-align: center;
            color: #666;
            margin-top: 40px;
            padding-top: 20px;
            border-top: 1px solid rgba(255,255,255,0.1);
        }
        @media print {
            body { background: white; color: #333; }
            .container { box-shadow: none; border: none; }
            h1, h2, .stat-value { color: #0066cc; }
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>无人机航拍目标检测报告</h1>
        <p class="subtitle">生成时间：REPLACE_TIME</p>

        <div class="divider"></div>

        <h2>一、检测概要</h2>
        <div class="stats-grid">
            <div class="stat-card">
                <div class="stat-value">REPLACE_IMAGES</div>
                <div class="stat-label">检测图片数</div>
            </div>
            <div class="stat-card">
                <div class="stat-value">REPLACE_OBJECTS</div>
                <div class="stat-label">检测目标数</div>
            </div>
            <div class="stat-card">
                <div class="stat-value">REPLACE_CLASSES</div>
                <div class="stat-label">目标类别数</div>
            </div>
            <div class="stat-card">
                <div class="stat-value">REPLACE_CONF%</div>
                <div class="stat-label">平均置信度</div>
            </div>
            <div class="stat-card">
                <div class="stat-value">REPLACE_TOP</div>
                <div class="stat-label">最多检测类别</div>
            </div>
        </div>

        <h2>二、各类别详细统计</h2>
        <table>
            <thead>
                <tr>
                    <th>类别</th>
                    <th>数量</th>
                    <th>占比</th>
                    <th>平均置信度</th>
                </tr>
            </thead>
            <tbody>
                REPLACE_TABLE_ROWS
            </tbody>
        </table>

        <h2>三、智能分析报告</h2>
        <div class="analysis">
            REPLACE_ANALYSIS
        </div>

        <div class="footer">
            <p>无人机航拍目标检测与智能分析系统 - 自动生成报告</p>
        </div>
    </div>
</body>
</html>
)";

    // 替换占位符
    html.replace("REPLACE_TIME", QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));
    html.replace("REPLACE_IMAGES", QString::number(totalImages));
    html.replace("REPLACE_OBJECTS", QString::number(totalObjects));
    html.replace("REPLACE_CLASSES", QString::number(totalClasses));
    html.replace("REPLACE_CONF", QString::number(avgConfidence * 100, 'f', 1));
    html.replace("REPLACE_TOP", topCategory);

    // 生成表格行
    QString tableRows;
    for (auto it = classCounts.begin(); it != classCounts.end(); ++it) {
        float pct = totalObjects > 0 ? (float)it.value() / totalObjects * 100 : 0;
        float conf = classConfSums[it.key()] / it.value() * 100;
        tableRows += QString("<tr><td>%1</td><td>%2</td><td>%3%</td><td>%4%</td></tr>")
            .arg(it.key())
            .arg(it.value())
            .arg(pct, 0, 'f', 1)
            .arg(conf, 0, 'f', 1);
    }
    html.replace("REPLACE_TABLE_ROWS", tableRows);

    // 替换分析文本（将换行转换为HTML）
    if (!analysisText.isEmpty()) {
        // 简单的Markdown转HTML
        QStringList lines = analysisText.split("\n");
        QString htmlAnalysis;
        for (const QString& line : lines) {
            QString trimmed = line.trimmed();
            if (trimmed.isEmpty()) {
                htmlAnalysis += "<br>";
            } else if (trimmed.startsWith("### ")) {
                htmlAnalysis += "<h4>" + trimmed.mid(4) + "</h4>";
            } else if (trimmed.startsWith("## ")) {
                htmlAnalysis += "<h3>" + trimmed.mid(3) + "</h3>";
            } else if (trimmed.startsWith("- ") || trimmed.startsWith("* ")) {
                htmlAnalysis += "<li>" + trimmed.mid(2) + "</li>";
            } else {
                htmlAnalysis += "<p>" + trimmed + "</p>";
            }
        }
        analysisText = htmlAnalysis;
    } else {
        analysisText = "<p>未配置大模型API，无法生成智能分析。请在设置中配置大模型API密钥后重试。</p>";
    }
    html.replace("REPLACE_ANALYSIS", analysisText);

    // 写入文件
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out.setEncoding(QStringConverter::Utf8);
        out << html;
        file.close();

        ui->progressBar->setValue(100);
        ui->lblStatusInfo->setText("报告生成完成");

        QMessageBox::information(this, "成功",
            "HTML报告已生成到：\n" + filePath + "\n\n"
            "包含：\n• 检测统计卡片\n• 详细统计表格\n• 大模型智能分析\n\n"
            "可以用浏览器打开查看，也支持打印为PDF");
    } else {
        QMessageBox::warning(this, "错误", "生成报告失败");
    }
}

void MainWindow::onExportAnnotated()
{
    if (m_images.empty()) {
        QMessageBox::information(this, "提示", "请先导入图片");
        return;
    }

    QString dir = QFileDialog::getExistingDirectory(this, "选择导出目录");
    if (dir.isEmpty()) return;

    int exported = 0;
    for (const auto& img : m_images) {
        if (!img.detected || img.detections.empty()) continue;

        QImage image(img.filePath);
        drawDetections(image, img.detections);

        QString outputPath = dir + "/" + QFileInfo(img.fileName).baseName() + "_annotated.png";
        if (image.save(outputPath)) {
            exported++;
        }
    }

    QMessageBox::information(this, "成功", QString("已导出 %1 张标注图片到：\n%2").arg(exported).arg(dir));
}

void MainWindow::onExportVideo()
{
    if (!m_videoManager || m_videoManager->isDetecting() || m_videoManager->isPlaying()) {
        QMessageBox::information(this, "提示", "请先完成视频检测");
        return;
    }

    QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation) + "/detected_video.mp4";
    QString filePath = QFileDialog::getSaveFileName(this, "导出视频", defaultPath, "视频文件 (*.mp4)");

    if (filePath.isEmpty()) return;

    ui->lblStatusInfo->setText("正在导出视频...");
    ui->progressBar->setVisible(true);
    ui->progressBar->setValue(0);

    connect(m_videoManager, &VideoDetectionManager::exportProgress, this, [this](int current, int total) {
        ui->progressBar->setValue(current * 100 / total);
    });

    connect(m_videoManager, &VideoDetectionManager::exportFinished, this, [this, filePath](bool success) {
        ui->progressBar->setVisible(false);
        if (success) {
            QMessageBox::information(this, "成功", "视频已导出到：\n" + filePath);
            ui->lblStatusInfo->setText("视频导出完成");
        } else {
            QMessageBox::warning(this, "错误", "视频导出失败");
            ui->lblStatusInfo->setText("视频导出失败");
        }
    });

    m_videoManager->exportVideo(filePath);
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
    if (m_videoManager && m_videoManager->totalFrames() > 0) {
        VideoPreviewDialog dialog(m_videoManager, this);
        dialog.exec();
        return;
    }

    if (m_currentIndex < 0 || m_currentIndex >= static_cast<int>(m_images.size())) return;

    const auto& img = m_images[m_currentIndex];
    QImage image(img.filePath);

    if (img.detected && m_showAnnotated) {
        drawDetections(image, img.detections);
    }

    ImagePreviewDialog dialog(image, QString("图片预览 - %1").arg(img.fileName), this);
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

void MainWindow::onUseLlmToggled(bool checked)
{
    // 更新大模型模式
    m_agent->setUseLlm(checked);
    
    // 更新UI状态
    ui->comboLlmModel->setEnabled(checked);
    
    // 更新标题
    if (checked) {
        ui->lblChatTitle->setText("空中侦察分析员 - 大模型模式");
        addAgentMessage("已切换到大模型模式，所有问题将由大模型处理。");
    } else {
        ui->lblChatTitle->setText("空中侦察分析员");
        addAgentMessage("已切换到规则匹配模式，简单问题将快速响应。");
    }
}

void MainWindow::onLlmModelChanged(int index)
{
    if (index < 0) return;
    
    QString modelName = ui->comboLlmModel->itemText(index);
    LlmConfig config = m_llmClient->getConfig();
    
    // 根据选择的模型更新配置
    if (modelName == "DeepSeek Chat") {
        config.provider = LlmProvider::DeepSeek;
        config.modelName = "deepseek-chat";
        config.baseUrl = "https://api.deepseek.com/v1";
    } else if (modelName == "DeepSeek Coder") {
        config.provider = LlmProvider::DeepSeek;
        config.modelName = "deepseek-coder";
        config.baseUrl = "https://api.deepseek.com/v1";
    } else if (modelName == "小米 MIMO") {
        config.provider = LlmProvider::XiaomiMimo;
        config.modelName = "mimo-7b";
        config.baseUrl = "https://api.xiaomimimo.com/v1";
    }
    
    m_llmClient->setConfig(config);
    addAgentMessage(QString("已切换到模型：%1").arg(modelName));
}

void MainWindow::onLlmSettingsClicked()
{
    LlmSettingsDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        // 重新加载配置
        auto configs = LlmSettingsDialog::loadConfigs();
        qDebug() << "Settings saved, loaded" << configs.size() << "configs";
        
        // 更新下拉框
        ui->comboLlmModel->clear();
        for (const auto& config : configs) {
            ui->comboLlmModel->addItem(config.name);
        }
        
        // 使用第一个配置（默认）更新当前大模型
        if (!configs.isEmpty()) {
            ui->comboLlmModel->setCurrentIndex(0);
            LlmConfig config;
            config.provider = configs[0].provider;
            config.apiKey = configs[0].apiKey;
            config.baseUrl = configs[0].baseUrl;
            config.modelName = configs[0].modelName;
            m_llmClient->setConfig(config);
            qDebug() << "Updated LLM config to" << configs[0].name << "apiKey:" << (config.apiKey.isEmpty() ? "empty" : "set");
            addAgentMessage("大模型配置已更新为：" + configs[0].name);
        }
    }
}

void MainWindow::onImportVideo()
{
    QString filePath = QFileDialog::getOpenFileName(
        this, "选择视频文件", "",
        "视频文件 (*.mp4 *.avi *.mov *.mkv *.wmv);;所有文件 (*)"
    );

    if (filePath.isEmpty()) return;

    if (m_videoManager) {
        m_videoManager->closeVideo();
        delete m_videoManager;
    }

    m_videoManager = new VideoDetectionManager(m_engine, this);

    connect(m_videoManager, &VideoDetectionManager::frameUpdated, this,
        [this](int frameIndex, const QImage& frame) {
            if (!frame.isNull()) {
                QPixmap pixmap = QPixmap::fromImage(frame).scaled(
                    ui->imageViewer->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
                ui->imageViewer->setPixmap(pixmap);
            }
            ui->lblImageIndex->setText(QString("帧 %1 / %2").arg(frameIndex).arg(m_videoManager->totalFrames()));
            ui->lblVideoFrame->setText(QString("帧：%1/%2").arg(frameIndex).arg(m_videoManager->totalFrames()));
            if (!ui->videoSlider->isSliderDown()) {
                ui->videoSlider->setValue(frameIndex);
            }
        });

    connect(m_videoManager, &VideoDetectionManager::detectionFrameProcessed, this,
        [this](int frameIndex, const std::vector<Detection>& detections) {
            ui->lblDetectionSummary->setText(QString("检测目标: %1").arg(detections.size()));
            ui->lblObjectCount->setText(QString("目标数: %1").arg(detections.size()));
            ui->progressBar->setValue(frameIndex * 100 / m_videoManager->totalFrames());
            
            // 每30帧更新一次快照
            if (frameIndex % 30 == 0) {
                updateSnapshotBar(detections);
            }
        });

    connect(m_videoManager, &VideoDetectionManager::detectionFinished, this, [this]() {
        m_isDetecting = false;
        ui->lblStatusInfo->setText("视频检测完成");
        ui->btnStartDetection->setEnabled(true);
        ui->btnStartDetection->setVisible(true);
        ui->btnPauseDetection->setVisible(false);
        ui->btnStopDetection->setVisible(false);
        ui->videoControlBar->setVisible(true);
        ui->btnExportVideo->setVisible(true);
        ui->videoSlider->setRange(0, m_videoManager->totalFrames() - 1);
        
        // 更新仪表盘（视频检测）
        int totalObjects = m_videoManager->getTotalObjects();
        QMap<QString, int> classCounts = m_videoManager->getClassCounts();
        
        QString topClass = "--";
        int maxCount = 0;
        for (auto it = classCounts.begin(); it != classCounts.end(); ++it) {
            if (it.value() > maxCount) {
                maxCount = it.value();
                topClass = it.key();
            }
        }
        
        ui->lblStatTotalValue->setText(QString::number(m_videoManager->totalFrames()));
        ui->lblStatObjectsValue->setText(QString::number(totalObjects));
        ui->lblStatClassesValue->setText(QString::number(classCounts.size()));
        ui->lblTopCategory->setText(maxCount > 0 ? QString("最多检测类别：%1").arg(topClass) : "最多检测类别：--");
        
        addAgentMessage(QString("视频检测完成！共检测 %1 帧，检测到 %2 个目标。\n点击\"保存到数据库\"按钮可保存结果。")
            .arg(m_videoManager->totalFrames())
            .arg(totalObjects));
    });

    connect(m_videoManager, &VideoDetectionManager::playbackFinished, this, [this]() {
        ui->lblStatusInfo->setText("视频播放完成");
    });

    if (m_videoManager->openVideo(filePath)) {
        ui->lblImageName->setText(QFileInfo(filePath).fileName());
        ui->lblStatusInfo->setText(QString("视频已加载: %1帧, %2fps - 点击开始检测")
            .arg(m_videoManager->totalFrames())
            .arg(m_videoManager->fps(), 0, 'f', 1));
        
        ui->videoControlBar->setVisible(true);
        ui->videoSlider->setRange(0, m_videoManager->totalFrames() - 1);
        ui->videoSlider->setValue(0);
        ui->lblVideoFrame->setText(QString("帧：0/%1").arg(m_videoManager->totalFrames()));
        ui->progressBar->setVisible(false);
        ui->btnExportVideo->setVisible(false);
    } else {
        QMessageBox::warning(this, "错误", "无法打开视频文件");
    }
}

void MainWindow::onVideoPreview()
{
    if (!m_videoManager) {
        QMessageBox::information(this, "提示", "请先导入视频");
        return;
    }
    
    VideoPreviewDialog dialog(m_videoManager, this);
    dialog.exec();
}

void MainWindow::onVideoFrameUpdate()
{
}

void MainWindow::onVideoSliderChanged(int value)
{
    if (!m_videoManager) return;

    if (m_videoManager->isDetecting() || m_videoManager->isPlaying()) return;

    m_videoManager->seekTo(value);
    
    QImage frame = m_videoManager->getCurrentFrame();
    if (!frame.isNull()) {
        QPixmap pixmap = QPixmap::fromImage(frame).scaled(
            ui->imageViewer->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        ui->imageViewer->setPixmap(pixmap);
    }
    
    auto detections = m_videoManager->getCurrentDetections();
    ui->lblImageIndex->setText(QString("帧 %1 / %2").arg(value).arg(m_videoManager->totalFrames()));
    ui->lblDetectionSummary->setText(QString("检测目标: %1").arg(detections.size()));
    ui->lblObjectCount->setText(QString("目标数: %1").arg(detections.size()));
    ui->lblVideoFrame->setText(QString("帧：%1/%2").arg(value).arg(m_videoManager->totalFrames()));
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == ui->imageViewer && event->type() == QEvent::MouseButtonPress) {
        onImageViewerClicked();
        return true;
    }
    return QMainWindow::eventFilter(obj, event);
}
