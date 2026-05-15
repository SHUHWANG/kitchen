#include "DatabaseManager.h"
#include <QSqlRecord>
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <QDebug>

DatabaseManager::~DatabaseManager()
{
    close();
}

bool DatabaseManager::initialize(const QString& dbPath)
{
    if (m_initialized) return true;

    m_db = QSqlDatabase::addDatabase("QSQLITE");
    m_db.setDatabaseName(dbPath);

    if (!m_db.open()) {
        qDebug() << "Failed to open database:" << m_db.lastError().text();
        return false;
    }

    if (!createTables()) {
        return false;
    }

    m_initialized = true;
    qDebug() << "Database initialized:" << dbPath;
    return true;
}

void DatabaseManager::close()
{
    if (m_db.isOpen()) {
        m_db.close();
    }
    m_initialized = false;
}

void DatabaseManager::clearAll()
{
    if (!m_initialized) return;

    QSqlQuery query(m_db);
    query.exec("DELETE FROM detection_objects");
    query.exec("DELETE FROM detection_tasks");
    query.exec("DELETE FROM sqlite_sequence WHERE name='detection_objects'");
    query.exec("DELETE FROM sqlite_sequence WHERE name='detection_tasks'");
    qDebug() << "Database cleared";
}

bool DatabaseManager::createTables()
{
    QSqlQuery query(m_db);

    QString createTasks = R"(
        CREATE TABLE IF NOT EXISTS detection_tasks (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            image_path TEXT NOT NULL,
            image_width INTEGER,
            image_height INTEGER,
            object_count INTEGER DEFAULT 0,
            avg_confidence REAL DEFAULT 0.0,
            inference_ms INTEGER DEFAULT 0,
            created_at TEXT DEFAULT (datetime('now','localtime'))
        )
    )";

    if (!query.exec(createTasks)) {
        qDebug() << "Failed to create detection_tasks table:" << query.lastError().text();
        return false;
    }

    QString createObjects = R"(
        CREATE TABLE IF NOT EXISTS detection_objects (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            task_id INTEGER NOT NULL,
            class_id INTEGER,
            class_name TEXT,
            confidence REAL,
            bbox_x REAL,
            bbox_y REAL,
            bbox_w REAL,
            bbox_h REAL,
            created_at TEXT DEFAULT (datetime('now','localtime')),
            FOREIGN KEY (task_id) REFERENCES detection_tasks(id)
        )
    )";

    if (!query.exec(createObjects)) {
        qDebug() << "Failed to create detection_objects table:" << query.lastError().text();
        return false;
    }

    QString createVideoResults = R"(
        CREATE TABLE IF NOT EXISTS video_results (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            video_path TEXT NOT NULL,
            total_frames INTEGER,
            total_objects INTEGER,
            class_counts TEXT,
            created_at TEXT DEFAULT (datetime('now','localtime'))
        )
    )";

    if (!query.exec(createVideoResults)) {
        qDebug() << "Failed to create video_results table:" << query.lastError().text();
        return false;
    }

    return true;
}

int DatabaseManager::createTask(const QString& imagePath, int imageWidth, int imageHeight)
{
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO detection_tasks (image_path, image_width, image_height) VALUES (?, ?, ?)");
    query.addBindValue(imagePath);
    query.addBindValue(imageWidth);
    query.addBindValue(imageHeight);

    if (!query.exec()) {
        qDebug() << "Failed to create task:" << query.lastError().text();
        return -1;
    }
    return query.lastInsertId().toInt();
}

bool DatabaseManager::updateTaskResult(int taskId, int objectCount, float avgConfidence, qint64 inferenceMs)
{
    QSqlQuery query(m_db);
    query.prepare("UPDATE detection_tasks SET object_count=?, avg_confidence=?, inference_ms=? WHERE id=?");
    query.addBindValue(objectCount);
    query.addBindValue(avgConfidence);
    query.addBindValue(inferenceMs);
    query.addBindValue(taskId);

    if (!query.exec()) {
        qDebug() << "Failed to update task:" << query.lastError().text();
        return false;
    }
    return true;
}

