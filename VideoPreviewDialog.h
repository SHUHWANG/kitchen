#pragma once

#include <QDialog>
#include <QLabel>
#include <QSlider>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QImage>

class VideoDetectionManager;

class VideoPreviewDialog : public QDialog
{
    Q_OBJECT

public:
    explicit VideoPreviewDialog(VideoDetectionManager* manager, QWidget *parent = nullptr);

private slots:
    void onPlayPause();
    void onStop();
    void onSliderPressed();
    void onSliderReleased();
    void onSliderValueChanged(int value);
    void onFrameUpdated(int frameIndex, const QImage& frame);
    void onVideoFinished();

private:
    void updateFrame();

    VideoDetectionManager* m_manager;
    QLabel* m_videoLabel;
    QLabel* m_infoLabel;
    QSlider* m_slider;
    QPushButton* m_playPauseBtn;
    QPushButton* m_stopBtn;
    QLabel* m_frameLabel;
    bool m_sliderDragging = false;
    bool m_wasPlaying = false;
};
