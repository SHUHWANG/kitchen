#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QMap>
#include "LlmClient.h"

// 大模型配置结构（带名称）
struct LlmProviderConfig {
    QString name;
    LlmProvider provider;
    QString apiKey;
    QString baseUrl;
    QString modelName;
};

class LlmSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LlmSettingsDialog(QWidget* parent = nullptr);
    ~LlmSettingsDialog();

    // 获取/设置配置
    static QList<LlmProviderConfig> loadConfigs();
    static void saveConfigs(const QList<LlmProviderConfig>& configs);
    static QString getConfigFilePath();

private slots:
    void onProviderChanged(int index);
    void onSaveClicked();
    void onCancelClicked();
    void onTestClicked();

private:
    void setupUi();
    void loadSettings();
    void saveSettings();
    void updateProviderInfo();

private:
    QComboBox* m_comboProvider;
    QLineEdit* m_editApiKey;
    QLineEdit* m_editBaseUrl;
    QLineEdit* m_editModelName;
    QPushButton* m_btnSave;
    QPushButton* m_btnCancel;
    QPushButton* m_btnTest;
    QLabel* m_lblStatus;
    
    QList<LlmProviderConfig> m_configs;
    int m_currentIndex = 0;
    LlmClient* m_testClient = nullptr;  // 测试用客户端
};