bool DatabaseManager::insertDetection(int taskId, const Detection& det)
{
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO detection_objects (task_id, class_id, class_name, confidence, bbox_x, bbox_y, bbox_w, bbox_h) VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    query.addBindValue(taskId);
    query.addBindValue(det.classId);
    query.addBindValue(det.className);
    query.addBindValue(det.confidence);
    query.addBindValue(det.bbox.x());
    query.addBindValue(det.bbox.y());
    query.addBindValue(det.bbox.width());
    query.addBindValue(det.bbox.height());

    if (!query.exec()) {
        qDebug() << "Failed to insert detection:" << query.lastError().text();
        return false;
    }
    return true;
}

bool DatabaseManager::deleteTask(int taskId)
{
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM detection_objects WHERE task_id=?");
    query.addBindValue(taskId);
    query.exec();

    query.prepare("DELETE FROM detection_tasks WHERE id=?");
    query.addBindValue(taskId);
    return query.exec();
}

bool DatabaseManager::deleteVideoResult(int videoId)
{
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM video_results WHERE id=?");
    query.addBindValue(videoId);
    return query.exec();
}

bool DatabaseManager::saveVideoResult(const QString& videoPath, int totalFrames, int totalObjects, const QMap<QString, int>& classCounts)
{
    QSqlQuery query(m_db);
    
    QString classCountsStr;
    for (auto it = classCounts.begin(); it != classCounts.end(); ++it) {
        if (!classCountsStr.isEmpty()) classCountsStr += ",";
        classCountsStr += QString("%1:%2").arg(it.key()).arg(it.value());
    }
    
    query.prepare("INSERT INTO video_results (video_path, total_frames, total_objects, class_counts) VALUES (?, ?, ?, ?)");
    query.addBindValue(videoPath);
    query.addBindValue(totalFrames);
    query.addBindValue(totalObjects);
    query.addBindValue(classCountsStr);

    if (!query.exec()) {
        qDebug() << "Failed to save video result:" << query.lastError().text();
        return false;
    }
    return true;
}

std::vector<DatabaseManager::TaskInfo> DatabaseManager::getTasksByDate(const QDate& date)
{
    std::vector<TaskInfo> tasks;
    QSqlQuery query(m_db);
    query.prepare("SELECT * FROM detection_tasks WHERE DATE(created_at) = ? ORDER BY created_at DESC");
    query.addBindValue(date.toString("yyyy-MM-dd"));
    query.exec();

    while (query.next()) {
        TaskInfo info;
        info.id = query.value("id").toInt();
        info.imagePath = query.value("image_path").toString();
        info.imageWidth = query.value("image_width").toInt();
        info.imageHeight = query.value("image_height").toInt();
        info.objectCount = query.value("object_count").toInt();
        info.avgConfidence = query.value("avg_confidence").toFloat();
        info.inferenceMs = query.value("inference_ms").toLongLong();
        info.createdAt = query.value("created_at").toString();
        tasks.push_back(info);
    }
    return tasks;
}

std::vector<DatabaseManager::TaskInfo> DatabaseManager::getTasksByDateRange(const QDate& startDate, const QDate& endDate)
{
    std::vector<TaskInfo> tasks;
    QSqlQuery query(m_db);
    query.prepare("SELECT * FROM detection_tasks WHERE DATE(created_at) BETWEEN ? AND ? ORDER BY created_at DESC");
    query.addBindValue(startDate.toString("yyyy-MM-dd"));
    query.addBindValue(endDate.toString("yyyy-MM-dd"));
    query.exec();

    while (query.next()) {
        TaskInfo info;
        info.id = query.value("id").toInt();
        info.imagePath = query.value("image_path").toString();
        info.imageWidth = query.value("image_width").toInt();
        info.imageHeight = query.value("image_height").toInt();
        info.objectCount = query.value("object_count").toInt();
        info.avgConfidence = query.value("avg_confidence").toFloat();
        info.inferenceMs = query.value("inference_ms").toLongLong();
        info.createdAt = query.value("created_at").toString();
        tasks.push_back(info);
    }
    return tasks;
}

