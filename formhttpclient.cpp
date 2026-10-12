#include "formhttpclient.h"
#include "ui_formhttpclient.h"

#include <QDateTime>
#include <QTimer>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QApplication>
#include <QDebug>

#include "errorhandler.h"
#include "inputvalidator.h"

// 功能：构造函数，初始化UI、网络管理器、自动测试定时器并恢复上次配置
FormHttpClient::FormHttpClient(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::FormHttpClient)
{
    ui->setupUi(this);

    // 布局微调：输入列占满剩余宽度，消息编辑器吸收多余高度
    ui->gridLayout_3->setColumnStretch(1, 1);
    ui->gridLayout_3->setRowStretch(7, 1);
    this->setWindowTitle("HTTP客户端");

    m_netManager = new QNetworkAccessManager(this); // 网络管理器，父对象负责回收

    m_autoTimer = new QTimer(this);                 // 自动测试定时器
    m_autoTimer->setInterval(1000);                 // 每秒发送一次请求
    connect(m_autoTimer, &QTimer::timeout, this, [this]() {
        if (m_connected) {
            sendRequest();
        }
    });

    ui->comboBox_HTTPClientIP->setEditable(true);   // 允许手动输入主机地址
    ui->pushButton_HTTPClientDisconnect->setEnabled(false);
    ui->pushButton_HTTPClientSendMsg->setEnabled(false);
    ui->checkBox_HTTPClientAutoTesting->setEnabled(false);

    // 恢复上次使用的IP与端口
    {
        QSettings settings;
        const QString lastIp = settings.value("HTTPClient/lastIp", "127.0.0.1").toString();
        const int lastPort = settings.value("HTTPClient/lastPort", 12345).toInt();

        int index = ui->comboBox_HTTPClientIP->findText(lastIp);
        if (index >= 0) {
            ui->comboBox_HTTPClientIP->setCurrentIndex(index);
        } else {
            ui->comboBox_HTTPClientIP->setEditText(lastIp);
        }

        if (lastPort >= ui->spinBox_HTTPClientPort->minimum() &&
            lastPort <= ui->spinBox_HTTPClientPort->maximum()) {
            ui->spinBox_HTTPClientPort->setValue(lastPort);
        }
    }

    // 与MQTT/TCP模块一致的浅色精致主题
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
        "QPlainTextEdit#plainTextEdit_HTTPClientMsg {"
        "  font-size: 12px;"
        "  font-family: Consolas, 'Microsoft YaHei UI';"
        "  background-color: #FFFFFF;"
        "  border: 1px solid #E2E8F0;"
        "  border-radius: 6px;"
        "  padding: 4px;"
        "}"
        "QPlainTextEdit#plainTextEdit_HTTPClientSendData {"
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
        "QPushButton#pushButton_HTTPClientConnect {"
        "  background-color: #10B981;"
        "  color: #FFFFFF;"
        "}"
        "QPushButton#pushButton_HTTPClientConnect:hover {"
        "  background-color: #059669;"
        "}"
        "QPushButton#pushButton_HTTPClientDisconnect {"
        "  background-color: #EF4444;"
        "  color: #FFFFFF;"
        "}"
        "QPushButton#pushButton_HTTPClientDisconnect:hover {"
        "  background-color: #DC2626;"
        "}"
        "QPushButton#pushButton_HTTPClientSendMsg {"
        "  background-color: #2563EB;"
        "  color: #FFFFFF;"
        "}"
        "QPushButton#pushButton_HTTPClientSendMsg:hover {"
        "  background-color: #1D4ED8;"
        "}"
        "QPushButton#pushButton_HTTPClientClose {"
        "  background-color: #F1F5F9;"
        "  color: #475569;"
        "  border: 1px solid #CBD5E1;"
        "}"
        "QPushButton#pushButton_HTTPClientClose:hover {"
        "  background-color: #E2E8F0;"
        "}"
        "QPushButton:disabled {"
        "  background-color: #E2E8F0;"
        "  color: #94A3B8;"
        "}"
        );
}

// 析构函数：中止未完成请求，释放UI
FormHttpClient::~FormHttpClient()
{
    abortPendingReplies();
    delete ui;
}

