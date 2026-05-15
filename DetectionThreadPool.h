#pragma once

#include <QObject>
#include <QTimer>
#include <QStringList>
#include <vector>
#include "Detection.h"

class InferenceEngine;

class DetectionManager : public QObject
{
    Q_OBJECT

public:
    explicit DetectionManager(InferenceEngine* engine, QObject* parent = nullptr);

    void startDetection(const QStringList& imagePaths, int startIndex = 0);
    void stop();
    bool isRunning() const { return m_running; }
    int completedCount() const { return m_completedCount; }
    int totalCount() const { return m_totalCount; }

signals:
    void detectionComplete(int index, const std::vector<Detection>& results, qint64 elapsedMs);
    void allTasksComplete();

private slots:
    void processNext();

private:
    InferenceEngine* m_engine;
    QTimer* m_timer;
    QStringList m_imagePaths;
    int m_currentIndex = 0;
    int m_startIndex = 0;  // 起始索引偏移
    int m_completedCount = 0;
    int m_totalCount = 0;
    bool m_running = false;
};
