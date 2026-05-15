#pragma once

#include <QObject>
#include <QString>
#include <QMap>
#include <functional>
#include <vector>
#include "Detection.h"

class MainWindow;
class LlmClient;

class AnalysisAgent : public QObject {
    Q_OBJECT

public:
    explicit AnalysisAgent(QObject* parent = nullptr);

    QString processQuery(const QString& query);
    void setCurrentDetections(const std::vector<Detection>& detections);
    void setImageList(const QStringList& imageNames);
    void setMainWindow(MainWindow* window) { m_mainWindow = window; }
    
    // 大模型相关方法
    void setLlmClient(LlmClient* llmClient) { m_llmClient = llmClient; }
    bool isLlmEnabled() const { return m_llmClient != nullptr; }
    void setUseLlm(bool use) { m_useLlm = use; }
    bool isUseLlm() const { return m_useLlm; }

signals:
    void llmResponseReceived(const QString& response);
    void llmErrorOccurred(const QString& error);

private:
    void initializeHandlers();

    // 规则匹配处理方法（快速响应）
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
    
    // 大模型处理方法
    QString buildContextForLlm() const;
    void processWithLlm(const QString& query);
    QString getLlmSystemPrompt() const;
    void handleLlmResponse(const QString& response, bool success, const QString& error);

    QMap<QString, std::function<QString(const QString&)>> m_handlers;

    struct QueryPattern {
        QStringList keywords;
        std::function<QString(const QString&)> handler;
    };
    QList<QueryPattern> m_patterns;

    std::vector<Detection> m_currentDetections;
    QStringList m_imageNames;
    MainWindow* m_mainWindow = nullptr;
    LlmClient* m_llmClient = nullptr;
    bool m_useLlm = false;  // 是否使用大模型模式
};
