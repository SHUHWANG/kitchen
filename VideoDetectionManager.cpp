#include "VideoDetectionManager.h"
#include "InferenceEngine.h"
#include <QElapsedTimer>
#include <QDebug>
#include <QPainter>
#include <QPen>
#include <QFont>
#include <opencv2/opencv.hpp>

VideoDetectionManager::VideoDetectionManager(InferenceEngine* engine, QObject* parent)
    : QObject(parent), m_engine(engine)
{
    m_playbackTimer = new QTimer(this);
    m_playbackTimer->setSingleShot(true);
    connect(m_playbackTimer, &QTimer::timeout, this, &VideoDetectionManager::onPlaybackTimer);

    m_detectionTimer = new QTimer(this);
    m_detectionTimer->setSingleShot(true);
    connect(m_detectionTimer, &QTimer::timeout, this, &VideoDetectionManager::onDetectionTimer);
}

VideoDetectionManager::~VideoDetectionManager()
{
    closeVideo();
}

bool VideoDetectionManager::openVideo(const QString& videoPath)
{
    closeVideo();

    m_capture = new cv::VideoCapture(videoPath.toStdString());
    if (!m_capture->isOpened()) {
        emit videoError("无法打开视频文件");
        delete m_capture;
        m_capture = nullptr;
        return false;
    }

    m_videoPath = videoPath;
    m_totalFrames = static_cast<int>(m_capture->get(cv::CAP_PROP_FRAME_COUNT));
    m_fps = m_capture->get(cv::CAP_PROP_FPS);
    m_frameWidth = static_cast<int>(m_capture->get(cv::CAP_PROP_FRAME_WIDTH));
    m_frameHeight = static_cast<int>(m_capture->get(cv::CAP_PROP_FRAME_HEIGHT));
    m_currentFrame = 0;
    m_allResults.clear();
    m_allResults.resize(m_totalFrames);
    m_lastDetections.clear();
    m_lastFrame = QImage();

    if (m_fps <= 0) m_fps = 30.0;

    QImage firstFrame = readFrame(0);
    if (!firstFrame.isNull()) {
        m_lastFrame = firstFrame;
        emit frameUpdated(0, firstFrame);
    }

    return true;
}

void VideoDetectionManager::closeVideo()
{
    m_playbackTimer->stop();
    m_detectionTimer->stop();
    m_playing = false;
    m_detecting = false;
    m_paused = false;

    if (m_capture) {
        m_capture->release();
        delete m_capture;
        m_capture = nullptr;
    }
}

void VideoDetectionManager::startPlayback()
{
    if (!m_capture || !m_capture->isOpened()) return;
    
    m_capture->set(cv::CAP_PROP_POS_FRAMES, m_currentFrame);
    m_playing = true;
    m_paused = false;
    m_playbackTimer->start(static_cast<int>(1000.0 / m_fps));
}

void VideoDetectionManager::pausePlayback()
{
    m_paused = !m_paused;
    if (!m_paused && m_playing) {
        m_playbackTimer->start(0);
    }
}

void VideoDetectionManager::stopPlayback()
{
    m_playbackTimer->stop();
    m_playing = false;
    m_paused = false;
}

void VideoDetectionManager::seekTo(int frameIndex)
{
    if (!m_capture || !m_capture->isOpened()) return;

    frameIndex = std::max(0, std::min(frameIndex, m_totalFrames - 1));
    m_currentFrame = frameIndex;
    m_capture->set(cv::CAP_PROP_POS_FRAMES, frameIndex);

    if (frameIndex < static_cast<int>(m_allResults.size()) && !m_allResults[frameIndex].frameImage.isNull()) {
        emit frameUpdated(frameIndex, m_allResults[frameIndex].frameImage);
    } else {
        QImage frame = readFrame(frameIndex);
        if (!frame.isNull()) {
            if (frameIndex < static_cast<int>(m_allResults.size()) && !m_allResults[frameIndex].detections.empty()) {
                drawDetections(frame, m_allResults[frameIndex].detections);
            }
            emit frameUpdated(frameIndex, frame);
        }
    }
}

void VideoDetectionManager::startDetection()
{
    if (!m_capture || !m_capture->isOpened()) {
        emit videoError("视频未打开");
        return;
    }

    m_detecting = true;
    m_currentFrame = 0;
    m_detectionTimer->start(0);
}

void VideoDetectionManager::pauseDetection()
{
    m_paused = !m_paused;
    if (!m_paused && m_detecting) {
        m_detectionTimer->start(0);
    }
}

void VideoDetectionManager::stopDetection()
{
    m_detectionTimer->stop();
    m_detecting = false;
    m_paused = false;
}

QImage VideoDetectionManager::getCurrentFrame() const
{
    if (m_currentFrame >= 0 && m_currentFrame < static_cast<int>(m_allResults.size())) {
        if (!m_allResults[m_currentFrame].frameImage.isNull()) {
            return m_allResults[m_currentFrame].frameImage;
        }
    }
    return m_lastFrame;
}

