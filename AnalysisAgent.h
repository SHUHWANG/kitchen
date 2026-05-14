#pragma once

#include <QObject>
#include <QString>
#include <QMap>
#include <functional>
#include <vector>
#include "Detection.h"

class MainWindow;

class AnalysisAgent : public QObject {
    Q_OBJECT

public:
    explicit AnalysisAgent(QObject* parent = nullptr);

    QString processQuery(const QString& query);
    void setCurrentDetections(const std::vector<Detection>& detections);
    void setImageList(const QStringList& imageNames);
    void setMainWindow(MainWindow* window) { m_mainWindow = window; }

private:
    void initializeHandlers();

    QString handleCountQuery(const QString& query);
    QString handleClassQuery(const QString& query);
    QString handleConfidenceQuery(const QString& query);
    QString handleCompareQuery(const QString& query);
    QString handleTimeQuery(const QString& query);
    QString handleHelpQuery(const QString& query);
    QString handleCurrentQuery(const QString& query);
    QString handleImageQuery(const QString& query);
    QString handleThresholdQuery(const QString& query);
    QString handleFilterQuery(const QString& query);

    QMap<QString, std::function<QString(const QString&)>> m_handlers;

    struct QueryPattern {
        QStringList keywords;
        std::function<QString(const QString&)> handler;
    };
    QList<QueryPattern> m_patterns;

    std::vector<Detection> m_currentDetections;
    QStringList m_imageNames;
    MainWindow* m_mainWindow = nullptr;
};
