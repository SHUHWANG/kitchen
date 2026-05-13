#include "InferenceEngine.h"
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <opencv2/opencv.hpp>
#include <algorithm>
#include <fstream>
#include <cmath>

void Logger::log(Severity severity, const char* msg) noexcept {
    if (severity <= Severity::kWARNING) {
        qDebug() << "[TensorRT]" << msg;
    }
}

InferenceEngine::InferenceEngine(QObject* parent)
    : QObject(parent)
{
}

InferenceEngine::~InferenceEngine()
{
    freeBuffers();
    if (m_context) { delete m_context; m_context = nullptr; }
    if (m_engine) { delete m_engine; m_engine = nullptr; }
    if (m_runtime) { delete m_runtime; m_runtime = nullptr; }
}

bool InferenceEngine::loadEngine(const QString& enginePath)
{
    QFileInfo fi(enginePath);
    if (!fi.exists()) {
        qDebug() << "Engine file not found:" << enginePath;
        return false;
    }

    std::ifstream file(enginePath.toStdString(), std::ios::binary);
    if (!file.good()) {
        qDebug() << "Failed to open engine file:" << enginePath;
        return false;
    }

    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> engineData(size);
    file.read(engineData.data(), size);
    file.close();

    initLibNvInferPlugins(&m_logger, "");

    m_runtime = nvinfer1::createInferRuntime(m_logger);
    if (!m_runtime) {
        qDebug() << "Failed to create TensorRT runtime";
        return false;
    }

    m_engine = m_runtime->deserializeCudaEngine(engineData.data(), size);
    if (!m_engine) {
        qDebug() << "Failed to deserialize engine";
        return false;
    }

    m_context = m_engine->createExecutionContext();
    if (!m_context) {
        qDebug() << "Failed to create execution context";
        return false;
    }

    int nbBindings = m_engine->getNbIOTensors();
    if (nbBindings != 2) {
        qDebug() << "Expected 2 bindings (input/output), got:" << nbBindings;
        return false;
    }

    const char* inputTensorName = m_engine->getIOTensorName(0);
    const char* outputTensorName = m_engine->getIOTensorName(1);
    nvinfer1::Dims inputDims = m_engine->getTensorShape(inputTensorName);
    nvinfer1::Dims outputDims = m_engine->getTensorShape(outputTensorName);

    qDebug() << "=== Tensor Dimensions ===";
    qDebug() << "Input dims:" << inputDims.nbDims;
    for (int i = 0; i < inputDims.nbDims; i++) {
        qDebug() << "  inputDims.d[" << i << "] =" << inputDims.d[i];
    }
    qDebug() << "Output dims:" << outputDims.nbDims;
    for (int i = 0; i < outputDims.nbDims; i++) {
        qDebug() << "  outputDims.d[" << i << "] =" << outputDims.d[i];
    }
    qDebug() << "========================";

    m_inputWidth = inputDims.d[2];
    m_inputHeight = inputDims.d[3];
    m_inputSize = 1;
    for (int i = 0; i < inputDims.nbDims; i++) {
        m_inputSize *= inputDims.d[i];
    }

    m_outputSize = 1;
    for (int i = 0; i < outputDims.nbDims; i++) {
        m_outputSize *= outputDims.d[i];
    }

    if (outputDims.nbDims == 3) {
        m_numClasses = outputDims.d[1] - 4;
        m_numDetections = outputDims.d[2];
    } else if (outputDims.nbDims == 2) {
        m_numDetections = outputDims.d[0];
        m_numClasses = outputDims.d[1] - 4;
    }

    qDebug() << "Engine loaded:" << enginePath;
    qDebug() << "Input:" << m_inputWidth << "x" << m_inputHeight;
    qDebug() << "Output size:" << m_outputSize;
    qDebug() << "Num classes:" << m_numClasses;
    qDebug() << "Num detections:" << m_numDetections;

    allocateBuffers();
    m_initialized = true;
    return true;
}

void InferenceEngine::allocateBuffers()
{
    cudaMalloc(reinterpret_cast<void**>(&m_inputDevice), m_inputSize * sizeof(float));
    cudaMalloc(reinterpret_cast<void**>(&m_outputDevice), m_outputSize * sizeof(float));
    m_inputHost = new float[m_inputSize];
    m_outputHost = new float[m_outputSize];
}

void InferenceEngine::freeBuffers()
{
    if (m_inputDevice) { cudaFree(reinterpret_cast<void*>(m_inputDevice)); m_inputDevice = nullptr; }
    if (m_outputDevice) { cudaFree(reinterpret_cast<void*>(m_outputDevice)); m_outputDevice = nullptr; }
    if (m_inputHost) { delete[] m_inputHost; m_inputHost = nullptr; }
    if (m_outputHost) { delete[] m_outputHost; m_outputHost = nullptr; }
}

