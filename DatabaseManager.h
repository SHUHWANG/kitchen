#pragma once

#include <QObject>
#include <QString>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDate>
#include <vector>
#include "Detection.h"

class DatabaseManager : public QObject {
    Q_OBJECT

public:
    static DatabaseManager& instance() {
        static DatabaseManager inst;
        return inst;
    }

    bool initialize(const QString& dbPath = "detection_results.db");
    void close();
    void clearAll();

    int createTask(const QString& imagePath, int imageWidth, int imageHeight);
    bool updateTaskResult(int taskId, int objectCount, float avgConfidence, qint64 inferenceMs);
    bool insertDetection(int taskId, const Detection& det);
    bool deleteTask(int taskId);
    bool saveVideoResult(const QString& videoPath, int totalFrames, int totalObjects, const QMap<QString, int>& classCounts);

    struct TaskInfo {
        int id;
        QString imagePath;
        int imageWidth;
        int imageHeight;
        int objectCount;
        float avgConfidence;
        qint64 inferenceMs;
        QString createdAt;
    };

    struct ObjectStat {
        QString className;
        int count;
        float avgConfidence;
    };

    std::vector<TaskInfo> getAllTasks();
    std::vector<TaskInfo> getTasksByDate(const QDate& date);
    std::vector<TaskInfo> getTasksByDateRange(const QDate& startDate, const QDate& endDate);
    std::vector<Detection> getDetectionsByTaskId(int taskId);
    std::vector<Detection> getDetectionsByImageName(const QString& imageName);

    struct VideoResultInfo {
        int id;
        QString videoPath;
        int totalFrames;
        int totalObjects;
        QString classCounts;
        QString createdAt;
    };

    std::vector<VideoResultInfo> getVideoResultsByDateRange(const QDate& startDate, const QDate& endDate);
    std::vector<ObjectStat> getClassStatistics();
    std::vector<ObjectStat> getClassStatisticsByDate(const QDate& date);
    int getTotalObjects();
    int getTotalObjectsByDate(const QDate& date);
    int getTotalTasks();
    int getTotalTasksByDate(const QDate& date);
    float getAverageConfidence();
    float getAverageConfidenceByDate(const QDate& date);
    QString getTopClass();
    QString getTopClassByDate(const QDate& date);

    bool exportToCSV(const QString& filePath);

private:
    DatabaseManager() = default;
    ~DatabaseManager();
    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    bool createTables();

    QSqlDatabase m_db;
    bool m_initialized = false;
};
