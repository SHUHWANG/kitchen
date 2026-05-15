#include "LlmClient.h"
#include <QNetworkRequest>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QEventLoop>
#include <QDebug>

LlmClient::LlmClient(QObject* parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
{
    connect(m_networkManager, &QNetworkAccessManager::finished,
            this, &LlmClient::onReplyFinished);
}

LlmClient::~LlmClient()
{
    cancelCurrentRequest();
}

void LlmClient::setConfig(const LlmConfig& config)
{
    m_config = config;
}

void LlmClient::setApiKey(const QString& apiKey)
{
    m_config.apiKey = apiKey;
}

void LlmClient::setProvider(LlmProvider provider)
{
    m_config = getDefaultConfig(provider);
}

LlmConfig LlmClient::getDefaultConfig(LlmProvider provider) const
{
    LlmConfig config;
    config.provider = provider;
    
    switch (provider) {
    case LlmProvider::XiaomiMimo:
        config.baseUrl = "https://token-plan-cn.xiaomimimo.com/v1";
        config.modelName = "mimo-v2.5-pro";
        break;
    case LlmProvider::DeepSeek:
        config.baseUrl = "https://api.deepseek.com/v1";
        config.modelName = "deepseek-chat";
        break;
    case LlmProvider::OpenAI:
        config.baseUrl = "https://api.openai.com/v1";
        config.modelName = "gpt-3.5-turbo";
        break;
    }
    
    return config;
}

void LlmClient::chatAsync(const QString& message, LlmResponseCallback callback)
{
    QJsonArray messages;
    QJsonObject userMessage;
    userMessage["role"] = "user";
    userMessage["content"] = message;
    messages.append(userMessage);
    
    chatWithContextAsync(messages, callback);
}

QString LlmClient::chatSync(const QString& message)
{
    // 同步版本，使用事件循环阻塞等待
    QJsonArray messages;
    QJsonObject userMessage;
    userMessage["role"] = "user";
    userMessage["content"] = message;
    messages.append(userMessage);
    
    QJsonObject body = buildRequestBody(messages);
    
    QNetworkRequest request;
    request.setUrl(QUrl(m_config.baseUrl + "/chat/completions"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", ("Bearer " + m_config.apiKey).toUtf8());
    
    QJsonDocument doc(body);
    QByteArray data = doc.toJson();
    
    QNetworkReply* reply = m_networkManager->post(request, data);
    
    // 阻塞等待响应
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    
    QString result;
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray responseData = reply->readAll();
        QJsonDocument responseDoc = QJsonDocument::fromJson(responseData);
        result = parseResponse(responseDoc);
    } else {
        result = "错误: " + reply->errorString();
    }
    
    reply->deleteLater();
    return result;
}

void LlmClient::chatWithContextAsync(const QJsonArray& messages, LlmResponseCallback callback)
{
    if (m_config.apiKey.isEmpty()) {
        if (callback) {
            callback("", false, "API密钥未设置");
        }
        return;
    }
    
    QJsonObject body = buildRequestBody(messages);
    sendRequest(body, callback);
}

QJsonObject LlmClient::buildRequestBody(const QJsonArray& messages)
{
    QJsonObject body;
    body["model"] = m_config.modelName;
    body["messages"] = messages;
    body["max_tokens"] = m_config.maxTokens;
    body["temperature"] = m_config.temperature;
    body["stream"] = m_config.stream;
    
    return body;
}

void LlmClient::sendRequest(const QJsonObject& body, LlmResponseCallback callback)
{
    cancelCurrentRequest();
    
    QNetworkRequest request;
    request.setUrl(QUrl(m_config.baseUrl + "/chat/completions"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", ("Bearer " + m_config.apiKey).toUtf8());
    
    QJsonDocument doc(body);
    QByteArray data = doc.toJson();
    
    m_currentCallback = callback;
    m_currentReply = m_networkManager->post(request, data);
    
    // 连接错误信号用于调试
    connect(m_currentReply, &QNetworkReply::errorOccurred, this, [this](QNetworkReply::NetworkError error) {
        qDebug() << "LlmClient: Network error:" << error << m_currentReply->errorString();
    });
    
    emit requestStarted();
}

void LlmClient::cancelCurrentRequest()
{
    if (m_currentReply) {
        disconnect(m_currentReply, nullptr, this, nullptr);
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
        m_currentCallback = nullptr;
    }
}

void LlmClient::onReplyFinished(QNetworkReply* reply)
{
    if (reply != m_currentReply) {
        reply->deleteLater();
        return;
    }
    
    // 保存回调，因为回调可能触发新请求
    LlmResponseCallback callback = m_currentCallback;
    m_currentReply = nullptr;
    m_currentCallback = nullptr;
    
    emit requestFinished();
    
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray responseData = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(responseData);
        QString response = parseResponse(doc);
        
        qDebug() << "LlmClient: Response received:" << response.left(100);
        
        if (callback) {
            callback(response, true, "");
        }
        emit responseReceived(response);
    } else {
        QString error = reply->errorString();
        if (reply->error() == QNetworkReply::OperationCanceledError) {
            error = "请求已取消";
        }
        
        qDebug() << "LlmClient: Error:" << error;
        
        if (callback) {
            callback("", false, error);
        }
        emit errorOccurred(error);
    }
    
    reply->deleteLater();
}

QString LlmClient::parseResponse(const QJsonDocument& doc)
{
    if (!doc.isObject()) {
        return "无效的响应格式";
    }
    
    QJsonObject obj = doc.object();
    
    // 检查是否有错误
    if (obj.contains("error")) {
        QJsonObject error = obj["error"].toObject();
        return "API错误: " + error["message"].toString();
    }
    
    // 解析标准OpenAI格式响应
    if (obj.contains("choices")) {
        QJsonArray choices = obj["choices"].toArray();
        if (!choices.isEmpty()) {
            QJsonObject choice = choices[0].toObject();
            QJsonObject message = choice["message"].toObject();
            return message["content"].toString();
        }
    }
    
    return "无法解析响应";
}