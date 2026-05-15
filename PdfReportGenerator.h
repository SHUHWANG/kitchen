#pragma once

#include <QObject>
#include <QString>
#include <QImage>
#include <QMap>
#include <vector>
#include "Detection.h"

class LlmClient;

struct ReportData {
    int totalImages = 0;
    int totalObjects = 0;
    int totalClasses = 0;
    float avgConfidence = 0.0f;
    QMap<QString, int> classCounts;
    QMap<QString, float> classAvgConfidence;
    QString topCategory;
    QString generationTime;
};

class PdfReportGenerator : public QObject
{
    Q_OBJECT

public:
    explicit PdfReportGenerator(QObject* parent = nullptr);
    ~PdfReportGenerator();

    void setLlmClient(LlmClient* client) { m_llmClient = client; }

    // 生成完整报告
    void generateReport(const QString& outputPath,
                       const std::vector<Detection>& allDetections,
                       const QStringList& imageNames);

signals:
    void progressUpdated(const QString& status, int percent);
    void reportGenerated(const QString& filePath);
    void errorOccurred(const QString& error);

private:
    // 收集统计数据
    ReportData collectStatistics(const std::vector<Detection>& detections,
                                const QStringList& imageNames);

    // 生成图表
    QImage generateBarChart(const QMap<QString, int>& data, const QString& title);
    QImage generatePieChart(const QMap<QString, int>& data, const QString& title);
    QImage generateConfidenceChart(const QMap<QString, float>& data);

    // 调用大模型生成分析文本
    void generateAnalysisText(const ReportData& data, 
                             std::function<void(const QString&)> callback);

    // 生成PDF
    void createPdf(const QString& outputPath,
                  const ReportData& data,
                  const QImage& barChart,
                  const QImage& pieChart,
                  const QImage& confidenceChart,
                  const QString& analysisText);

    // 辅助函数
    QString formatDateTime();

private:
    LlmClient* m_llmClient = nullptr;
};