void InferenceEngine::preprocess(const QImage& image, float* inputBuffer)
{
    QImage rgbImage = image.convertToFormat(QImage::Format_RGB888);
    cv::Mat mat(rgbImage.height(), rgbImage.width(), CV_8UC3,
                const_cast<uchar*>(rgbImage.bits()), rgbImage.bytesPerLine());

    cv::Mat resized;
    cv::resize(mat, resized, cv::Size(m_inputWidth, m_inputHeight));

    int idx = 0;
    for (int c = 0; c < 3; c++) {
        for (int h = 0; h < m_inputHeight; h++) {
            for (int w = 0; w < m_inputWidth; w++) {
                inputBuffer[idx++] = resized.at<cv::Vec3b>(h, w)[c] / 255.0f;
            }
        }
    }
}

std::vector<Detection> InferenceEngine::postprocess(int imgWidth, int imgHeight)
{
    std::vector<Detection> detections;

    float scaleX = static_cast<float>(imgWidth) / m_inputWidth;
    float scaleY = static_cast<float>(imgHeight) / m_inputHeight;

    std::vector<cv::Rect2d> boxes;
    std::vector<float> confidences;
    std::vector<int> classIds;

    int numAttrs = m_numClasses + 4;
    int passCount = 0;

    for (int i = 0; i < m_numDetections; i++) {
        float cx = m_outputHost[0 * m_numDetections + i];
        float cy = m_outputHost[1 * m_numDetections + i];
        float w  = m_outputHost[2 * m_numDetections + i];
        float h  = m_outputHost[3 * m_numDetections + i];

        float maxConf = 0.0f;
        int maxClassId = -1;
        for (int c = 0; c < m_numClasses; c++) {
            float conf = m_outputHost[(4 + c) * m_numDetections + i];
            if (conf > maxConf) {
                maxConf = conf;
                maxClassId = c;
            }
        }

        if (maxConf < m_confThreshold) continue;
        passCount++;

        if (passCount <= 5) {
            qDebug() << "Pass" << passCount << ": conf=" << maxConf << "class=" << maxClassId;
        }

        float x1 = (cx - w / 2.0f) * scaleX;
        float y1 = (cy - h / 2.0f) * scaleY;
        float x2 = (cx + w / 2.0f) * scaleX;
        float y2 = (cy + h / 2.0f) * scaleY;

        x1 = std::max(0.0f, std::min(x1, static_cast<float>(imgWidth)));
        y1 = std::max(0.0f, std::min(y1, static_cast<float>(imgHeight)));
        x2 = std::max(0.0f, std::min(x2, static_cast<float>(imgWidth)));
        y2 = std::max(0.0f, std::min(y2, static_cast<float>(imgHeight)));

        boxes.push_back(cv::Rect2d(x1, y1, x2 - x1, y2 - y1));
        confidences.push_back(maxConf);
        classIds.push_back(maxClassId);
    }

    qDebug() << "Total passed threshold:" << passCount;

    std::vector<int> indices;
    cv::dnn::NMSBoxes(boxes, confidences, m_confThreshold, m_nmsThreshold, indices);

    qDebug() << "After NMS:" << indices.size();

    const auto& config = DetectionConfig::instance();
    for (int idx : indices) {
        QRectF bbox(boxes[idx].x, boxes[idx].y, boxes[idx].width, boxes[idx].height);
        detections.emplace_back(
            classIds[idx],
            config.className(classIds[idx]),
            confidences[idx],
            bbox
        );
    }

    return detections;
}

std::vector<Detection> InferenceEngine::detect(const QImage& image)
{
    if (!m_initialized || !m_context) {
        qDebug() << "Engine not initialized";
        return {};
    }

    int imgWidth = image.width();
    int imgHeight = image.height();

    preprocess(image, m_inputHost);

    cudaMemcpy(m_inputDevice, m_inputHost, m_inputSize * sizeof(float), cudaMemcpyHostToDevice);

    void* bindings[] = { m_inputDevice, m_outputDevice };
    bool success = m_context->executeV2(bindings);
    if (!success) {
        qDebug() << "TensorRT inference failed";
        return {};
    }

    cudaMemcpy(m_outputHost, m_outputDevice, m_outputSize * sizeof(float), cudaMemcpyDeviceToHost);

    auto results = postprocess(imgWidth, imgHeight);

    return results;

    return results;
}

std::vector<Detection> InferenceEngine::detect(const QString& imagePath)
{
    QImage image(imagePath);
    if (image.isNull()) {
        qDebug() << "Failed to load image:" << imagePath;
        return {};
    }
    return detect(image);
}
