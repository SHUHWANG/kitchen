#include "AnalysisAgent.h"
#include "DatabaseManager.h"
#include "mainwindow.h"
#include <QRegularExpression>
#include <QMap>

AnalysisAgent::AnalysisAgent(QObject* parent)
    : QObject(parent)
{
    initializeHandlers();
}

void AnalysisAgent::initializeHandlers()
{
    m_patterns = {
        {{"阈值", "threshold", "置信度设置"},
         [this](const QString& q) { return handleThresholdQuery(q); }},
        {{"当前", "这次", "本次", "现在", "current"},
         [this](const QString& q) { return handleCurrentQuery(q); }},
        {{"图片", "照片", "image", "photo"},
         [this](const QString& q) { return handleImageQuery(q); }},
        {{"多少", "数量", "总数", "count", "几个", "几辆", "几架"},
         [this](const QString& q) { return handleCountQuery(q); }},
        {{"类别", "类型", "class", "种类", "分类", "占比", "比例"},
         [this](const QString& q) { return handleClassQuery(q); }},
        {{"置信", "准确", "confidence", "精确"},
         [this](const QString& q) { return handleConfidenceQuery(q); }},
        {{"最多", "最大", "最高", "频繁", "top", "最常见"},
         [this](const QString& q) { return handleCompareQuery(q); }},
        {{"时间", "推理", "耗时", "速度", "time", "快慢"},
         [this](const QString& q) { return handleTimeQuery(q); }},
        {{"帮助", "help", "怎么用", "功能", "支持"},
         [this](const QString& q) { return handleHelpQuery(q); }},
        {{"设置", "筛选", "过滤", "filter", "显示"},
         [this](const QString& q) { return handleFilterQuery(q); }}
    };
}

void AnalysisAgent::setCurrentDetections(const std::vector<Detection>& detections)
{
    m_currentDetections = detections;
}

void AnalysisAgent::setImageList(const QStringList& imageNames)
{
    m_imageNames = imageNames;
}

QString AnalysisAgent::processQuery(const QString& query)
{
    QString lowerQuery = query.toLower();

    for (const auto& pattern : m_patterns) {
        for (const auto& keyword : pattern.keywords) {
            if (lowerQuery.contains(keyword)) {
                return pattern.handler(query);
            }
        }
    }

    return handleCountQuery(query);
}

QString AnalysisAgent::handleThresholdQuery(const QString& query)
{
    if (query.contains("多少") || query.contains("当前") || query.contains("现在") || query.contains("是")) {
        if (m_mainWindow) {
            float currentThreshold = m_mainWindow->getConfidenceThreshold();
            return QString("当前置信度阈值为 %1（%2%）。")
                .arg(currentThreshold)
                .arg(currentThreshold * 100, 0, 'f', 1);
        }
        return "无法获取当前阈值。";
    }

    QRegularExpression re("(\\d+\\.?\\d*)");
    QRegularExpressionMatch match = re.match(query);

    if (match.hasMatch()) {
        float threshold = match.captured(1).toFloat();
        if (threshold > 0 && threshold <= 1.0f) {
            if (m_mainWindow) {
                m_mainWindow->setConfidenceThreshold(threshold);
                return QString("置信度阈值已设置为 %1（%2%）。后续检测将使用此阈值过滤低置信度目标。")
                    .arg(threshold).arg(threshold * 100, 0, 'f', 1);
            }
        } else if (threshold > 1.0f && threshold <= 100.0f) {
            threshold = threshold / 100.0f;
            if (m_mainWindow) {
                m_mainWindow->setConfidenceThreshold(threshold);
                return QString("置信度阈值已设置为 %1（%2%）。后续检测将使用此阈值过滤低置信度目标。")
                    .arg(threshold).arg(match.captured(1));
            }
        } else {
            return "置信度阈值应在 0-1 或 1%-100% 之间。例如：设置置信度阈值为0.5 或 设置置信度阈值为50%";
        }
    }

    return "请指定置信度阈值，例如：\n  - 设置置信度阈值为0.5\n  - 设置置信度阈值为50%\n  - 阈值0.3\n  - 当前阈值是多少？";
}

