#pragma once

#include <QMainWindow>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QListWidgetItem>
#include <QEvent>
#include <QTimer>
#include <vector>
#include "Detection.h"

namespace Ui {
class MainWindow;
}

class InferenceEngine;
class AnalysisAgent;
class DetectionManager;
class VideoDetectionManager;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void setConfidenceThreshold(float threshold);
    float getConfidenceThreshold() const { return m_confidenceThreshold; }

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void onImportImages();
    void onImportFolder();
    void onStartDetection();
    void onPauseDetection();
    void onStopDetection();
    void onResetDetection();
    void onToggleViewMode();
    void onSendMessage();
    void saveToDatabase();
    void onExportCSV();
    void onGenerateReport();
    void onExportAnnotated();
    void onExportVideo();
    void onModelChanged(int index);
    void onFilterDetectionsChanged(Qt::CheckState state);
    void onImageListItemClicked(QListWidgetItem* item);
    void onImageViewerClicked();
    void onPrevImage();
    void onNextImage();
    void onAutoFollowTimeout();
    void onClearChat();
    void onShowHistory();
    void onCopyImageName();
    void onImportVideo();
    void onVideoPreview();
    void onVideoFrameUpdate();
    void onVideoSliderChanged(int value);

    void onDetectionComplete(int index, const std::vector<Detection>& results, qint64 elapsedMs);
    void onDetectionError(int index, const QString& message);

private:
    void setupUiStyle();
    void setupConnections();
    void loadModelList();
    void loadImageList();
    void displayImage(int index);
    void drawDetections(QImage& image, const std::vector<Detection>& detections);
    void updateDashboard();
    void updateProgressBar(int current, int total);
    void updateSnapshotBar(const std::vector<Detection>& detections);
    void addAgentMessage(const QString& message);
    void processNextImage();

    Ui::MainWindow *ui;

    InferenceEngine* m_engine = nullptr;
    AnalysisAgent* m_agent = nullptr;
    DetectionManager* m_detectionManager = nullptr;
    VideoDetectionManager* m_videoManager = nullptr;

    std::vector<ImageInfo> m_images;
    int m_currentIndex = -1;
    bool m_showAnnotated = true;
    int m_completedCount = 0;
    int m_totalCount = 0;
    qint64 m_lastInferenceMs = 0;

    bool m_isPaused = false;
    bool m_isDetecting = false;
    bool m_userInteracting = false;
    QTimer* m_autoFollowTimer = nullptr;
    int m_nextDetectIndex = 0;

    QStringList m_modelPaths;
    float m_confidenceThreshold = 0.25f;
    bool m_savedToDatabase = false;  // 标记是否已保存到数据库
};
