#include "DetectionThreadPool.h"
#include "InferenceEngine.h"
#include <QElapsedTimer>
#include <QDebug>

DetectionManager::DetectionManager(InferenceEngine* engine, QObject* parent)
    : QObject(parent), m_engine(engine)
{
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &DetectionManager::processNext);
}

void DetectionManager::startDetection(const QStringList& imagePaths)
{
    if (m_running) {
        stop();
    }

    m_imagePaths = imagePaths;
    m_currentIndex = 0;
    m_completedCount = 0;
    m_totalCount = imagePaths.size();
    m_running = true;

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

    int index = m_currentIndex;
    QString imagePath = m_imagePaths[index];
    m_currentIndex++;

    QElapsedTimer timer;
    timer.start();

    std::vector<Detection> results = m_engine->detect(imagePath);
    qint64 elapsed = timer.elapsed();

    m_completedCount++;
    emit detectionComplete(index, results, elapsed);

    if (m_running && m_currentIndex < m_totalCount) {
        m_timer->start(0);
    } else {
        m_running = false;
        emit allTasksComplete();
    }
}
