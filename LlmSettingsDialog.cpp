#include "LlmSettingsDialog.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QStandardPaths>
#include <QMessageBox>
#include <QApplication>

LlmSettingsDialog::LlmSettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    m_testClient = new LlmClient(this);  // 使用成员变量，生命周期由dialog管理
    setupUi();
    loadSettings();
}

LlmSettingsDialog::~LlmSettingsDialog()
{
}

void LlmSettingsDialog::setupUi()
{
    setWindowTitle("大模型设置");
    setMinimumWidth(500);
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // 提供商选择
    QFormLayout* formLayout = new QFormLayout();
    
    m_comboProvider = new QComboBox();
    m_comboProvider->addItem("小米 MIMO");
    m_comboProvider->addItem("DeepSeek");
    m_comboProvider->addItem("OpenAI");
    formLayout->addRow("提供商：", m_comboProvider);
    
    m_editApiKey = new QLineEdit();
    m_editApiKey->setEchoMode(QLineEdit::Password);
    m_editApiKey->setPlaceholderText("请输入API密钥");
    formLayout->addRow("API密钥：", m_editApiKey);
    
    m_editBaseUrl = new QLineEdit();
    m_editBaseUrl->setPlaceholderText("https://api.deepseek.com/v1");
    formLayout->addRow("API地址：", m_editBaseUrl);
    
    m_editModelName = new QLineEdit();
    m_editModelName->setPlaceholderText("deepseek-chat");
    formLayout->addRow("模型名称：", m_editModelName);
    
    mainLayout->addLayout(formLayout);
    
    // 状态标签
    m_lblStatus = new QLabel();
    m_lblStatus->setWordWrap(true);
    mainLayout->addWidget(m_lblStatus);
    
    // 按钮
    QHBoxLayout* btnLayout = new QHBoxLayout();
    
    m_btnTest = new QPushButton("测试连接");
    m_btnTest->setToolTip("测试API连接是否正常");
    btnLayout->addWidget(m_btnTest);
    
    btnLayout->addStretch();
    
    m_btnSave = new QPushButton("保存");
    m_btnSave->setToolTip("保存配置");
    btnLayout->addWidget(m_btnSave);
    
    m_btnCancel = new QPushButton("取消");
    btnLayout->addWidget(m_btnCancel);
    
    mainLayout->addLayout(btnLayout);
    
    // 连接信号
    connect(m_comboProvider, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &LlmSettingsDialog::onProviderChanged);
    connect(m_btnSave, &QPushButton::clicked, this, &LlmSettingsDialog::onSaveClicked);
    connect(m_btnCancel, &QPushButton::clicked, this, &LlmSettingsDialog::onCancelClicked);
    connect(m_btnTest, &QPushButton::clicked, this, &LlmSettingsDialog::onTestClicked);
    
    // 初始化默认配置
    m_configs = {
        {"小米 MIMO", LlmProvider::XiaomiMimo, "", "https://api.xiaomimimo.com/v1", "mimo-7b"},
        {"DeepSeek", LlmProvider::DeepSeek, "", "https://api.deepseek.com/v1", "deepseek-chat"},
        {"OpenAI", LlmProvider::OpenAI, "", "https://api.openai.com/v1", "gpt-3.5-turbo"}
    };
}

void LlmSettingsDialog::loadSettings()
{
    m_configs = loadConfigs();
    if (!m_configs.isEmpty()) {
        m_comboProvider->setCurrentIndex(0);
        updateProviderInfo();
    }
}

void LlmSettingsDialog::saveSettings()
{
    qDebug() << "saveSettings: m_currentIndex =" << m_currentIndex;
    
    // 保存当前编辑的配置
    if (m_currentIndex >= 0 && m_currentIndex < m_configs.size()) {
        m_configs[m_currentIndex].apiKey = m_editApiKey->text();
        m_configs[m_currentIndex].baseUrl = m_editBaseUrl->text();
        m_configs[m_currentIndex].modelName = m_editModelName->text();
        qDebug() << "Saving config for" << m_configs[m_currentIndex].name << "apiKey:" << (m_editApiKey->text().isEmpty() ? "empty" : "set");
    }
    
    // 打印所有配置
    for (int i = 0; i < m_configs.size(); i++) {
        qDebug() << "Config" << i << ":" << m_configs[i].name << "apiKey:" << (m_configs[i].apiKey.isEmpty() ? "empty" : "set");
    }
    
    saveConfigs(m_configs);
}

void LlmSettingsDialog::updateProviderInfo()
{
    if (m_currentIndex >= 0 && m_currentIndex < m_configs.size()) {
        const auto& config = m_configs[m_currentIndex];
        m_editApiKey->setText(config.apiKey);
        m_editBaseUrl->setText(config.baseUrl);
        m_editModelName->setText(config.modelName);
    }
}