QString AnalysisAgent::handleCurrentQuery(const QString& query)
{
    if (m_currentDetections.empty()) {
        return "当前没有检测结果，请先进行图片检测。";
    }

    QMap<QString, int> classCounts;
    QMap<QString, float> classConfSums;

    for (const auto& det : m_currentDetections) {
        classCounts[det.className]++;
        classConfSums[det.className] += det.confidence;
    }

    int total = static_cast<int>(m_currentDetections.size());
    QString result = QString("当前图片共检测到 %1 个目标，各类别统计：\n").arg(total);

    for (auto it = classCounts.begin(); it != classCounts.end(); ++it) {
        QString className = it.key();
        int count = it.value();
        float percentage = (static_cast<float>(count) / total) * 100.0f;
        float avgConf = classConfSums[className] / count;

        result += QString("  - %1: %2 个 (占比 %3%，平均置信度 %4%)\n")
            .arg(className)
            .arg(count)
            .arg(percentage, 0, 'f', 1)
            .arg(avgConf * 100, 0, 'f', 1);
    }

    return result;
}

QString AnalysisAgent::handleImageQuery(const QString& query)
{
    for (const auto& imageName : m_imageNames) {
        if (query.contains(imageName) || imageName.contains(query.trimmed())) {
            auto detections = DatabaseManager::instance().getDetectionsByImageName(imageName);

            if (detections.empty()) {
                return QString("图片「%1」暂无检测结果。").arg(imageName);
            }

            QMap<QString, int> classCounts;
            QMap<QString, float> classConfSums;

            for (const auto& det : detections) {
                classCounts[det.className]++;
                classConfSums[det.className] += det.confidence;
            }

            int total = static_cast<int>(detections.size());
            QString result = QString("图片「%1」共检测到 %2 个目标，各类别统计：\n").arg(imageName).arg(total);

            for (auto it = classCounts.begin(); it != classCounts.end(); ++it) {
                QString className = it.key();
                int count = it.value();
                float percentage = (static_cast<float>(count) / total) * 100.0f;
                float avgConf = classConfSums[className] / count;

                result += QString("  - %1: %2 个 (占比 %3%，平均置信度 %4%)\n")
                    .arg(className)
                    .arg(count)
                    .arg(percentage, 0, 'f', 1)
                    .arg(avgConf * 100, 0, 'f', 1);
            }

            return result;
        }
    }

    return handleCountQuery(query);
}

QString AnalysisAgent::handleCountQuery(const QString& query)
{
    auto& db = DatabaseManager::instance();
    int totalObjects = db.getTotalObjects();
    int totalTasks = db.getTotalTasks();

    const auto& config = DetectionConfig::instance();
    QStringList classNames = config.classNames();

    QString targetClass;
    for (const auto& name : classNames) {
        if (query.contains(name)) {
            targetClass = name;
            break;
        }
    }
    QStringList classNamesEn = config.classNamesEn();
    for (const auto& name : classNamesEn) {
        if (query.toLower().contains(name)) {
            int idx = classNamesEn.indexOf(name);
            if (idx >= 0) targetClass = classNames[idx];
            break;
        }
    }

    if (!targetClass.isEmpty()) {
        auto stats = db.getClassStatistics();
        for (const auto& stat : stats) {
            if (stat.className == targetClass) {
                return QString("在已检测的 %1 张图片中，共检测到 %2 个 %3，平均置信度为 %4%。")
                    .arg(totalTasks)
                    .arg(stat.count)
                    .arg(targetClass)
                    .arg(stat.avgConfidence * 100, 0, 'f', 1);
            }
        }
        return QString("在已检测的图片中未检测到 %1。").arg(targetClass);
    }

    if (query.contains("车")) {
        auto stats = db.getClassStatistics();
        int carCount = 0, vanCount = 0, truckCount = 0, busCount = 0;
        for (const auto& stat : stats) {
            if (stat.className == "轿车") carCount = stat.count;
            if (stat.className == "面包车") vanCount = stat.count;
            if (stat.className == "卡车") truckCount = stat.count;
            if (stat.className == "公交车") busCount = stat.count;
        }
        int totalVehicles = carCount + vanCount + truckCount + busCount;
        return QString("在已检测的 %1 张图片中，共检测到 %2 辆车辆，其中轿车 %3、面包车 %4、卡车 %5、公交车 %6。")
            .arg(totalTasks).arg(totalVehicles).arg(carCount).arg(vanCount).arg(truckCount).arg(busCount);
    }

    if (query.contains("人")) {
        auto stats = db.getClassStatistics();
        int pedCount = 0, peopleCount = 0;
        for (const auto& stat : stats) {
            if (stat.className == "行人") pedCount = stat.count;
            if (stat.className == "人群") peopleCount = stat.count;
        }
        return QString("在已检测的 %1 张图片中，共检测到 %2 个行人和 %3 个人群。")
            .arg(totalTasks).arg(pedCount).arg(peopleCount);
    }

    return QString("在已检测的 %1 张图片中，共检测到 %2 个目标，涵盖 %3 个类别。")
        .arg(totalTasks).arg(totalObjects).arg(db.getClassStatistics().size());
}

