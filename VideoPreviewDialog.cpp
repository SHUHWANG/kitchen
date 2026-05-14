#include "VideoPreviewDialog.h"
#include "VideoDetectionManager.h"
#include <QScrollBar>

VideoPreviewDialog::VideoPreviewDialog(VideoDetectionManager* manager, QWidget *parent)
    : QDialog(parent), m_manager(manager)
{
    setWindowTitle("视频预览 - 检测结果");
    setMinimumSize(1024, 768);
    resize(1280, 900);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    m_videoLabel = new QLabel();
    m_videoLabel->setAlignment(Qt::AlignCenter);
    m_videoLabel->setMinimumSize(640, 480);
    m_videoLabel->setStyleSheet("background-color: #0A1018; border: 1px solid rgba(0,229,255,0.2);");
    mainLayout->addWidget(m_videoLabel, 1);

    m_infoLabel = new QLabel("准备就绪");
    m_infoLabel->setStyleSheet("color: #8892A0; padding: 4px;");
    mainLayout->addWidget(m_infoLabel);

    QWidget* controlBar = new QWidget();
    QHBoxLayout* controlLayout = new QHBoxLayout(controlBar);
    controlLayout->setContentsMargins(8, 4, 8, 4);

    m_playPauseBtn = new QPushButton("播放");
    m_playPauseBtn->setFixedWidth(60);
    connect(m_playPauseBtn, &QPushButton::clicked, this, &VideoPreviewDialog::onPlayPause);
    controlLayout->addWidget(m_playPauseBtn);

    m_stopBtn = new QPushButton("停止");
    m_stopBtn->setFixedWidth(60);
    connect(m_stopBtn, &QPushButton::clicked, this, &VideoPreviewDialog::onStop);
    controlLayout->addWidget(m_stopBtn);

    m_slider = new QSlider(Qt::Horizontal);
    m_slider->setRange(0, m_manager->totalFrames() - 1);
    connect(m_slider, &QSlider::sliderPressed, this, &VideoPreviewDialog::onSliderPressed);
    connect(m_slider, &QSlider::sliderReleased, this, &VideoPreviewDialog::onSliderReleased);
    connect(m_slider, &QSlider::valueChanged, this, &VideoPreviewDialog::onSliderValueChanged);
    controlLayout->addWidget(m_slider, 1);

    m_frameLabel = new QLabel("0 / 0");
    m_frameLabel->setMinimumWidth(80);
    m_frameLabel->setAlignment(Qt::AlignCenter);
    controlLayout->addWidget(m_frameLabel);

    QPushButton* closeBtn = new QPushButton("关闭");
    closeBtn->setFixedWidth(60);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    controlLayout->addWidget(closeBtn);

    mainLayout->addWidget(controlBar);

    connect(m_manager, &VideoDetectionManager::frameUpdated, this, &VideoPreviewDialog::onFrameUpdated);
    connect(m_manager, &VideoDetectionManager::detectionFinished, this, &VideoPreviewDialog::onVideoFinished);
    connect(m_manager, &VideoDetectionManager::playbackFinished, this, &VideoPreviewDialog::onVideoFinished);

    QString styleSheet = R"(
        QDialog { background-color: #0B1120; }
        QLabel { color: #E0E0E0; }
        QPushButton { background: transparent; border: 1px solid #00E5FF; border-radius: 4px; color: #00E5FF; padding: 6px 12px; }
        QPushButton:hover { background: rgba(0,229,255,0.12); }
        QSlider::groove:horizontal { background: #1A2332; height: 6px; border-radius: 3px; }
        QSlider::handle:horizontal { background: #00E5FF; width: 16px; margin: -5px 0; border-radius: 8px; }
        QSlider::sub-page:horizontal { background: rgba(0,229,255,0.3); border-radius: 3px; }
    )";
    setStyleSheet(styleSheet);

    updateFrame();
}

void VideoPreviewDialog::onPlayPause()
{
    if (m_manager->isPlaying()) {
        m_manager->pausePlayback();
        m_playPauseBtn->setText(m_manager->isPaused() ? "播放" : "暂停");
    } else if (m_manager->isDetecting()) {
        m_manager->pauseDetection();
        m_playPauseBtn->setText(m_manager->isPaused() ? "播放" : "暂停");
    } else {
        m_manager->startPlayback();
        m_playPauseBtn->setText("暂停");
    }
}

void VideoPreviewDialog::onStop()
{
    if (m_manager->isPlaying()) {
        m_manager->stopPlayback();
    } else if (m_manager->isDetecting()) {
        m_manager->stopDetection();
    }
    m_playPauseBtn->setText("播放");
}

void VideoPreviewDialog::onSliderPressed()
{
    m_sliderDragging = true;
    m_wasPlaying = m_manager->isPlaying();
    if (m_wasPlaying) {
        m_manager->pausePlayback();
    }
}

void VideoPreviewDialog::onSliderReleased()
{
    m_sliderDragging = false;
    m_manager->seekTo(m_slider->value());
    updateFrame();
    if (m_wasPlaying) {
        m_manager->startPlayback();
        m_playPauseBtn->setText("暂停");
    }
}

void VideoPreviewDialog::onSliderValueChanged(int value)
{
    if (m_sliderDragging) {
        m_frameLabel->setText(QString("%1 / %2").arg(value).arg(m_manager->totalFrames()));
    }
}

void VideoPreviewDialog::onFrameUpdated(int frameIndex, const QImage& frame)
{
    if (!m_sliderDragging) {
        m_slider->setValue(frameIndex);
    }
    updateFrame();
}

void VideoPreviewDialog::onVideoFinished()
{
    m_playPauseBtn->setText("播放");
    m_infoLabel->setText("视频播放完成");
}

void VideoPreviewDialog::updateFrame()
{
    QImage frame = m_manager->getCurrentFrame();
    if (!frame.isNull()) {
        QPixmap pixmap = QPixmap::fromImage(frame).scaled(m_videoLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        m_videoLabel->setPixmap(pixmap);
    }

    int current = m_manager->currentFrame();
    int total = m_manager->totalFrames();
    m_frameLabel->setText(QString("%1 / %2").arg(current).arg(total));

    auto detections = m_manager->getCurrentDetections();
    m_infoLabel->setText(QString("帧 %1 | 检测目标: %2").arg(current).arg(detections.size()));
}
