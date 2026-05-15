#include "PdfReportGenerator.h"
#include "LlmClient.h"
#include <QPdfWriter>
#include <QPainter>
#include <QPageSize>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QBarSeries>
#include <QtCharts/QBarSet>
#include <QtCharts/QBarCategoryAxis>
#include <QtCharts/QValueAxis>
#include <QtCharts/QPieSeries>
#include <QtCharts/QPieSlice>
#include <QVBoxLayout>
#include <QBuffer>
#include <QDateTime>
#include <QStandardPaths>
#include <QDir>
#include <QApplication>
#include <QTextDocument>

PdfReportGenerator::PdfReportGenerator(QObject* parent)
    : QObject(parent)
{
}

PdfReportGenerator::~PdfReportGenerator()
{
}

void PdfReportGenerator::generateReport(const QString& outputPath,
                                       const std::vector<Detection>& allDetections,
                                       const QStringList& imageNames)
{
    emit progressUpdated("正在收集统计数据...", 10);

    // 收集统计数据
    ReportData data = collectStatistics(allDetections, imageNames);

    emit progressUpdated("正在生成柱状图...", 25);
    QImage barChart = generateBarChart(data.classCounts, "各类别目标检测数量");

    emit progressUpdated("正在生成饼图...", 40);
    QImage pieChart = generatePieChart(data.classCounts, "目标类别占比");

    emit progressUpdated("正在生成置信度图表...", 55);
    QImage confidenceChart = generateConfidenceChart(data.classAvgConfidence);

    // 如果有大模型，生成分析文本
    if (m_llmClient && m_llmClient->getConfig().apiKey.isEmpty() == false) {
        emit progressUpdated("正在调用大模型生成分析...", 70);

        // 同步调用大模型
        QString analysisText;
        QString prompt = QString(
            "请根据以下无人机航拍目标检测数据生成一份专业的分析报告：\n\n"
            "检测统计：\n"
            "- 总检测图片数：%1\n"
            "- 总检测目标数：%2\n"
            "- 目标类别数：%3\n"
            "- 平均置信度：%.2f%%\n\n"
            "各类别数量：\n"
        ).arg(data.totalImages).arg(data.totalObjects)
         .arg(data.totalClasses).arg(data.avgConfidence * 100);

        for (auto it = data.classCounts.begin(); it != data.classCounts.end(); ++it) {
            float pct = data.totalObjects > 0 ? (float)it.value() / data.totalObjects * 100 : 0;
            prompt += QString("- %1: %2个 (%.1f%%)\n").arg(it.key()).arg(it.value()).arg(pct);
        }

        prompt += "\n请生成包含以下内容的分析报告：\n"
                  "1. 检测概况总结\n"
                  "2. 主要发现和特点\n"
                  "3. 数据分析和趋势\n"
                  "4. 建议和注意事项\n"
                  "请使用专业但易懂的中文语言。";

        analysisText = m_llmClient->chatSync(prompt);

        emit progressUpdated("正在生成PDF文档...", 85);
        createPdf(outputPath, data, barChart, pieChart, confidenceChart, analysisText);
    } else {
        emit progressUpdated("正在生成PDF文档...", 85);
        createPdf(outputPath, data, barChart, pieChart, confidenceChart, "");
    }

    emit progressUpdated("报告生成完成！", 100);
    emit reportGenerated(outputPath);
}

ReportData PdfReportGenerator::collectStatistics(const std::vector<Detection>& detections,
                                                const QStringList& imageNames)
{
    ReportData data;
    data.totalImages = imageNames.size();
    data.totalObjects = detections.size();
    data.generationTime = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");

    QMap<QString, int> classConfSums;
    QMap<QString, int> classConfCounts;

    for (const auto& det : detections) {
        data.classCounts[det.className]++;
        classConfSums[det.className] += (int)(det.confidence * 1000);
        classConfCounts[det.className]++;
    }

    data.totalClasses = data.classCounts.size();

    // 计算平均置信度
    float totalConf = 0;
    for (const auto& det : detections) {
        totalConf += det.confidence;
    }
    data.avgConfidence = detections.empty() ? 0 : totalConf / detections.size();

    // 计算各类别平均置信度
    for (auto it = classConfSums.begin(); it != classConfSums.end(); ++it) {
        QString className = it.key();
        if (classConfCounts[className] > 0) {
            data.classAvgConfidence[className] = (float)it.value() / classConfCounts[className] / 1000.0f;
        }
    }

    // 找出最多的类别
    int maxCount = 0;
    for (auto it = data.classCounts.begin(); it != data.classCounts.end(); ++it) {
        if (it.value() > maxCount) {
            maxCount = it.value();
            data.topCategory = it.key();
        }
    }

    return data;
}

