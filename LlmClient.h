#pragma once

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <functional>

// 大模型API提供商枚举
enum class LlmProvider {
    XiaomiMimo,    // 小米MIMO
    DeepSeek,      // DeepSeek
    OpenAI         // OpenAI兼容格式
};

// 大模型配置结构体
struct LlmConfig {
    LlmProvider provider = LlmProvider::DeepSeek;
    QString apiKey;
    QString baseUrl;
    QString modelName;
    int maxTokens = 2048;
    float temperature = 0.7f;
    bool stream = false;
};

// 回调函数类型
using LlmResponseCallback = std::function<void(const QString& response, bool success, const QString& error)>;

class LlmClient : public QObject {
    Q_OBJECT

public:
    explicit LlmClient(QObject* parent = nullptr);
    ~LlmClient();

    // 配置方法
    void setConfig(const LlmConfig& config);
    LlmConfig getConfig() const { return m_config; }
    
    // 设置API密钥
    void setApiKey(const QString& apiKey);
    
    // 设置提供商
    void setProvider(LlmProvider provider);
    
    // 发送聊天请求（异步）
    void chatAsync(const QString& message, LlmResponseCallback callback);
    
    // 发送聊天请求（同步，阻塞）
    QString chatSync(const QString& message);
    
    // 带上下文的聊天（支持多轮对话）
    void chatWithContextAsync(const QJsonArray& messages, LlmResponseCallback callback);
    
    // 取消当前请求
    void cancelCurrentRequest();
    
    // 检查是否正在请求
    bool isRequesting() const { return m_currentReply != nullptr; }

signals:
    void responseReceived(const QString& response);
    void errorOccurred(const QString& error);
    void requestStarted();
    void requestFinished();

private slots:
    void onReplyFinished(QNetworkReply* reply);

private:
    // 构建请求体
    QJsonObject buildRequestBody(const QJsonArray& messages);
    
    // 解析响应
    QString parseResponse(const QJsonDocument& doc);
    
    // 发送HTTP请求
    void sendRequest(const QJsonObject& body, LlmResponseCallback callback);
    
    // 获取默认配置
    LlmConfig getDefaultConfig(LlmProvider provider) const;

private:
    QNetworkAccessManager* m_networkManager;
    LlmConfig m_config;
    QNetworkReply* m_currentReply = nullptr;
    LlmResponseCallback m_currentCallback;
};