// 功能：连接服务器——校验IP/端口，设置目标地址并发GET测试请求
void FormHttpClient::on_pushButton_HTTPClientConnect_clicked()
{
    if (m_connected) {
        appendLog("[提示] 已连接目标服务器，无需重复连接");
        return;
    }

    QString ip = ui->comboBox_HTTPClientIP->currentText().trimmed();
    int port = ui->spinBox_HTTPClientPort->value();

    auto ipValidation = InputValidator::validatorNetworkAddress(ip);
    if (!ipValidation.isValid) {
        HANDLE_ERROR(ErrorHandler::ValidationError, ErrorHandler::Warning,
                     ipValidation.errorMessage, this);
        ui->comboBox_HTTPClientIP->setFocus();
        return;
    }

    auto portValidation = InputValidator::validatorPort(port);
    if (!portValidation.isValid) {
        HANDLE_ERROR(ErrorHandler::ValidationError, ErrorHandler::Warning,
                     portValidation.errorMessage, this);
        ui->spinBox_HTTPClientPort->setFocus();
        return;
    }

    m_baseUrl = QString("http://%1:%2").arg(ip).arg(port);

    {
        QSettings settings;                 // 持久化最新IP/端口
        settings.setValue("HTTPClient/lastIp", ip);
        settings.setValue("HTTPClient/lastPort", port);
        settings.sync();
    }

    m_connected = true;
    setCommunicationControls(true);

    appendLog(QString("[%1] 已设置目标服务器 %2")
                  .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                  .arg(m_baseUrl));

    // HTTP为无状态协议，"连接"即验证服务器可达：发送一次GET测试请求
    sendRequest();
}

// 功能：断开——中止所有未完成请求并复位UI
void FormHttpClient::on_pushButton_HTTPClientDisconnect_clicked()
{
    abortPendingReplies();

    if (m_autoTimer->isActive()) {
        m_autoTimer->stop();
        ui->checkBox_HTTPClientAutoTesting->setChecked(false);
    }

    m_connected = false;
    m_baseUrl.clear();
    setCommunicationControls(false);

    appendLog(QString("[%1] 已断开与目标服务器的连接")
                  .arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
}

// 功能：发送消息——按当前方法/路径/消息体发送一次HTTP请求
void FormHttpClient::on_pushButton_HTTPClientSendMsg_clicked()
{
    if (!m_connected) {
        HANDLE_ERROR(ErrorHandler::NetworkError, ErrorHandler::Warning,
                     "未连接服务器，无法发送。", this);
        return;
    }

    QString method = ui->comboBox_HTTPClientMethod->currentText();
    if ((method == "POST" || method == "PUT") &&
        ui->plainTextEdit_HTTPClientSendData->toPlainText().trimmed().isEmpty()) {
        HANDLE_ERROR(ErrorHandler::ValidationError, ErrorHandler::Warning,
                     QString("%1请求的消息体不能为空！").arg(method), this);
        return;
    }

    sendRequest();
}

// 功能：发送一次HTTP请求（供手动发送与自动测试共用）
void FormHttpClient::sendRequest()
{
    if (m_baseUrl.isEmpty() || !m_connected) {
        return;
    }

    QString method = ui->comboBox_HTTPClientMethod->currentText();
    QString path = ui->lineEdit_HTTPClientPath->text().trimmed();
    if (path.isEmpty()) {
        path = "/";
    } else if (!path.startsWith('/')) {
        path.prepend('/');
    }

    QNetworkRequest req(QUrl(m_baseUrl + path));
    QByteArray body;
    if (method == "POST" || method == "PUT") {
        body = ui->plainTextEdit_HTTPClientSendData->toPlainText().toUtf8();
        req.setHeader(QNetworkRequest::ContentTypeHeader, "text/plain; charset=utf-8");
    }

    QNetworkReply *reply = nullptr;
    if (method == "GET") {
        reply = m_netManager->get(req);
    } else if (method == "POST") {
        reply = m_netManager->post(req, body);
    } else if (method == "PUT") {
        reply = m_netManager->put(req, body);
    } else if (method == "DELETE") {
        reply = m_netManager->deleteResource(req);
    } else {
        HANDLE_ERROR(ErrorHandler::ValidationError, ErrorHandler::Warning,
                     "不支持的HTTP方法: " + method, this);
        return;
    }

    m_pendingReplies.append(reply);

    QString log = QString("[%1] 发送请求: %2 %3")
                      .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                      .arg(method, req.url().toString());
    if (!body.isEmpty()) {
        log += QString(" (%1字节)").arg(body.size());
    }
    appendLog(log);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        m_pendingReplies.removeOne(reply);

        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray resp = reply->readAll();
            appendLog(QString("[%1] 收到响应: HTTP %2 (%3字节)")
                          .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                          .arg(status)
                          .arg(resp.size()));
            appendLog(QString::fromUtf8(resp));
        } else {
            appendLog(QString("[%1] 请求失败: %2")
                          .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                          .arg(reply->errorString()));
        }
        reply->deleteLater();
    });
}