std::vector<Detection> VideoDetectionManager::getCurrentDetections() const
{
    if (m_currentFrame >= 0 && m_currentFrame < static_cast<int>(m_allResults.size())) {
        return m_allResults[m_currentFrame].detections;
    }
    return m_lastDetections;
}

void VideoDetectionManager::onPlaybackTimer()
{
    if (!m_playing || m_paused || !m_capture || !m_capture->isOpened()) return;

    if (m_currentFrame >= m_totalFrames) {
        m_playing = false;
        emit playbackFinished();
        return;
    }

    QImage frame;
    if (m_currentFrame < static_cast<int>(m_allResults.size()) && !m_allResults[m_currentFrame].frameImage.isNull()) {
        frame = m_allResults[m_currentFrame].frameImage;
    } else {
        cv::Mat cvFrame;
        if (m_capture->read(cvFrame) && !cvFrame.empty()) {
            QImage image(cvFrame.data, cvFrame.cols, cvFrame.rows, cvFrame.step, QImage::Format_BGR888);
            frame = image.copy();
        }
    }

    if (!frame.isNull()) {
        m_lastFrame = frame;
        emit frameUpdated(m_currentFrame, frame);
    }

    m_currentFrame++;

    if (m_playing && !m_paused) {
        m_playbackTimer->start(static_cast<int>(1000.0 / m_fps));
    }
}

void VideoDetectionManager::onDetectionTimer()
{
    if (!m_detecting || m_paused || !m_capture || !m_capture->isOpened()) return;

    if (m_currentFrame >= m_totalFrames) {
        m_detecting = false;
        emit detectionFinished();
        return;
    }

    QImage frame = readFrame(m_currentFrame);
    if (frame.isNull()) {
        m_detecting = false;
        emit detectionFinished();
        return;
    }

    int frameIndex = m_currentFrame;
    m_currentFrame++;

    bool shouldDetect = (frameIndex % 3 == 0);
    std::vector<Detection> detections;

    if (shouldDetect) {
        detections = m_engine->detect(frame);
        m_lastDetections = detections;
    } else {
        detections = m_lastDetections;
    }

    QImage annotatedFrame = frame.copy();
    drawDetections(annotatedFrame, detections);
    saveFrameResult(frameIndex, detections, annotatedFrame);
    m_lastFrame = annotatedFrame;

    emit frameUpdated(frameIndex, annotatedFrame);
    emit detectionFrameProcessed(frameIndex, detections);

    if (m_detecting && !m_paused) {
        m_detectionTimer->start(0);
    }
}

QImage VideoDetectionManager::readFrame(int frameIndex)
{
    if (!m_capture || !m_capture->isOpened()) return QImage();

    m_capture->set(cv::CAP_PROP_POS_FRAMES, frameIndex);
    cv::Mat frame;
    if (!m_capture->read(frame) || frame.empty()) return QImage();

    QImage image(frame.data, frame.cols, frame.rows, frame.step, QImage::Format_BGR888);
    return image.copy();
}

void VideoDetectionManager::saveFrameResult(int frameIndex, const std::vector<Detection>& detections, const QImage& frame)
{
    if (frameIndex >= 0 && frameIndex < static_cast<int>(m_allResults.size())) {
        VideoDetectionResult result;
        result.frameIndex = frameIndex;
        result.timestamp = static_cast<qint64>(frameIndex * 1000.0 / m_fps);
        result.detections = detections;
        result.frameImage = frame;
        m_allResults[frameIndex] = result;
    }
}

void VideoDetectionManager::drawDetections(QImage& image, const std::vector<Detection>& detections)
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

int VideoDetectionManager::getTotalObjects() const
{
    int total = 0;
    for (const auto& result : m_allResults) {
        total += static_cast<int>(result.detections.size());
    }
    return total;
}

QMap<QString, int> VideoDetectionManager::getClassCounts() const
{
    QMap<QString, int> counts;
    for (const auto& result : m_allResults) {
        for (const auto& det : result.detections) {
            counts[det.className]++;
        }
    }
    return counts;
}

bool VideoDetectionManager::exportVideo(const QString& outputPath)
{
    if (m_allResults.empty()) {
        emit exportFinished(false);
        return false;
    }

    cv::VideoWriter writer;
    int fourcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');
    cv::Size size(m_frameWidth, m_frameHeight);
    writer.open(outputPath.toStdString(), fourcc, m_fps, size);

    if (!writer.isOpened()) {
        emit exportFinished(false);
        return false;
    }

    int total = static_cast<int>(m_allResults.size());
    for (int i = 0; i < total; i++) {
        const QImage& frame = m_allResults[i].frameImage;
        if (frame.isNull()) continue;

        QImage rgbFrame = frame.convertToFormat(QImage::Format_RGB888);
        cv::Mat mat(rgbFrame.height(), rgbFrame.width(), CV_8UC3,
                    const_cast<uchar*>(rgbFrame.bits()), rgbFrame.bytesPerLine());
        cv::Mat bgrMat;
        cv::cvtColor(mat, bgrMat, cv::COLOR_RGB2BGR);
        writer.write(bgrMat);

        emit exportProgress(i + 1, total);
    }

    writer.release();
    emit exportFinished(true);
    return true;
}