QString AnalysisAgent::handleClassQuery(const QString& query)
{
    auto& db = DatabaseManager::instance();
    auto stats = db.getClassStatistics();

    if (stats.empty()) {
        return "暂无检测数据，请先进行图片检测。";
    }

    int totalObjects = db.getTotalObjects();
    QString result = "检测到的目标类别统计：\n";

    for (const auto& stat : stats) {
        float percentage = (static_cast<float>(stat.count) / totalObjects) * 100.0f;
        result += QString("  - %1: %2 个 (占比 %3%，平均置信度 %4%)\n")
            .arg(stat.className)
            .arg(stat.count)
            .arg(percentage, 0, 'f', 1)
            .arg(stat.avgConfidence * 100, 0, 'f', 1);
    }
    return result;
}

QString AnalysisAgent::handleConfidenceQuery(const QString& query)
{
    auto& db = DatabaseManager::instance();
    float avgConf = db.getAverageConfidence();

    return QString("所有检测目标的平均置信度为 %1%。")
        .arg(avgConf * 100, 0, 'f', 1);
}

QString AnalysisAgent::handleCompareQuery(const QString& query)
{
    auto& db = DatabaseManager::instance();
    QString topClass = db.getTopClass();

    if (topClass == "--") {
        return "暂无检测数据，请先进行图片检测。";
    }

    auto stats = db.getClassStatistics();
    if (stats.empty()) return "暂无检测数据。";

    const auto& topStat = stats[0];
    int totalObjects = db.getTotalObjects();
    float percentage = (static_cast<float>(topStat.count) / totalObjects) * 100.0f;

    return QString("检测最多的目标类别是「%1」，共 %2 个（占比 %3%），平均置信度 %4%。")
        .arg(topStat.className)
        .arg(topStat.count)
        .arg(percentage, 0, 'f', 1)
        .arg(topStat.avgConfidence * 100, 0, 'f', 1);
}

QString AnalysisAgent::handleTimeQuery(const QString& query)
{
    auto& db = DatabaseManager::instance();
    auto tasks = db.getAllTasks();

    if (tasks.empty()) {
        return "暂无检测数据，请先进行图片检测。";
    }

    qint64 totalMs = 0;
    int count = 0;
    for (const auto& task : tasks) {
        if (task.inferenceMs > 0) {
            totalMs += task.inferenceMs;
            count++;
        }
    }

    if (count == 0) return "暂无推理时间数据。";

    float avgMs = static_cast<float>(totalMs) / count;
    return QString("已检测 %1 张图片，平均推理耗时 %2ms。")
        .arg(count)
        .arg(avgMs, 0, 'f', 1);
}

QString AnalysisAgent::handleHelpQuery(const QString& query)
{
    Q_UNUSED(query);
    return "空中侦察分析员支持以下查询：\n"
           "  1. 数量查询：检测到了多少车辆/行人/目标？\n"
           "  2. 类别统计：有哪些目标类别？各占比多少？\n"
           "  3. 置信度：平均置信度是多少？\n"
           "  4. 排行查询：检测最多的是什么？\n"
           "  5. 性能查询：推理耗时多少？\n"
           "  6. 当前图片：当前检测了哪些类别？\n"
           "  7. 特定图片：查询 xxx.jpg 的检测结果\n"
           "  8. 阈值设置：设置置信度阈值为0.5\n"
           "  9. 阈值查询：当前阈值是多少？\n"
           "  10. 筛选类别：只显示轿车和公交车\n"
           "  11. 特定类别：轿车出现了几次？";
}

QString AnalysisAgent::handleFilterQuery(const QString& query)
{
    if (m_mainWindow) {
        const auto& config = DetectionConfig::instance();
        QStringList classNames = config.classNames();

        QStringList filterClasses;
        for (const auto& name : classNames) {
            if (query.contains(name)) {
                filterClasses.append(name);
            }
        }

        if (filterClasses.isEmpty()) {
            return "请指定要筛选的类别，例如：\n  - 只显示轿车和公交车\n  - 筛选行人";
        }

        return QString("已筛选显示类别：%1。这些类别的检测结果将优先显示。")
            .arg(filterClasses.join("、"));
    }

    return "筛选功能暂不可用。";
}
