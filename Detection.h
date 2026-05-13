#pragma once

#include <QString>
#include <QRectF>
#include <QColor>
#include <vector>

struct Detection {
    int classId;
    QString className;
    float confidence;
    QRectF bbox;
    int imageIndex;

    Detection() : classId(-1), confidence(0.0f), imageIndex(-1) {}
    Detection(int id, const QString& name, float conf, const QRectF& box, int imgIdx = -1)
        : classId(id), className(name), confidence(conf), bbox(box), imageIndex(imgIdx) {}

    bool isValid() const { return classId >= 0 && confidence > 0.0f; }

    static QColor getClassColor(int classId) {
        static const QColor colors[] = {
            QColor(255, 56, 56), QColor(255, 157, 151), QColor(255, 112, 31),
            QColor(255, 178, 29), QColor(207, 210, 49), QColor(72, 249, 10),
            QColor(146, 204, 23), QColor(61, 219, 134), QColor(26, 147, 52),
            QColor(0, 212, 187), QColor(255, 0, 0), QColor(0, 255, 0),
            QColor(0, 0, 255), QColor(255, 255, 0), QColor(255, 0, 255),
            QColor(0, 255, 255), QColor(128, 0, 0), QColor(0, 128, 0),
            QColor(0, 0, 128), QColor(128, 128, 0)
        };
        return colors[classId % 20];
    }
};

struct ImageInfo {
    QString filePath;
    QString fileName;
    int width;
    int height;
    bool detected;
    std::vector<Detection> detections;

    ImageInfo() : width(0), height(0), detected(false) {}
};

class DetectionConfig {
public:
    static DetectionConfig& instance() {
        static DetectionConfig config;
        return config;
    }

    QStringList classNames() const {
        return classNamesVisDrone();
    }

    QStringList classNamesCoco() const {
        return {
            "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck",
            "boat", "traffic light", "fire hydrant", "stop sign", "parking meter", "bench",
            "bird", "cat", "dog", "horse", "sheep", "cow", "elephant", "bear", "zebra",
            "giraffe", "backpack", "umbrella", "handbag", "tie", "suitcase", "frisbee",
            "skis", "snowboard", "sports ball", "kite", "baseball bat", "baseball glove",
            "skateboard", "surfboard", "tennis racket", "bottle", "wine glass", "cup",
            "fork", "knife", "spoon", "bowl", "banana", "apple", "sandwich", "orange",
            "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair", "couch",
            "potted plant", "bed", "dining table", "toilet", "tv", "laptop", "mouse",
            "remote", "keyboard", "cell phone", "microwave", "oven", "toaster", "sink",
            "refrigerator", "book", "clock", "vase", "scissors", "teddy bear",
            "hair drier", "toothbrush"
        };
    }

    QStringList classNamesVisDrone() const {
        return {
            QString::fromUtf8("\xe8\xa1\x8c\xe4\xba\xba"),
            QString::fromUtf8("\xe4\xba\xba\xe7\xbe\xa4"),
            QString::fromUtf8("\xe8\x87\xaa\xe8\xa1\x8c\xe8\xbd\xa6"),
            QString::fromUtf8("\xe8\xbd\xbf\xe8\xbd\xa6"),
            QString::fromUtf8("\xe9\x9d\xa2\xe5\x8c\x85\xe8\xbd\xa6"),
            QString::fromUtf8("\xe5\x8d\xa1\xe8\xbd\xa6"),
            QString::fromUtf8("\xe4\xb8\x89\xe8\xbd\xae\xe8\xbd\xa6"),
            QString::fromUtf8("\xe9\x81\xae\xe9\x98\xb3\xe4\xb8\x89\xe8\xbd\xae\xe8\xbd\xa6"),
            QString::fromUtf8("\xe5\x85\xac\xe4\xba\xa4\xe8\xbd\xa6"),
            QString::fromUtf8("\xe6\x91\xa9\xe6\x89\x98\xe8\xbd\xa6")
        };
    }

    QStringList classNamesEn() const {
        return classNamesVisDrone();
    }

    QString className(int id) const {
        QStringList names = classNames();
        if (id >= 0 && id < names.size()) return names[id];
        return QString("unknown_%1").arg(id);
    }

    int inputWidth() const { return 640; }
    int inputHeight() const { return 640; }
    float confThreshold() const { return 0.001f; }
    float nmsThreshold() const { return 0.3f; }

private:
    DetectionConfig() = default;
};
