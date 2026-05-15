#include "DetectionThreadPool.h"
#include "InferenceEngine.h"
#include <QElapsedTimer>
#include <QDebug>
#include <QCoreApplication>

DetectionManager::DetectionManager(InferenceEngine* engine, QObject* parent)
    : QObject(parent), m_engine(engine)
{
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &DetectionManager::processNext);
}

void DetectionManager::startDetection(const QStringList& imagePaths, int startIndex)
{
    if (m_running) {
        stop();
    }

    m_imagePaths = imagePaths;
    m_currentIndex = 0;
    m_startIndex = startIndex;  // 记录起始偏移
    m_completedCount = 0;
    m_totalCount = imagePaths.size();
    m_running = true;

    // 立即开始，不需要延迟
    m_timer->start(0);
}

void DetectionManager::stop()
{
    m_timer->stop();
    m_running = false;
}

void DetectionManager::processNext()
{
    if (!m_running || m_currentIndex >= m_totalCount) {
        m_running = false;
        emit allTasksComplete();
        return;
    }

    int localIndex = m_currentIndex;
    int realIndex = m_currentIndex + m_startIndex;
    QString imagePath = m_imagePaths[localIndex];
    m_currentIndex++;

    QElapsedTimer timer;
    timer.start();

    std::vector<Detection> results = m_engine->detect(imagePath);
    qint64 elapsed = timer.elapsed();

    m_completedCount++;
    emit detectionComplete(realIndex, results, elapsed);

    if (m_running && m_currentIndex < m_totalCount) {
        m_timer->start(1);  // 1ms延迟，降低CPU占用
    } else {
        m_running = false;
        emit allTasksComplete();
    }
}