std::vector<DatabaseManager::VideoResultInfo> DatabaseManager::getVideoResultsByDateRange(const QDate& startDate, const QDate& endDate)
{
    std::vector<VideoResultInfo> results;
    QSqlQuery query(m_db);
    query.prepare("SELECT * FROM video_results WHERE DATE(created_at) BETWEEN ? AND ? ORDER BY created_at DESC");
    query.addBindValue(startDate.toString("yyyy-MM-dd"));
    query.addBindValue(endDate.toString("yyyy-MM-dd"));
    query.exec();

    while (query.next()) {
        VideoResultInfo info;
        info.id = query.value("id").toInt();
        info.videoPath = query.value("video_path").toString();
        info.totalFrames = query.value("total_frames").toInt();
        info.totalObjects = query.value("total_objects").toInt();
        info.classCounts = query.value("class_counts").toString();
        info.createdAt = query.value("created_at").toString();
        results.push_back(info);
    }
    return results;
}

std::vector<Detection> DatabaseManager::getDetectionsByTaskId(int taskId)
{
    std::vector<Detection> detections;
    QSqlQuery query(m_db);
    query.prepare("SELECT * FROM detection_objects WHERE task_id=? ORDER BY id");
    query.addBindValue(taskId);
    query.exec();

    while (query.next()) {
        Detection det;
        det.classId = query.value("class_id").toInt();
        det.className = query.value("class_name").toString();
        det.confidence = query.value("confidence").toFloat();
        det.bbox = QRectF(
            query.value("bbox_x").toFloat(),
            query.value("bbox_y").toFloat(),
            query.value("bbox_w").toFloat(),
            query.value("bbox_h").toFloat()
        );
        detections.push_back(det);
    }
    return detections;
}

std::vector<Detection> DatabaseManager::getDetectionsByImageName(const QString& imageName)
{
    std::vector<Detection> detections;
    QSqlQuery query(m_db);
    query.prepare(R"(
        SELECT o.* FROM detection_objects o
        JOIN detection_tasks t ON o.task_id = t.id
        WHERE t.image_path LIKE ?
        ORDER BY o.id
    )");
    query.addBindValue("%" + imageName);
    query.exec();

    while (query.next()) {
        Detection det;
        det.classId = query.value("class_id").toInt();
        det.className = query.value("class_name").toString();
        det.confidence = query.value("confidence").toFloat();
        det.bbox = QRectF(
            query.value("bbox_x").toFloat(),
            query.value("bbox_y").toFloat(),
            query.value("bbox_w").toFloat(),
            query.value("bbox_h").toFloat()
        );
        detections.push_back(det);
    }
    return detections;
}

std::vector<DatabaseManager::ObjectStat> DatabaseManager::getClassStatisticsByDate(const QDate& date)
{
    std::vector<ObjectStat> stats;
    QSqlQuery query(m_db);
    query.prepare(R"(
        SELECT o.class_name, COUNT(*) as cnt, AVG(o.confidence) as avg_conf
        FROM detection_objects o
        JOIN detection_tasks t ON o.task_id = t.id
        WHERE DATE(t.created_at) = ?
        GROUP BY o.class_name ORDER BY cnt DESC
    )");
    query.addBindValue(date.toString("yyyy-MM-dd"));
    query.exec();

    while (query.next()) {
        ObjectStat stat;
        stat.className = query.value("class_name").toString();
        stat.count = query.value("cnt").toInt();
        stat.avgConfidence = query.value("avg_conf").toFloat();
        stats.push_back(stat);
    }
    return stats;
}

int DatabaseManager::getTotalObjectsByDate(const QDate& date)
{
    QSqlQuery query(m_db);
    query.prepare("SELECT SUM(object_count) FROM detection_tasks WHERE DATE(created_at) = ?");
    query.addBindValue(date.toString("yyyy-MM-dd"));
    query.exec();
    if (query.next()) return query.value(0).toInt();
    return 0;
}

int DatabaseManager::getTotalTasksByDate(const QDate& date)
{
    QSqlQuery query(m_db);
    query.prepare("SELECT COUNT(*) FROM detection_tasks WHERE DATE(created_at) = ?");
    query.addBindValue(date.toString("yyyy-MM-dd"));
    query.exec();
    if (query.next()) return query.value(0).toInt();
    return 0;
}

float DatabaseManager::getAverageConfidenceByDate(const QDate& date)
{
    QSqlQuery query(m_db);
    query.prepare(R"(
        SELECT AVG(o.confidence) FROM detection_objects o
        JOIN detection_tasks t ON o.task_id = t.id
        WHERE DATE(t.created_at) = ?
    )");
    query.addBindValue(date.toString("yyyy-MM-dd"));
    query.exec();
    if (query.next()) return query.value(0).toFloat();
    return 0.0f;
}

