#include "formhttpserver.h"
#include "ui_formhttpserver.h"

#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QDateTime>
#include <QMetaEnum>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QApplication>
#include <QDebug>

#include "errorhandler.h"
#include "inputvalidator.h"

// 功能：构造函数，初始化UI、填充本机IP列表并恢复上次配置
FormHttpServer::FormHttpServer(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::FormHttpServer)
{
    ui->setupUi(this);

    // 布局微调：输入列占满剩余宽度，消息编辑器吸收多余高度
    ui->gridLayout_3->setColumnStretch(1, 1);
    ui->gridLayout_3->setRowStretch(4, 1);
    this->setWindowTitle("HTTP服务器");

    ui->comboBox_HTTPServerIP->setEditable(true);   // 允许手动输入监听地址
    ui->pushButton_HTTPServerStop->setEnabled(false); // 初始未运行时禁用"停止"

    // 默认响应内容，可在请求处理时实时修改
    ui->plainTextEdit_HTTPServerSendData->setPlainText("Hello from NetTools HTTP Server!");

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
    ui->comboBox_HTTPServerIP->setUpdatesEnabled(false);
    ui->comboBox_HTTPServerIP->addItems(ipList);
    ui->comboBox_HTTPServerIP->setUpdatesEnabled(true);

    // 恢复上次使用的IP与端口
    {
        QSettings settings;
        const QString lastIp = settings.value("HTTPServer/lastIp").toString();
        const int lastPort = settings.value("HTTPServer/lastPort", 12345).toInt();

        if (!lastIp.isEmpty()) {
            int index = ui->comboBox_HTTPServerIP->findText(lastIp);
            if (index >= 0) {
                ui->comboBox_HTTPServerIP->setCurrentIndex(index);
            } else {
                ui->comboBox_HTTPServerIP->setEditText(lastIp);
            }
        }

        if (lastPort >= ui->spinBox_HTTPServerPort->minimum() &&
            lastPort <= ui->spinBox_HTTPServerPort->maximum()) {
            ui->spinBox_HTTPServerPort->setValue(lastPort);
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
        "QComboBox, QSpinBox {"
        "  font-size: 13px;"
        "  background-color: #FFFFFF;"
        "  border: 1px solid #CBD5E1;"
        "  border-radius: 6px;"
        "  padding: 4px 8px;"
        "  min-height: 20px;"
        "}"
        "QComboBox:hover, QSpinBox:hover {"
        "  border-color: #94A3B8;"
        "}"
        "QComboBox:focus, QSpinBox:focus {"
        "  border-color: #3B82F6;"
        "}"
        "QTextEdit#ListWidget_HTTPServerListMsg {"
        "  font-size: 12px;"
        "  font-family: Consolas, 'Microsoft YaHei UI';"
        "  background-color: #FFFFFF;"
        "  border: 1px solid #E2E8F0;"
        "  border-radius: 6px;"
        "  padding: 4px;"
        "}"
        "QPlainTextEdit#plainTextEdit_HTTPServerSendData {"
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
        "QPushButton#pushButton_HTTPServerStart {"
        "  background-color: #10B981;"
        "  color: #FFFFFF;"
        "}"
        "QPushButton#pushButton_HTTPServerStart:hover {"
        "  background-color: #059669;"
        "}"
        "QPushButton#pushButton_HTTPServerStop {"
        "  background-color: #EF4444;"
        "  color: #FFFFFF;"
        "}"
        "QPushButton#pushButton_HTTPServerStop:hover {"
        "  background-color: #DC2626;"
        "}"
        "QPushButton#pushButton_HTTPServerSendMsg {"
        "  background-color: #2563EB;"
        "  color: #FFFFFF;"
        "}"
        "QPushButton#pushButton_HTTPServerSendMsg:hover {"
        "  background-color: #1D4ED8;"
        "}"
        "QPushButton#pushButton_HTTPServerClose {"
        "  background-color: #F1F5F9;"
        "  color: #475569;"
        "  border: 1px solid #CBD5E1;"
        "}"
        "QPushButton#pushButton_HTTPServerClose:hover {"
        "  background-color: #E2E8F0;"
        "}"
        "QPushButton:disabled {"
        "  background-color: #E2E8F0;"
        "  color: #94A3B8;"
        "}"
        );
}

// 析构函数：销毁HTTP服务器实例（自动停止监听）并释放UI
FormHttpServer::~FormHttpServer()
{
    if (m_httpServer) {
        delete m_httpServer;
        m_httpServer = nullptr;
    }
    delete ui;
}

// 功能：启动监听——校验IP/端口，注册路由并开始监听
void FormHttpServer::on_pushButton_HTTPServerStart_clicked()
{
    if (m_serverRunning) {
        appendLog("[提示] 服务器已在运行");
        return;
    }

    QString ip = ui->comboBox_HTTPServerIP->currentText().trimmed();
    int port = ui->spinBox_HTTPServerPort->value();

    auto ipValidation = InputValidator::validatorNetworkAddress(ip);
    if (!ipValidation.isValid) {
        HANDLE_ERROR(ErrorHandler::ValidationError, ErrorHandler::Warning,
                     ipValidation.errorMessage, this);
        ui->comboBox_HTTPServerIP->setFocus();
        return;
    }

    auto portValidation = InputValidator::validatorPort(port);
    if (!portValidation.isValid) {
        HANDLE_ERROR(ErrorHandler::ValidationError, ErrorHandler::Warning,
                     portValidation.errorMessage, this);
        ui->spinBox_HTTPServerPort->setFocus();
        return;
    }

    // 每次启动创建新的服务器实例；Qt 6.5的QHttpServer没有close接口，
    // 停止监听通过销毁实例实现
    if (m_httpServer) {
        delete m_httpServer;
        m_httpServer = nullptr;
    }
    m_httpServer = new QHttpServer(this);

    // 注册根路径路由
    m_httpServer->route("/", [this](const QHttpServerRequest &request) {
        return makeResponse(request);
    });

    // 其余路径统一由missingHandler处理，保证任何路径都有响应
    m_httpServer->setMissingHandler([this](const QHttpServerRequest &request,
                                           QHttpServerResponder &&responder) {
        appendRequestLog(request);
        responder.write(ui->plainTextEdit_HTTPServerSendData->toPlainText().toUtf8(),
                        "text/plain; charset=utf-8",
                        QHttpServerResponder::StatusCode::Ok);
    });

    // 开始监听，返回0表示失败（如端口被占用）
    quint16 boundPort = m_httpServer->listen(QHostAddress(ip), static_cast<quint16>(port));
    if (boundPort == 0) {
        appendLog(QString("[%1] [错误] 启动失败: 端口 %2 可能被占用")
                      .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                      .arg(port));
        delete m_httpServer;
        m_httpServer = nullptr;
        QMessageBox::warning(this, "错误",
                             QString("HTTP服务器启动失败!\n请检查端口 %1 是否被占用。").arg(port));
        return;
    }

    m_serverRunning = true;
    ui->pushButton_HTTPServerStart->setEnabled(false);
    ui->pushButton_HTTPServerStop->setEnabled(true);

    // 持久化配置
    {
        QSettings settings;
        settings.setValue("HTTPServer/lastIp", ip);
        settings.setValue("HTTPServer/lastPort", port);
        settings.sync();
    }

    appendLog(QString("[%1] HTTP服务器已启动: http://%2:%3")
                  .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                  .arg(ip)
                  .arg(port));
}

// 功能：停止监听——销毁服务器实例
void FormHttpServer::on_pushButton_HTTPServerStop_clicked()
{
    if (!m_serverRunning) {
        appendLog("[提示] 服务器未在运行");
        return;
    }

    if (m_httpServer) {
        delete m_httpServer;    // 销毁实例即停止监听
        m_httpServer = nullptr;
    }

    m_serverRunning = false;
    ui->pushButton_HTTPServerStart->setEnabled(true);
    ui->pushButton_HTTPServerStop->setEnabled(false);

    appendLog(QString("[%1] HTTP服务器已停止")
                  .arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
}

// 功能：应用响应内容——HTTP服务器为被动响应模式，
// 输入框中的内容将在每次请求时作为响应体返回
void FormHttpServer::on_pushButton_HTTPServerSendMsg_clicked()
{
    QString text = ui->plainTextEdit_HTTPServerSendData->toPlainText().trimmed();
    if (text.isEmpty()) {
        HANDLE_ERROR(ErrorHandler::ValidationError, ErrorHandler::Warning,
                     "响应内容不能为空！", this);
        return;
    }

    appendLog(QString("[%1] 响应内容已应用(%2字节)，客户端下次请求时将收到该内容")
                  .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                  .arg(text.toUtf8().size()));
}

// 功能：关闭窗口——保存日志并退出程序
void FormHttpServer::on_pushButton_HTTPServerClose_clicked()
{
    saveLog();
    QCoreApplication::instance()->quit();
}

// 功能：构造HTTP响应——实时读取响应内容输入框
QHttpServerResponse FormHttpServer::makeResponse(const QHttpServerRequest &request)
{
    appendRequestLog(request);
    QByteArray body = ui->plainTextEdit_HTTPServerSendData->toPlainText().toUtf8();
    return QHttpServerResponse(body, QHttpServerResponse::StatusCode::Ok);
}

// 功能：记录请求日志——方法、路径、客户端地址与请求体
void FormHttpServer::appendRequestLog(const QHttpServerRequest &request)
{
    QString methodStr = QString::fromLatin1(
        QMetaEnum::fromType<QHttpServerRequest::Method>().valueToKey(
            static_cast<int>(request.method())));
    QString path = request.url().path();
    QString client = request.remoteAddress().toString();

    appendLog(QString("[%1] 收到请求: %2 %3 (来自 %4)")
                  .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                  .arg(methodStr, path, client));

    const QByteArray body = request.body();
    if (!body.isEmpty()) {
        appendLog(QString("  请求体(%1字节): %2")
                      .arg(body.size())
                      .arg(QString::fromUtf8(body).left(300)));
    }
}

// 功能：追加日志并自动滚动到底部
void FormHttpServer::appendLog(const QString &text)
{
    ui->ListWidget_HTTPServerListMsg->append(text);
    trimLog();
}

// 功能：裁剪日志，防止内容过长导致UI卡顿
void FormHttpServer::trimLog(int keepBlocks, int trimStep)
{
    static const int kKeepDefault = 1000;   // 默认保留block数
    static const int kTrimDefault = 200;    // 默认删除步长
    const int keep = keepBlocks > 0 ? keepBlocks : kKeepDefault;
    const int step = trimStep > 0 ? trimStep : kTrimDefault;

    QTextDocument *doc = ui->ListWidget_HTTPServerListMsg->document();
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
void FormHttpServer::saveLog()
{
    if (ui && ui->ListWidget_HTTPServerListMsg) {
        saveTextEditToFile(ui->ListWidget_HTTPServerListMsg);
    }
}

// 功能：将日志文本保存到文件（与TCP服务器一致的追加式日志）
void FormHttpServer::saveTextEditToFile(QTextEdit *textEdit)
{
    if (!textEdit) {
        qWarning() << "FormHttpServer::saveTextEditToFile: textEdit is null";
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

    QString fileName = QDir(appDataPath).filePath("HTTPServerLogfile.txt");

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
        qDebug() << "HTTP服务器日志已保存至:" << fileName;
        if (this->isVisible()) {
            QMessageBox::information(this, "成功", QString("日志已保存至 %1").arg(fileName));
        }
    } else {
        qWarning() << "HTTP服务器日志保存失败:" << file.errorString();
        if (this->isVisible()) {
            QMessageBox::critical(this, "错误", "保存失败: " + file.errorString());
        }
    }
}
