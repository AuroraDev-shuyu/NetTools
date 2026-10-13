#include "formquicserver.h"
#include "ui_formquicserver.h"

#include <QHostAddress>
#include <QNetworkInterface>
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
#include <QTextEdit>
#include <QTextCursor>

#include "errorhandler.h"
#include "inputvalidator.h"

// 功能：构造函数，初始化UI、填充本机IP列表、绑定封装层信号并恢复上次配置
FormQuicServer::FormQuicServer(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::FormQuicServer)
{
    ui->setupUi(this);
    this->setWindowTitle("QUIC服务器");

    // 布局微调：输入列占满剩余宽度，消息编辑器吸收多余高度
    ui->gridLayout_3->setColumnStretch(1, 1);
    ui->gridLayout_3->setRowStretch(4, 1);

    ui->comboBox_QUICServerIP->setEditable(true);     // 允许手动输入监听地址
    ui->pushButton_QUICServerStop->setEnabled(false); // 初始未运行时禁用"停止"

    // 枚举本机IPv4，填充下拉框
    QList<QHostAddress> addrList = QNetworkInterface::allAddresses();
    QStringList ipList;
    ipList.reserve(addrList.size());
    for (const QHostAddress &address : addrList) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol) {
            ipList.append(address.toString());
        }
    }

    // 批量添加IP地址，减少UI更新次数
    ui->comboBox_QUICServerIP->setUpdatesEnabled(false);
    ui->comboBox_QUICServerIP->addItems(ipList);
    ui->comboBox_QUICServerIP->setUpdatesEnabled(true);

    // 恢复上次使用的IP与端口
    {
        QSettings settings;
        const QString lastIp = settings.value("QUICServer/lastIp").toString();
        const int lastPort = settings.value("QUICServer/lastPort", 12345).toInt();

        if (!lastIp.isEmpty()) {
            int index = ui->comboBox_QUICServerIP->findText(lastIp);
            if (index >= 0) {
                ui->comboBox_QUICServerIP->setCurrentIndex(index);
            } else {
                ui->comboBox_QUICServerIP->setEditText(lastIp);
            }
        }

        if (lastPort >= ui->spinBox_QUICServerPort->minimum() &&
            lastPort <= ui->spinBox_QUICServerPort->maximum()) {
            ui->spinBox_QUICServerPort->setValue(lastPort);
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
        "QTextEdit#ListWidget_QUICServerListMsg {"
        "  font-size: 12px;"
        "  font-family: Consolas, 'Microsoft YaHei UI';"
        "  background-color: #FFFFFF;"
        "  border: 1px solid #E2E8F0;"
        "  border-radius: 6px;"
        "  padding: 4px;"
        "}"
        "QPlainTextEdit#plainTextEdit_QUICServerSendData {"
        "  font-size: 13px;"
        "  background-color: #FFFFFF;"
        "  border: 1px solid #CBD5E1;"
        "  border-radius: 6px;"
        "  padding: 4px;"
        "}"
        "QPushButton {"
        "  font-size: 13px;"
        "  border: none;"
        "  border-radius: 6px;"
        "  padding: 5px 12px;"
        "}"
        "QPushButton#pushButton_QUICServerStart {"
        "  background-color: #10B981;"
        "  color: #FFFFFF;"
        "}"
        "QPushButton#pushButton_QUICServerStart:hover {"
        "  background-color: #059669;"
        "}"
        "QPushButton#pushButton_QUICServerStop {"
        "  background-color: #EF4444;"
        "  color: #FFFFFF;"
        "}"
        "QPushButton#pushButton_QUICServerStop:hover {"
        "  background-color: #DC2626;"
        "}"
        "QPushButton#pushButton_QUICServerSendMsg {"
        "  background-color: #2563EB;"
        "  color: #FFFFFF;"
        "}"
        "QPushButton#pushButton_QUICServerSendMsg:hover {"
        "  background-color: #1D4ED8;"
        "}"
        "QPushButton#pushButton_QUICServerClose {"
        "  background-color: #F1F5F9;"
        "  color: #475569;"
        "  border: 1px solid #CBD5E1;"
        "}"
        "QPushButton#pushButton_QUICServerClose:hover {"
        "  background-color: #E2E8F0;"
        "}"
        "QPushButton:disabled {"
        "  background-color: #E2E8F0;"
        "  color: #94A3B8;"
        "}"
        );

    // 绑定封装层信号（回调线程emit，Qt跨线程自动queued到本线程）
    // 新连接/断开用灰色，收到数据用蓝色
    connect(&m_quicServer, &QuicServer::onNewConn, this, [this](const QString &peerAddr) {
        appendLog(QString("[%1] 新客户端接入: %2")
                      .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                      .arg(peerAddr));
    });

    connect(&m_quicServer, &QuicServer::onDataReceived, this, [this](const QString &data) {
        appendLog(QString("\n[%1] 接收: %2")
                      .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                      .arg(data),
                  QColor("#0078D4"));
    });

    connect(&m_quicServer, &QuicServer::onConnClosed, this, [this]() {
        appendLog(QString("[%1] 客户端断开")
                      .arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
    });
}

// 析构函数：先断开信号（避免成员析构时回调访问已释放的UI），再释放UI
FormQuicServer::~FormQuicServer()
{
    m_quicServer.disconnect(this);
    delete ui;
}

// 功能：启动服务器——校验IP/端口，持久化配置并开始QUIC监听
void FormQuicServer::on_pushButton_QUICServerStart_clicked()
{
    if (serverRunning) {
        appendLog("[提示] 服务器已在运行");
        return;
    }

    QString ip = ui->comboBox_QUICServerIP->currentText().trimmed();
    int port = ui->spinBox_QUICServerPort->value();

    auto ipValidation = InputValidator::validatorNetworkAddress(ip);
    if (!ipValidation.isValid) {
        HANDLE_ERROR(ErrorHandler::ValidationError, ErrorHandler::Warning,
                     ipValidation.errorMessage, this);
        ui->comboBox_QUICServerIP->setFocus();
        return;
    }

    auto portValidation = InputValidator::validatorPort(port);
    if (!portValidation.isValid) {
        HANDLE_ERROR(ErrorHandler::ValidationError, ErrorHandler::Warning,
                     portValidation.errorMessage, this);
        ui->spinBox_QUICServerPort->setFocus();
        return;
    }

    if (m_quicServer.start(port, ip)) {
        serverRunning = true;
        ui->pushButton_QUICServerStart->setEnabled(false);
        ui->pushButton_QUICServerStop->setEnabled(true);

        // 持久化配置
        {
            QSettings settings;
            settings.setValue("QUICServer/lastIp", ip);
            settings.setValue("QUICServer/lastPort", port);
            settings.sync();
        }

        appendLog(QString("[%1] QUIC服务器已启动: %2:%3")
                      .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                      .arg(ip)
                      .arg(port));
    } else {
        appendLog(QString("[%1] [错误] 启动失败: %2")
                      .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                      .arg(m_quicServer.lastError()),
                  QColor("#CC0000"));
        QMessageBox::warning(this, "错误",
                             QString("QUIC服务器启动失败!\n%1").arg(m_quicServer.lastError()));
    }
}

// 功能：停止服务器并断开所有客户端
void FormQuicServer::on_pushButton_QUICServerStop_clicked()
{
    if (!serverRunning) {
        appendLog("[提示] 服务器未在运行");
        return;
    }

    m_quicServer.stop();

    serverRunning = false;
    ui->pushButton_QUICServerStart->setEnabled(true);
    ui->pushButton_QUICServerStop->setEnabled(false);

    appendLog(QString("[%1] QUIC服务器已停止，所有客户端已断开")
                  .arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
}

// 功能：广播消息——校验服务器运行/内容非空/存在客户端后群发
void FormQuicServer::on_pushButton_QUICServerSendMsg_clicked()
{
    if (!serverRunning) {
        HANDLE_ERROR(ErrorHandler::NetworkError, ErrorHandler::Warning,
                     "服务器未启动，无法发送消息。", this);
        return;
    }

    QString message = ui->plainTextEdit_QUICServerSendData->toPlainText().trimmed();
    if (message.isEmpty()) {
        HANDLE_ERROR(ErrorHandler::ValidationError, ErrorHandler::Warning,
                     "发送内容不能为空！", this);
        return;
    }

    if (m_quicServer.clientCount() == 0) {
        HANDLE_ERROR(ErrorHandler::NetworkError, ErrorHandler::Warning,
                     "当前没有任何已连接的客户端，无法发送测试消息。", this);
        return;
    }

    m_quicServer.sendToAll(message);

    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    appendLog(QString("\n[%1] 服务器广播: %2").arg(timestamp, message), QColor("#008000"));
}

// 功能：关闭窗口——保存日志并退出程序
void FormQuicServer::on_pushButton_QUICServerClose_clicked()
{
    saveLog();
    QCoreApplication::instance()->quit();
}

// 功能：追加彩色日志并自动滚动到底部
void FormQuicServer::appendLog(const QString &text, const QColor &color)
{
    QTextEdit *log = ui->ListWidget_QUICServerListMsg;
    QTextCursor cursor = log->textCursor();
    cursor.movePosition(QTextCursor::End);
    log->setTextCursor(cursor);
    log->setTextColor(color);
    log->append(text);
    trimLog();
}

// 功能：裁剪日志，防止内容过长导致UI卡顿
void FormQuicServer::trimLog(int keepBlocks, int trimStep)
{
    static const int kKeepDefault = 1000;   // 默认保留block数
    static const int kTrimDefault = 200;    // 默认删除步长
    const int keep = keepBlocks > 0 ? keepBlocks : kKeepDefault;
    const int step = trimStep > 0 ? trimStep : kTrimDefault;

    QTextDocument *doc = ui->ListWidget_QUICServerListMsg->document();
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

// 功能：窗口关闭事件——保存日志
void FormQuicServer::closeEvent(QCloseEvent *event)
{
    saveLog();
    event->accept();
}

// 功能：保存日志（public接口，供主窗口关闭时调用）
void FormQuicServer::saveLog()
{
    if (ui && ui->ListWidget_QUICServerListMsg) {
        saveTextEditToFile(ui->ListWidget_QUICServerListMsg);
    }
}

// 功能：将日志文本保存到文件（与其他模块一致的追加式日志）
void FormQuicServer::saveTextEditToFile(QTextEdit *textEdit)
{
    if (!textEdit) {
        qWarning() << "FormQuicServer::saveTextEditToFile: textEdit is null";
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

    QString fileName = QDir(appDataPath).filePath("QUICServerLogfile.txt");

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

        out << textEdit->toPlainText() << Qt::endl;
        file.close();
        qDebug() << "QUIC服务器日志已保存至:" << fileName;
        if (this->isVisible()) {
            QMessageBox::information(this, "成功", QString("日志已保存至 %1").arg(fileName));
        }
    } else {
        qWarning() << "QUIC服务器日志保存失败:" << file.errorString();
        if (this->isVisible()) {
            QMessageBox::critical(this, "错误", "保存失败: " + file.errorString());
        }
    }
}