QString DatabaseManager::getTopClassByDate(const QDate& date)
{
    QSqlQuery query(m_db);
    query.prepare(R"(
        SELECT o.class_name, COUNT(*) as cnt
        FROM detection_objects o
        JOIN detection_tasks t ON o.task_id = t.id
        WHERE DATE(t.created_at) = ?
        GROUP BY o.class_name ORDER BY cnt DESC LIMIT 1
    )");
    query.addBindValue(date.toString("yyyy-MM-dd"));
    query.exec();
    if (query.next()) return query.value("class_name").toString();
    return "--";
}

std::vector<DatabaseManager::TaskInfo> DatabaseManager::getAllTasks()
{
    std::vector<TaskInfo> tasks;
    QSqlQuery query(m_db);
    query.exec("SELECT * FROM detection_tasks ORDER BY created_at DESC");

    while (query.next()) {
        TaskInfo info;
        info.id = query.value("id").toInt();
        info.imagePath = query.value("image_path").toString();
        info.imageWidth = query.value("image_width").toInt();
        info.imageHeight = query.value("image_height").toInt();
        info.objectCount = query.value("object_count").toInt();
        info.avgConfidence = query.value("avg_confidence").toFloat();
        info.inferenceMs = query.value("inference_ms").toLongLong();
        info.createdAt = query.value("created_at").toString();
        tasks.push_back(info);
    }
    return tasks;
}

std::vector<DatabaseManager::ObjectStat> DatabaseManager::getClassStatistics()
{
    std::vector<ObjectStat> stats;
    QSqlQuery query(m_db);
    query.exec("SELECT class_name, COUNT(*) as cnt, AVG(confidence) as avg_conf FROM detection_objects GROUP BY class_name ORDER BY cnt DESC");

    while (query.next()) {
        ObjectStat stat;
        stat.className = query.value("class_name").toString();
        stat.count = query.value("cnt").toInt();
        stat.avgConfidence = query.value("avg_conf").toFloat();
        stats.push_back(stat);
    }
    return stats;
}

int DatabaseManager::getTotalObjects()
{
    QSqlQuery query(m_db);
    query.exec("SELECT SUM(object_count) FROM detection_tasks");
    if (query.next()) return query.value(0).toInt();
    return 0;
}

int DatabaseManager::getTotalTasks()
{
    QSqlQuery query(m_db);
    query.exec("SELECT COUNT(*) FROM detection_tasks");
    if (query.next()) return query.value(0).toInt();
    return 0;
}

float DatabaseManager::getAverageConfidence()
{
    QSqlQuery query(m_db);
    query.exec("SELECT AVG(confidence) FROM detection_objects");
    if (query.next()) return query.value(0).toFloat();
    return 0.0f;
}

QString DatabaseManager::getTopClass()
{
    QSqlQuery query(m_db);
    query.exec("SELECT class_name, COUNT(*) as cnt FROM detection_objects GROUP BY class_name ORDER BY cnt DESC LIMIT 1");
    if (query.next()) return query.value("class_name").toString();
    return "--";
}

bool DatabaseManager::exportToCSV(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qDebug() << "Failed to open file for export:" << filePath;
        return false;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);

    out << "task_id,image_path,image_width,image_height,object_count,avg_confidence,inference_ms,created_at,class_id,class_name,confidence,bbox_x,bbox_y,bbox_w,bbox_h\n";

    QSqlQuery query(m_db);
    query.exec(R"(
        SELECT t.id, t.image_path, t.image_width, t.image_height, t.object_count, t.avg_confidence, t.inference_ms, t.created_at,
               o.class_id, o.class_name, o.confidence, o.bbox_x, o.bbox_y, o.bbox_w, o.bbox_h
        FROM detection_tasks t
        LEFT JOIN detection_objects o ON t.id = o.task_id
        ORDER BY t.id, o.id
    )");

    while (query.next()) {
        for (int i = 0; i < 15; i++) {
            if (i > 0) out << ",";
            out << query.value(i).toString();
        }
        out << "\n";
    }

    file.close();
    qDebug() << "CSV exported to:" << filePath;
    return true;
}