// 功能：关闭窗口——保存日志并退出程序
void FormHttpClient::on_pushButton_HTTPClientClose_clicked()
{
    saveLog();
    QCoreApplication::instance()->quit();
}

// 功能：自动测试开关——勾选后每秒自动发送一次请求
void FormHttpClient::on_checkBox_HTTPClientAutoTesting_clicked()
{
    bool isAutoTesting = ui->checkBox_HTTPClientAutoTesting->isChecked();

    if (isAutoTesting) {
        if (!m_connected) {
            HANDLE_ERROR(ErrorHandler::NetworkError, ErrorHandler::Warning,
                         "未连接服务器，无法开始自动测试。", this);
            ui->checkBox_HTTPClientAutoTesting->setChecked(false);
            return;
        }
        m_autoTimer->start();
        appendLog("[提示] 自动测试已开启，每秒自动发送一次请求");
    } else {
        m_autoTimer->stop();
        appendLog("[提示] 自动测试已停止");
    }
}

// 功能：中止所有未完成的请求（断开/析构时调用）
void FormHttpClient::abortPendingReplies()
{
    const QList<QNetworkReply *> replies = m_pendingReplies; // 拷贝，避免迭代中修改
    for (QNetworkReply *reply : replies) {
        if (reply) {
            reply->disconnect(this);    // 先断开信号，避免误记"请求失败"日志
            reply->abort();
            reply->deleteLater();
        }
    }
    m_pendingReplies.clear();
}

// 功能：批量更新与连接状态相关的按钮
void FormHttpClient::setCommunicationControls(bool connected)
{
    ui->pushButton_HTTPClientConnect->setEnabled(!connected);
    ui->pushButton_HTTPClientDisconnect->setEnabled(connected);
    ui->pushButton_HTTPClientSendMsg->setEnabled(connected);
    ui->checkBox_HTTPClientAutoTesting->setEnabled(connected);
}

// 功能：追加日志并自动滚动到底部
void FormHttpClient::appendLog(const QString &text)
{
    ui->plainTextEdit_HTTPClientMsg->appendPlainText(text);
    trimLog();
}

// 功能：裁剪日志，防止内容过长导致UI卡顿
void FormHttpClient::trimLog(int keepBlocks, int trimStep)
{
    static const int kKeepDefault = 1000;   // 默认保留block数
    static const int kTrimDefault = 200;    // 默认删除步长
    const int keep = keepBlocks > 0 ? keepBlocks : kKeepDefault;
    const int step = trimStep > 0 ? trimStep : kTrimDefault;

    QTextDocument *doc = ui->plainTextEdit_HTTPClientMsg->document();
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

// 功能：保存日志（public接口，供主窗口关闭时调用）
void FormHttpClient::saveLog()
{
    if (ui && ui->plainTextEdit_HTTPClientMsg) {
        savePlainTextEditToFile(ui->plainTextEdit_HTTPClientMsg);
    }
}

// 功能：将日志文本保存到文件（与TCP客户端一致的追加式日志）
void FormHttpClient::savePlainTextEditToFile(QPlainTextEdit *plainTextEdit)
{
    if (!plainTextEdit) {
        qWarning() << "FormHttpClient::savePlainTextEditToFile: plainTextEdit is null";
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

    QString fileName = QDir(appDataPath).filePath("HTTPClientLogfile.txt");

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
        qDebug() << "HTTP客户端日志已保存至:" << fileName;
        if (this->isVisible()) {
            QMessageBox::information(this, "成功", QString("日志已保存至 %1").arg(fileName));
        }
    } else {
        qWarning() << "HTTP客户端日志保存失败:" << file.errorString();
        if (this->isVisible()) {
            QMessageBox::critical(this, "错误", "保存失败: " + file.errorString());
        }
    }
}