void LlmSettingsDialog::onProviderChanged(int index)
{
    qDebug() << "onProviderChanged: from" << m_currentIndex << "to" << index;
    
    // 保存当前配置
    if (m_currentIndex >= 0 && m_currentIndex < m_configs.size()) {
        m_configs[m_currentIndex].apiKey = m_editApiKey->text();
        m_configs[m_currentIndex].baseUrl = m_editBaseUrl->text();
        m_configs[m_currentIndex].modelName = m_editModelName->text();
        qDebug() << "Saved config for" << m_configs[m_currentIndex].name << "apiKey:" << (m_editApiKey->text().isEmpty() ? "empty" : "set");
    }
    
    m_currentIndex = index;
    updateProviderInfo();
    m_lblStatus->clear();
}

void LlmSettingsDialog::onSaveClicked()
{
    saveSettings();
    accept();
}

void LlmSettingsDialog::onCancelClicked()
{
    reject();
}

void LlmSettingsDialog::onTestClicked()
{
    m_lblStatus->setText("正在测试连接...");
    m_lblStatus->setStyleSheet("color: #FFB000;");
    m_btnTest->setEnabled(false);
    
    // 使用成员变量测试客户端
    LlmConfig config;
    config.provider = m_configs[m_currentIndex].provider;
    config.apiKey = m_editApiKey->text();
    config.baseUrl = m_editBaseUrl->text();
    config.modelName = m_editModelName->text();
    m_testClient->setConfig(config);
    
    m_testClient->chatAsync("你好，请回复'连接成功'", [this](const QString& response, bool success, const QString& error) {
        m_btnTest->setEnabled(true);
        if (success) {
            m_lblStatus->setText("连接成功！响应：" + response.left(50));
            m_lblStatus->setStyleSheet("color: #00FF88;");
            
            // 测试成功，更新当前配置
            if (m_currentIndex >= 0 && m_currentIndex < m_configs.size()) {
                m_configs[m_currentIndex].apiKey = m_editApiKey->text();
                m_configs[m_currentIndex].baseUrl = m_editBaseUrl->text();
                m_configs[m_currentIndex].modelName = m_editModelName->text();
            }
        } else {
            m_lblStatus->setText("连接失败：" + error);
            m_lblStatus->setStyleSheet("color: #FF6B6B;");
        }
    });
}

QString LlmSettingsDialog::getConfigFilePath()
{
    QString appDir = QApplication::applicationDirPath();
    return appDir + "/llm_config.json";
}

QList<LlmProviderConfig> LlmSettingsDialog::loadConfigs()
{
    QList<LlmProviderConfig> configs;
    
    // 默认配置（小米MIMO放第一位作为默认）
    configs = {
        {"小米 MIMO", LlmProvider::XiaomiMimo, "", "https://api.xiaomimimo.com/v1", "mimo-7b"},
        {"DeepSeek", LlmProvider::DeepSeek, "", "https://api.deepseek.com/v1", "deepseek-chat"},
        {"OpenAI", LlmProvider::OpenAI, "", "https://api.openai.com/v1", "gpt-3.5-turbo"}
    };
    
    QString filePath = getConfigFilePath();
    QFile file(filePath);
    
    if (!file.open(QIODevice::ReadOnly)) {
        return configs;
    }
    
    QByteArray data = file.readAll();
    file.close();
    
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isArray()) {
        return configs;
    }
    
    QJsonArray array = doc.array();
    configs.clear();
    
    for (const auto& value : array) {
        QJsonObject obj = value.toObject();
        LlmProviderConfig config;
        config.name = obj["name"].toString();
        config.apiKey = obj["apiKey"].toString();
        config.baseUrl = obj["baseUrl"].toString();
        config.modelName = obj["modelName"].toString();
        
        QString providerStr = obj["provider"].toString();
        if (providerStr == "DeepSeek") {
            config.provider = LlmProvider::DeepSeek;
        } else if (providerStr == "XiaomiMimo") {
            config.provider = LlmProvider::XiaomiMimo;
        } else if (providerStr == "OpenAI") {
            config.provider = LlmProvider::OpenAI;
        }
        
        configs.append(config);
    }
    
    return configs;
}

void LlmSettingsDialog::saveConfigs(const QList<LlmProviderConfig>& configs)
{
    QJsonArray array;
    
    for (const auto& config : configs) {
        QJsonObject obj;
        obj["name"] = config.name;
        obj["apiKey"] = config.apiKey;
        obj["baseUrl"] = config.baseUrl;
        obj["modelName"] = config.modelName;
        
        switch (config.provider) {
        case LlmProvider::DeepSeek:
            obj["provider"] = "DeepSeek";
            break;
        case LlmProvider::XiaomiMimo:
            obj["provider"] = "XiaomiMimo";
            break;
        case LlmProvider::OpenAI:
            obj["provider"] = "OpenAI";
            break;
        }
        
        array.append(obj);
    }
    
    QJsonDocument doc(array);
    QString filePath = getConfigFilePath();
    QFile file(filePath);
    
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson());
        file.close();
    }
}