QImage PdfReportGenerator::generateBarChart(const QMap<QString, int>& data, const QString& title)
{
    int width = 700, height = 450;
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(Qt::white);

    // 使用堆分配，让Qt父子关系管理生命周期
    QChart* chart = new QChart();
    chart->setTitle(title);
    chart->setAnimationOptions(QChart::SeriesAnimations);

    QBarSeries* series = new QBarSeries();
    QBarSet* set = new QBarSet("检测数量");
    set->setColor(QColor("#00E5FF"));

    QStringList categories;
    for (auto it = data.begin(); it != data.end(); ++it) {
        *set << it.value();
        categories << it.key();
    }
    series->append(set);
    chart->addSeries(series);

    QBarCategoryAxis* axisX = new QBarCategoryAxis();
    axisX->append(categories);
    chart->addAxis(axisX, Qt::AlignBottom);
    series->attachAxis(axisX);

    QValueAxis* axisY = new QValueAxis();
    axisY->setTitleText("数量");
    chart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisY);

    chart->legend()->setVisible(true);
    chart->legend()->setAlignment(Qt::AlignBottom);

    // QChartView会接管chart的所有权
    QChartView* chartView = new QChartView(chart);
    chartView->setRenderHint(QPainter::Antialiasing);
    chartView->resize(width, height);

    QPainter painter(&image);
    painter.drawPixmap(0, 0, chartView->grab());
    painter.end();

    // 删除chartView会级联删除chart、series、set等所有子对象
    delete chartView;

    return image;
}

QImage PdfReportGenerator::generatePieChart(const QMap<QString, int>& data, const QString& title)
{
    int width = 700, height = 450;
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(Qt::white);

    QChart* chart = new QChart();
    chart->setTitle(title);
    chart->setAnimationOptions(QChart::SeriesAnimations);

    QPieSeries* series = new QPieSeries();

    QStringList colors = {"#00E5FF", "#00FF88", "#FFB000", "#FF6B6B", "#9C27B0",
                          "#2196F3", "#4CAF50", "#FF9800", "#E91E63", "#607D8B"};
    int colorIdx = 0;

    for (auto it = data.begin(); it != data.end(); ++it) {
        QPieSlice* slice = series->append(it.key(), it.value());
        slice->setColor(QColor(colors[colorIdx % colors.size()]));
        slice->setLabelVisible(true);
        slice->setLabelColor(Qt::white);
        colorIdx++;
    }

    chart->addSeries(series);
    chart->legend()->setVisible(true);
    chart->legend()->setAlignment(Qt::AlignRight);

    QChartView* chartView = new QChartView(chart);
    chartView->setRenderHint(QPainter::Antialiasing);
    chartView->resize(width, height);

    QPainter painter(&image);
    painter.drawPixmap(0, 0, chartView->grab());
    painter.end();

    delete chartView;

    return image;
}

QImage PdfReportGenerator::generateConfidenceChart(const QMap<QString, float>& data)
{
    int width = 700, height = 400;
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(Qt::white);

    QChart* chart = new QChart();
    chart->setTitle("各类别平均置信度");
    chart->setAnimationOptions(QChart::SeriesAnimations);

    QBarSeries* series = new QBarSeries();
    QBarSet* set = new QBarSet("置信度");
    set->setColor(QColor("#00FF88"));

    QStringList categories;
    for (auto it = data.begin(); it != data.end(); ++it) {
        *set << it.value() * 100;
        categories << it.key();
    }
    series->append(set);
    chart->addSeries(series);

    QBarCategoryAxis* axisX = new QBarCategoryAxis();
    axisX->append(categories);
    chart->addAxis(axisX, Qt::AlignBottom);
    series->attachAxis(axisX);

    QValueAxis* axisY = new QValueAxis();
    axisY->setTitleText("置信度 (%)");
    axisY->setRange(0, 100);
    chart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisY);

    chart->legend()->setVisible(true);
    chart->legend()->setAlignment(Qt::AlignBottom);

    QChartView* chartView = new QChartView(chart);
    chartView->setRenderHint(QPainter::Antialiasing);
    chartView->resize(width, height);

    QPainter painter(&image);
    painter.drawPixmap(0, 0, chartView->grab());
    painter.end();

    delete chartView;

    return image;
}

