#pragma once

#include <QObject>
#include <QTimer>
#include <QString>
#include <QImage>
#include <QMap>
#include <vector>
#include "Detection.h"

namespace cv {
    class VideoCapture;
}

class InferenceEngine;

struct VideoDetectionResult {
    int frameIndex;
    qint64 timestamp;
    std::vector<Detection> detections;
    QImage frameImage;
};

class VideoDetectionManager : public QObject
{
    Q_OBJECT

public:
    explicit VideoDetectionManager(InferenceEngine* engine, QObject* parent = nullptr);
    ~VideoDetectionManager();

    bool openVideo(const QString& videoPath);
    void closeVideo();

    void startPlayback();
    void pausePlayback();
    void stopPlayback();
    void seekTo(int frameIndex);

    void startDetection();
    void pauseDetection();
    void stopDetection();

    bool isPlaying() const { return m_playing; }
    bool isDetecting() const { return m_detecting; }
    bool isPaused() const { return m_paused; }
    int currentFrame() const { return m_currentFrame; }
    int totalFrames() const { return m_totalFrames; }
    double fps() const { return m_fps; }
    QString videoPath() const { return m_videoPath; }

    QImage getCurrentFrame() const;
    std::vector<Detection> getCurrentDetections() const;
    std::vector<VideoDetectionResult> getAllResults() const { return m_allResults; }

    int getTotalObjects() const;
    QMap<QString, int> getClassCounts() const;

    bool exportVideo(const QString& outputPath);
    bool hasDetectionResults() const { return !m_allResults.empty(); }

signals:
    void frameUpdated(int frameIndex, const QImage& frame);
    void detectionFrameProcessed(int frameIndex, const std::vector<Detection>& detections);
    void playbackFinished();
    void detectionFinished();
    void videoError(const QString& message);
    void exportProgress(int current, int total);
    void exportFinished(bool success);

private slots:
    void onPlaybackTimer();
    void onDetectionTimer();

private:
    void drawDetections(QImage& image, const std::vector<Detection>& detections);
    void saveFrameResult(int frameIndex, const std::vector<Detection>& detections, const QImage& frame);
    QImage readFrame(int frameIndex);

    InferenceEngine* m_engine;
    QTimer* m_playbackTimer;
    QTimer* m_detectionTimer;

    QString m_videoPath;
    cv::VideoCapture* m_capture = nullptr;

    int m_currentFrame = 0;
    int m_totalFrames = 0;
    double m_fps = 30.0;
    int m_frameWidth = 0;
    int m_frameHeight = 0;

    bool m_playing = false;
    bool m_detecting = false;
    bool m_paused = false;

    std::vector<VideoDetectionResult> m_allResults;
    std::vector<Detection> m_lastDetections;
    QImage m_lastFrame;
};
