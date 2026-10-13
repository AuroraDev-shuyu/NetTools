#include "formquicclient.h"
#include "ui_formquicclient.h"

#include <QDateTime>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QCloseEvent>
#include <QPlainTextEdit>

#include "errorhandler.h"
#include "inputvalidator.h"

// 功能：构造函数，初始化UI、绑定封装层信号并恢复上次配置
FormQuicClient::FormQuicClient(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::FormQuicClient)
{
    ui->setupUi(this);
    this->setWindowTitle("QUIC客户端");

    // 布局微调：两列等宽，消息编辑器吸收多余高度
    ui->gridLayout_3->setColumnStretch(0, 1);
    ui->gridLayout_3->setColumnStretch(1, 1);
    ui->gridLayout_3->setRowStretch(4, 1);

    ui->comboBox_QUICClientIP->setEditable(true);          // 允许手动输入服务器地址
    ui->pushButton_QUICClientDisconnect->setEnabled(false); // 初始未连接时禁用"断开"
    ui->pushButton_QUICClientSendMsg->setEnabled(false);    // 初始禁用发送
    ui->checkBox_QUICClientAutoTesting->setEnabled(false);  // 初始禁用自动测试

    // 恢复上次使用的IP与端口
    {
        QSettings settings;
        const QString lastIp = settings.value("QUICClient/lastIp", "127.0.0.1").toString();
        const int lastPort = settings.value("QUICClient/lastPort", 12345).toInt();

        int index = ui->comboBox_QUICClientIP->findText(lastIp);
        if (index >= 0) {
            ui->comboBox_QUICClientIP->setCurrentIndex(index);
        } else {
            ui->comboBox_QUICClientIP->setEditText(lastIp);
        }

        if (lastPort >= ui->spinBox_QUICClientPort->minimum() &&
            lastPort <= ui->spinBox_QUICClientPort->maximum()) {
            ui->spinBox_QUICClientPort->setValue(lastPort);
        }
    }

    // 浅色精致主题：与MQTT模块一致的统一样式
    setStyleSheet(
        "QWidget {"
        "  background-color: #EEF1F6;"
        "  color: #1F2933;"
        "}"
        "QGroupBox {"
        "  font-size: 13px;"
        "  font-weight: bold;"
        "  background-color: #FFFFFF;"
        "  border: 1px solid #DCE2EC;"
        "  border-radius: 8px;"
        "  margin-top: 12px;"
        "}"
        "QGroupBox::title {"
        "  subcontrol-origin: margin;"
        "  left: 12px;"
        "  padding: 0 6px;"
        "  background-color: #EEF1F6;"
        "  color: #334155;"
        "}"
        "QLabel {"
        "  font-size: 13px;"
        "  color: #475569;"
        "  background-color: transparent;"
        "}"
        "QComboBox, QSpinBox, QLineEdit {"
        "  font-size: 13px;"
        "  background-color: #FFFFFF;"
        "  border: 1px solid #CBD5E1;"
        "  border-radius: 6px;"
        "  padding: 4px 8px;"
        "  min-height: 20px;"
        "}"
        "QComboBox:hover, QSpinBox:hover, QLineEdit:hover {"
        "  border-color: #94A3B8;"
        "}"
        "QComboBox:focus, QSpinBox:focus, QLineEdit:focus {"
        "  border-color: #3B82F6;"
        "}"
        "QPlainTextEdit#plainTextEdit_QUICClientMsg {"
        "  font-size: 12px;"
        "  font-family: Consolas, 'Microsoft YaHei UI';"
        "  background-color: #FFFFFF;"
        "  border: 1px solid #E2E8F0;"
        "  border-radius: 6px;"
        "  padding: 4px;"
        "}"
        "QPlainTextEdit#plainTextEdit_QUICClientSendData {"
        "  font-size: 13px;"
        "  background-color: #FFFFFF;"
        "  border: 1px solid #CBD5E1;"
        "  border-radius: 6px;"
        "  padding: 4px;"
        "}"
        "QCheckBox {"
        "  font-size: 13px;"
        "  color: #475569;"
        "  background-color: transparent;"
        "}"
        "QPushButton {"
        "  font-size: 13px;"
        "  border: none;"
        "  border-radius: 6px;"
        "  padding: 5px 12px;"
        "}"
        "QPushButton#pushButton_QUICClientConnect {"
        "  background-color: #10B981;"
        "  color: #FFFFFF;"
        "}"
        "QPushButton#pushButton_QUICClientConnect:hover {"
        "  background-color: #059669;"
        "}"
        "QPushButton#pushButton_QUICClientDisconnect {"
        "  background-color: #EF4444;"
        "  color: #FFFFFF;"
        "}"
        "QPushButton#pushButton_QUICClientDisconnect:hover {"
        "  background-color: #DC2626;"
        "}"
        "QPushButton#pushButton_QUICClientSendMsg {"
        "  background-color: #2563EB;"
        "  color: #FFFFFF;"
        "}"
        "QPushButton#pushButton_QUICClientSendMsg:hover {"
        "  background-color: #1D4ED8;"
        "}"
        "QPushButton#pushButton_QUICClientClose {"
        "  background-color: #F1F5F9;"
        "  color: #475569;"
        "  border: 1px solid #CBD5E1;"
        "}"
        "QPushButton#pushButton_QUICClientClose:hover {"
        "  background-color: #E2E8F0;"
        "}"
        "QPushButton:disabled {"
        "  background-color: #E2E8F0;"
        "  color: #94A3B8;"
        "}"
        );

    // 绑定封装层信号（回调线程emit，Qt跨线程自动queued到本线程）
    connect(&m_networkClient, &QuicClient::connectionEstablished, this, [this]() {
        enableCommunicationControls(true);
        connected = true;
        appendLog(QString("[%1] 连接服务器成功")
                      .arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
    });

    connect(&m_networkClient, &QuicClient::connectionFailed, this,
            [this](const QString &errorString) {
        // 连接失败/远端关闭：复位控件并停止自动测试
        if (ui->checkBox_QUICClientAutoTesting->isChecked()) {
            ui->checkBox_QUICClientAutoTesting->setChecked(false);
        }
        enableCommunicationControls(false);
        connected = false;
        appendLog(QString("[%1] 连接失败: %2")
                      .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                      .arg(errorString));
    });

    connect(&m_networkClient, &QuicClient::dataReceived, this, [this](const QString &data) {
        appendLog(QString("\n[%1] 接收: %2")
                      .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                      .arg(data));
    });
}

// 析构函数：先断开信号（避免成员析构时回调访问已释放的UI），再释放UI
FormQuicClient::~FormQuicClient()
{
    m_networkClient.disconnect(this);
    delete ui;
}

// 功能：连接服务器——校验IP/端口，持久化配置并发起QUIC连接
void FormQuicClient::on_pushButton_QUICClientConnect_clicked()
{
    if (connected) {
        appendLog("[提示] 已连接服务器，无需重复连接");
        return;
    }

    QString ip = ui->comboBox_QUICClientIP->currentText().trimmed();
    int port = ui->spinBox_QUICClientPort->value();

    auto ipValidation = InputValidator::validatorNetworkAddress(ip);
    if (!ipValidation.isValid) {
        HANDLE_ERROR(ErrorHandler::ValidationError, ErrorHandler::Warning,
                     ipValidation.errorMessage, this);
        ui->comboBox_QUICClientIP->setFocus();
        return;
    }

    auto portValidation = InputValidator::validatorPort(port);
    if (!portValidation.isValid) {
        HANDLE_ERROR(ErrorHandler::ValidationError, ErrorHandler::Warning,
                     portValidation.errorMessage, this);
        ui->spinBox_QUICClientPort->setFocus();
        return;
    }

    {
        QSettings settings;                 // 持久化最新IP/端口
        settings.setValue("QUICClient/lastIp", ip);
        settings.setValue("QUICClient/lastPort", port);
        settings.sync();
    }

    connected = false;                      // 重置状态，等待握手结果
    ui->pushButton_QUICClientConnect->setEnabled(false);
    ui->pushButton_QUICClientDisconnect->setEnabled(true);

    appendLog(QString("[%1] 正在连接服务器 %2:%3...")
                  .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                  .arg(ip)
                  .arg(port));
    m_networkClient.ClientConnectToServer(ip, port);
}

// 功能：断开服务器——停止自动测试并复位控件
void FormQuicClient::on_pushButton_QUICClientDisconnect_clicked()
{
    m_networkClient.DisconnectFromHost();

    if (ui->checkBox_QUICClientAutoTesting->isChecked()) {
        ui->checkBox_QUICClientAutoTesting->setChecked(false);
    }
    enableCommunicationControls(false);
    connected = false;

    appendLog(QString("[%1] 已主动断开与服务器的连接")
                  .arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
}

// 功能：发送消息——校验连接状态与内容后发送
void FormQuicClient::on_pushButton_QUICClientSendMsg_clicked()
{
    QString message = ui->plainTextEdit_QUICClientSendData->toPlainText().trimmed();
    if (message.isEmpty()) {
        HANDLE_ERROR(ErrorHandler::ValidationError, ErrorHandler::Warning,
                     "发送内容不能为空！", this);
        return;
    }

    if (!connected) {
        HANDLE_ERROR(ErrorHandler::NetworkError, ErrorHandler::Warning,
                     "未连接服务器，无法发送。", this);
        return;
    }

    m_networkClient.ClientSendMsgToServer(message);

    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    appendLog(QString("\n[%1]\n客户端发送: %2").arg(timestamp, message));
}

// 功能：关闭窗口——保存日志并退出程序
void FormQuicClient::on_pushButton_QUICClientClose_clicked()
{
    saveLog();
    QCoreApplication::instance()->quit();
}

// 功能：自动测试开关——勾选后每1.5秒自动发送一次消息
void FormQuicClient::on_checkBox_QUICClientAutoTesting_clicked()
{
    bool isAutoTesting = ui->checkBox_QUICClientAutoTesting->isChecked();

    if (isAutoTesting) {
        if (!connected) {
            HANDLE_ERROR(ErrorHandler::NetworkError, ErrorHandler::Warning,
                         "未连接服务器，无法开始自动测试。", this);
            ui->checkBox_QUICClientAutoTesting->setChecked(false);
            return;
        }

        QString message = ui->plainTextEdit_QUICClientSendData->toPlainText().trimmed();
        if (message.isEmpty()) {
            HANDLE_ERROR(ErrorHandler::ValidationError, ErrorHandler::Warning,
                         "发送内容不能为空，无法开始自动测试！", this);
            ui->checkBox_QUICClientAutoTesting->setChecked(false);
            return;
        }

        m_networkClient.setAutoTestMessage(message);
        m_networkClient.StartTimerOutFunc();
        ui->plainTextEdit_QUICClientSendData->setEnabled(false);  // 禁用手动输入
        ui->pushButton_QUICClientSendMsg->setEnabled(false);      // 禁用手动发送
        appendLog("[提示] 自动测试已开启，每1.5秒自动发送一次消息");
    } else {
        m_networkClient.StopTimerOutFunc();
        enableCommunicationControls(true);                        // 恢复手动交互
        appendLog("[提示] 自动测试已停止");
    }
}

// 功能：批量更新与连接状态相关的按钮/输入控件
void FormQuicClient::enableCommunicationControls(bool enable)
{
    ui->pushButton_QUICClientConnect->setEnabled(!enable);
    ui->pushButton_QUICClientDisconnect->setEnabled(enable);
    ui->pushButton_QUICClientSendMsg->setEnabled(enable);
    ui->checkBox_QUICClientAutoTesting->setEnabled(enable);
    ui->plainTextEdit_QUICClientSendData->setEnabled(enable);
}

// 功能：追加日志并自动滚动到底部
void FormQuicClient::appendLog(const QString &text)
{
    ui->plainTextEdit_QUICClientMsg->appendPlainText(text);
    trimLog();
}

// 功能：裁剪日志，防止内容过长导致UI卡顿
void FormQuicClient::trimLog(int keepBlocks, int trimStep)
{
    static const int kKeepDefault = 1000;   // 默认保留block数
    static const int kTrimDefault = 200;    // 默认删除步长
    const int keep = keepBlocks > 0 ? keepBlocks : kKeepDefault;
    const int step = trimStep > 0 ? trimStep : kTrimDefault;

    QTextDocument *doc = ui->plainTextEdit_QUICClientMsg->document();
    int blocks = doc->blockCount();
    if (blocks <= keep + step) {
        return;
    }

    QTextCursor cursor(doc);
    cursor.movePosition(QTextCursor::Start);
    int removeBlocks = blocks - keep;
    for (int i = 0; i < removeBlocks; ++i) {
        cursor.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
    }
    cursor.removeSelectedText();
    cursor.deleteChar();
}

// 功能：窗口关闭事件——先断开连接再保存日志
void FormQuicClient::closeEvent(QCloseEvent *event)
{
    if (connected) {
        m_networkClient.DisconnectFromHost();
        connected = false;
    }

    saveLog();
    event->accept();
}

// 功能：保存日志（public接口，供主窗口关闭时调用）
void FormQuicClient::saveLog()
{
    if (ui && ui->plainTextEdit_QUICClientMsg) {
        savePlainTextEditToFile(ui->plainTextEdit_QUICClientMsg);
    }
}

// 功能：将日志文本保存到文件（与其他模块一致的追加式日志）
void FormQuicClient::savePlainTextEditToFile(QPlainTextEdit *plainTextEdit)
{
    if (!plainTextEdit) {
        qWarning() << "FormQuicClient::savePlainTextEditToFile: plainTextEdit is null";
        return;
    }

    // 与QSettings配置文件同一目录：%APPDATA%\NetTools
    QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir appDataDir(appDataPath);
    QString appName = QApplication::applicationName();
    if (!appName.isEmpty() && (appDataPath.endsWith("/" + appName) ||
                               appDataPath.endsWith("\\" + appName))) {
        QDir parentDir = appDataDir;
        if (parentDir.cdUp()) {
            appDataPath = parentDir.absolutePath();
        }
    }
    QDir().mkpath(appDataPath); // 确保目录存在

    QString fileName = QDir(appDataPath).filePath("QUICClientLogfile.txt");

    bool fileExists = QFile::exists(fileName);
    QIODevice::OpenMode openMode = fileExists ?
                                       (QIODevice::Append | QIODevice::Text) :
                                       (QIODevice::WriteOnly | QIODevice::Text);

    QFile file(fileName);
    if (file.open(openMode)) {
        QTextStream out(&file);
        out.setEncoding(QStringConverter::Utf8);

        if (fileExists) {
            QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
            out << "\n" << "========== " << timestamp << " ==========" << Qt::endl;
        }

        out << plainTextEdit->toPlainText() << Qt::endl;
        file.close();
        qDebug() << "QUIC客户端日志已保存至:" << fileName;
        if (this->isVisible()) {
            QMessageBox::information(this, "成功", QString("日志已保存至 %1").arg(fileName));
        }
    } else {
        qWarning() << "QUIC客户端日志保存失败:" << file.errorString();
        if (this->isVisible()) {
            QMessageBox::critical(this, "错误", "保存失败: " + file.errorString());
        }
    }
}
