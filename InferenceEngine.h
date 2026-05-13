#pragma once

#include <QObject>
#include <QImage>
#include <QString>
#include <vector>
#include <memory>
#include "Detection.h"

#include "NvInfer.h"
#include "NvOnnxParser.h"
#include "cuda_runtime_api.h"
#include "NvInferPlugin.h"

class Logger : public nvinfer1::ILogger {
public:
    void log(Severity severity, const char* msg) noexcept override;
};

class InferenceEngine : public QObject {
    Q_OBJECT

public:
    explicit InferenceEngine(QObject* parent = nullptr);
    ~InferenceEngine();

    bool loadEngine(const QString& enginePath);
    bool isLoaded() const { return m_engine != nullptr; }

    std::vector<Detection> detect(const QImage& image);
    std::vector<Detection> detect(const QString& imagePath);

    void setConfidenceThreshold(float threshold) { m_confThreshold = threshold; }
    void setNmsThreshold(float threshold) { m_nmsThreshold = threshold; }

    int inputWidth() const { return m_inputWidth; }
    int inputHeight() const { return m_inputHeight; }

private:
    bool buildEngineFromONNX(const QString& onnxPath);
    void preprocess(const QImage& image, float* inputBuffer);
    std::vector<Detection> postprocess(int imgWidth, int imgHeight);

    void allocateBuffers();
    void freeBuffers();

    Logger m_logger;
    nvinfer1::IRuntime* m_runtime = nullptr;
    nvinfer1::ICudaEngine* m_engine = nullptr;
    nvinfer1::IExecutionContext* m_context = nullptr;

    float* m_inputDevice = nullptr;
    float* m_outputDevice = nullptr;
    float* m_inputHost = nullptr;
    float* m_outputHost = nullptr;

    int m_inputWidth = 640;
    int m_inputHeight = 640;
    int m_inputSize = 0;
    int m_outputSize = 0;
    int m_numClasses = 10;
    int m_numDetections = 0;

    float m_confThreshold = 0.25f;
    float m_nmsThreshold = 0.45f;

    bool m_initialized = false;
};