void PdfReportGenerator::createPdf(const QString& outputPath,
                                  const ReportData& data,
                                  const QImage& barChart,
                                  const QImage& pieChart,
                                  const QImage& confidenceChart,
                                  const QString& analysisText)
{
    QPdfWriter writer(outputPath);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setResolution(96);

    QPainter painter(&writer);

    // 页面参数（使用逻辑坐标）
    int pageWidth = painter.device()->width();
    int pageHeight = painter.device()->height();
    int margin = pageWidth / 20;  // 5% 边距
    int contentWidth = pageWidth - 2 * margin;
    int y = margin;
    int lineHeight = pageHeight / 40;  // 行高

    // 标题
    QFont titleFont("Microsoft YaHei", 16, QFont::Bold);
    painter.setFont(titleFont);
    painter.setPen(QColor("#00E5FF"));
    painter.drawText(QRect(margin, y, contentWidth, lineHeight * 2),
                    Qt::AlignCenter, "无人机航拍目标检测报告");
    y += lineHeight * 3;

    // 生成时间
    QFont timeFont("Microsoft YaHei", 8);
    painter.setFont(timeFont);
    painter.setPen(QColor("#888888"));
    painter.drawText(QRect(margin, y, contentWidth, lineHeight),
                    Qt::AlignCenter, "生成时间：" + data.generationTime);
    y += lineHeight * 2;

    // 分隔线
    painter.setPen(QPen(QColor("#00E5FF"), 2));
    painter.drawLine(margin, y, pageWidth - margin, y);
    y += lineHeight;

    // 统计概要
    QFont sectionFont("Microsoft YaHei", 12, QFont::Bold);
    painter.setFont(sectionFont);
    painter.setPen(QColor("#333333"));
    painter.drawText(QRect(margin, y, contentWidth, lineHeight),
                    Qt::AlignLeft, "一、检测概要");
    y += lineHeight * 2;

    QFont bodyFont("Microsoft YaHei", 9);
    painter.setFont(bodyFont);
    painter.setPen(QColor("#555555"));

    QStringList summaryItems;
    summaryItems << QString("• 总检测图片数：%1 张").arg(data.totalImages)
                 << QString("• 总检测目标数：%1 个").arg(data.totalObjects)
                 << QString("• 目标类别数：%1 类").arg(data.totalClasses)
                 << QString("• 平均置信度：%1%").arg(data.avgConfidence * 100, 0, 'f', 1)
                 << QString("• 最多检测类别：%1").arg(data.topCategory);

    for (const QString& item : summaryItems) {
        painter.drawText(QRect(margin, y, contentWidth, lineHeight),
                        Qt::AlignLeft, item);
        y += lineHeight;
    }

    y += lineHeight;

    // 各类别详细统计
    painter.setFont(sectionFont);
    painter.setPen(QColor("#333333"));
    painter.drawText(QRect(margin, y, contentWidth, lineHeight),
                    Qt::AlignLeft, "二、各类别详细统计");
    y += lineHeight * 2;

    // 表格
    QFont tableFont("Microsoft YaHei", 8);
    painter.setFont(tableFont);

    // 表头
    int col1 = margin;
    int col2 = margin + contentWidth / 4;
    int col3 = margin + contentWidth / 2;
    int col4 = margin + contentWidth * 3 / 4;
    int colWidth = contentWidth / 4;

    painter.setPen(QColor("#00E5FF"));
    painter.drawText(QRect(col1, y, colWidth, lineHeight), Qt::AlignCenter, "类别");
    painter.drawText(QRect(col2, y, colWidth, lineHeight), Qt::AlignCenter, "数量");
    painter.drawText(QRect(col3, y, colWidth, lineHeight), Qt::AlignCenter, "占比");
    painter.drawText(QRect(col4, y, colWidth, lineHeight), Qt::AlignCenter, "平均置信度");
    y += lineHeight;

    painter.setPen(QPen(QColor("#CCCCCC"), 1));
    painter.drawLine(margin, y, pageWidth - margin, y);
    y += 5;

    // 表格内容
    painter.setPen(QColor("#555555"));
    for (auto it = data.classCounts.begin(); it != data.classCounts.end(); ++it) {
        QString className = it.key();
        int count = it.value();
        float pct = data.totalObjects > 0 ? (float)count / data.totalObjects * 100 : 0;
        float conf = data.classAvgConfidence.value(className, 0) * 100;

        painter.drawText(QRect(col1, y, colWidth, lineHeight), Qt::AlignCenter, className);
        painter.drawText(QRect(col2, y, colWidth, lineHeight), Qt::AlignCenter, QString::number(count));
        painter.drawText(QRect(col3, y, colWidth, lineHeight), Qt::AlignCenter,
                        QString("%1%").arg(pct, 0, 'f', 1));
        painter.drawText(QRect(col4, y, colWidth, lineHeight), Qt::AlignCenter,
                        QString("%1%").arg(conf, 0, 'f', 1));
        y += lineHeight;
    }

    y += lineHeight;

    // 检查是否需要换页
    if (y > pageHeight - margin * 2) {
        writer.newPage();
        y = margin;
    }

    // 图表部分
    painter.setFont(sectionFont);
    painter.setPen(QColor("#333333"));
    painter.drawText(QRect(margin, y, contentWidth, lineHeight),
                    Qt::AlignLeft, "三、数据可视化");
    y += lineHeight * 2;

    // 柱状图
    int chartWidth = contentWidth;
    int chartHeight = chartWidth * 450 / 700;

    // 检查是否需要换页
    if (y + chartHeight > pageHeight - margin) {
        writer.newPage();
        y = margin;
    }

    painter.drawImage(QRect(margin, y, chartWidth, chartHeight), barChart);
    y += chartHeight + lineHeight;

    // 检查是否需要换页
    if (y + chartHeight > pageHeight - margin) {
        writer.newPage();
        y = margin;
    }

    // 饼图
    painter.drawImage(QRect(margin, y, chartWidth, chartHeight), pieChart);
    y += chartHeight + lineHeight;

    // 检查是否需要换页
    if (y + chartHeight > pageHeight - margin) {
        writer.newPage();
        y = margin;
    }

    // 置信度图
    int confChartHeight = chartWidth * 400 / 700;
    painter.drawImage(QRect(margin, y, chartWidth, confChartHeight), confidenceChart);
    y += confChartHeight + lineHeight;

    // 大模型分析
    if (!analysisText.isEmpty()) {
        // 检查是否需要换页
        if (y > pageHeight - margin * 3) {
            writer.newPage();
            y = margin;
        }

        painter.setFont(sectionFont);
        painter.setPen(QColor("#333333"));
        painter.drawText(QRect(margin, y, contentWidth, lineHeight),
                        Qt::AlignLeft, "四、智能分析报告");
        y += lineHeight * 2;

        painter.setFont(bodyFont);
        painter.setPen(QColor("#555555"));

        // 简单分行显示
        QStringList lines = analysisText.split("\n");
        for (const QString& line : lines) {
            if (line.trimmed().isEmpty()) {
                y += lineHeight / 2;
                continue;
            }

            // 检查是否需要换页
            if (y > pageHeight - margin) {
                writer.newPage();
                y = margin;
            }

            // 使用QTextDocument来自动换行
            QTextDocument doc;
            doc.setDefaultFont(bodyFont);
            doc.setTextWidth(contentWidth);
            doc.setPlainText(line);

            QRectF rect(margin, y, contentWidth, pageHeight - y - margin);
            painter.save();
            painter.translate(rect.topLeft());
            doc.drawContents(&painter, QRectF(0, 0, rect.width(), rect.height()));
            painter.restore();

            y += (int)doc.size().height() + 5;
        }
    }

    // 页脚
    if (y > pageHeight - margin) {
        writer.newPage();
        y = margin;
    }

    painter.setFont(QFont("Microsoft YaHei", 7));
    painter.setPen(QColor("#999999"));
    painter.drawText(QRect(margin, pageHeight - margin, contentWidth, lineHeight / 2),
                    Qt::AlignCenter, "无人机航拍目标检测与智能分析系统 - 自动生成报告");

    painter.end();
}

QString PdfReportGenerator::formatDateTime()
{
    return QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
}
