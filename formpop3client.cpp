#include "formpop3client.h"
#include "ui_formpop3client.h"

#include <QSslSocket>
#include <QTimer>
#include <QDateTime>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QApplication>
#include <QCloseEvent>
#include <QPlainTextEdit>
#include <QList>
#include <QDebug>

#include "pop3protocol.h"
#include "errorhandler.h"
#include "inputvalidator.h"

// 功能：构造函数，初始化UI、保活定时器、快捷按钮并恢复上次配置
FormPop3Client::FormPop3Client(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::FormPop3Client)
{
    ui->setupUi(this);
    this->setWindowTitle("POP3客户端");

    // 布局微调：参数区第二列吸收多余宽度
    ui->gridLayout_3->setColumnStretch(1, 1);

    ui->comboBox_POP3ClientIP->setEditable(true);   // 允许手动输入服务器地址
    ui->pushButton_POP3ClientDisconnect->setEnabled(false);
    ui->pushButton_POP3ClientSendMsg->setEnabled(false);
    ui->checkBox_POP3ClientAutoTesting->setEnabled(false);

    // 快捷命令按钮统一样式标记（QSS 用属性选择器）
    const QList<QPushButton *> quickButtons = {
        ui->pushButton_POP3ClientStat, ui->pushButton_POP3ClientList, ui->pushButton_POP3ClientUidl,
        ui->pushButton_POP3ClientRetr,  ui->pushButton_POP3ClientDele,  ui->pushButton_POP3ClientTop,
        ui->pushButton_POP3ClientNoop,  ui->pushButton_POP3ClientRset,  ui->pushButton_POP3ClientQuit
    };
    for (QPushButton *btn : quickButtons) {
        btn->setProperty("quickCmd", true);
    }

    // 自动保活定时器：30秒发送一次 NOOP，防止服务器空闲超时断开
    m_keepAliveTimer = new QTimer(this);
    m_keepAliveTimer->setInterval(30000);
    connect(m_keepAliveTimer, &QTimer::timeout, this, [this]() {
        if (m_connected) {
            sendCommand("NOOP");
        }
    });

    // 快捷命令按钮：无参命令直接发送；RETR/DELE/TOP 拼接邮件序号
    connect(ui->pushButton_POP3ClientStat, &QPushButton::clicked, this, [this]() { sendQuickCmd("STAT"); });
    connect(ui->pushButton_POP3ClientList, &QPushButton::clicked, this, [this]() { sendQuickCmd("LIST"); });
    connect(ui->pushButton_POP3ClientUidl, &QPushButton::clicked, this, [this]() { sendQuickCmd("UIDL"); });
    connect(ui->pushButton_POP3ClientNoop, &QPushButton::clicked, this, [this]() { sendQuickCmd("NOOP"); });
    connect(ui->pushButton_POP3ClientRset, &QPushButton::clicked, this, [this]() { sendQuickCmd("RSET"); });
    connect(ui->pushButton_POP3ClientQuit, &QPushButton::clicked, this, [this]() { sendQuickCmd("QUIT"); });
    connect(ui->pushButton_POP3ClientRetr, &QPushButton::clicked, this, [this]() {
        sendQuickCmd(QByteArray("RETR ") + QByteArray::number(ui->spinBox_POP3ClientMsgNo->value()));
    });
    connect(ui->pushButton_POP3ClientDele, &QPushButton::clicked, this, [this]() {
        sendQuickCmd(QByteArray("DELE ") + QByteArray::number(ui->spinBox_POP3ClientMsgNo->value()));
    });
    connect(ui->pushButton_POP3ClientTop, &QPushButton::clicked, this, [this]() {
        sendQuickCmd(QByteArray("TOP ") + QByteArray::number(ui->spinBox_POP3ClientMsgNo->value()) + " 10");
    });

    // 恢复上次使用的IP/端口/SSL/用户名（先恢复SSL勾选再恢复端口，避免联动切换覆盖已存端口）
    {
        QSettings settings;
        const QString lastIp = settings.value("POP3Client/lastIp", "127.0.0.1").toString();
        const int lastPort = settings.value("POP3Client/lastPort", 110).toInt();
        const bool lastSsl = settings.value("POP3Client/lastSsl", false).toBool();
        const QString lastUser = settings.value("POP3Client/lastUser", "test").toString();

        int index = ui->comboBox_POP3ClientIP->findText(lastIp);
        if (index >= 0) {
            ui->comboBox_POP3ClientIP->setCurrentIndex(index);
        } else {
            ui->comboBox_POP3ClientIP->setEditText(lastIp);
        }

        ui->checkBox_POP3ClientSsl->setChecked(lastSsl);
        if (lastPort >= ui->spinBox_POP3ClientPort->minimum() &&
            lastPort <= ui->spinBox_POP3ClientPort->maximum()) {
            ui->spinBox_POP3ClientPort->setValue(lastPort);
        }
        ui->lineEdit_POP3ClientUser->setText(lastUser);
    }

    // 浅色精致主题：与MQTT/TCP表单一致的统一样式
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
        "QPlainTextEdit#plainTextEdit_POP3ClientMsg {"
        "  font-size: 12px;"
        "  font-family: Consolas, 'Microsoft YaHei UI';"
        "  background-color: #FFFFFF;"
        "  border: 1px solid #E2E8F0;"
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
        "QPushButton[quickCmd=\"true\"] {"
        "  background-color: #2563EB;"
        "  color: #FFFFFF;"
        "  font-size: 13px;"
        "  font-weight: bold;"
        "  font-family: Consolas, 'Microsoft YaHei UI';"
        "  padding: 6px 10px;"
        "}"
        "QPushButton[quickCmd=\"true\"]:hover {"
        "  background-color: #1D4ED8;"
        "}"
        "QPushButton#pushButton_POP3ClientConnect {"
        "  background-color: #10B981;"
        "  color: #FFFFFF;"
        "}"
        "QPushButton#pushButton_POP3ClientConnect:hover {"
        "  background-color: #059669;"
        "}"
        "QPushButton#pushButton_POP3ClientDisconnect {"
        "  background-color: #EF4444;"
        "  color: #FFFFFF;"
        "}"
        "QPushButton#pushButton_POP3ClientDisconnect:hover {"
        "  background-color: #DC2626;"
        "}"
        "QPushButton#pushButton_POP3ClientSendMsg {"
        "  background-color: #2563EB;"
        "  color: #FFFFFF;"
        "}"
        "QPushButton#pushButton_POP3ClientSendMsg:hover {"
        "  background-color: #1D4ED8;"
        "}"
        "QPushButton#pushButton_POP3ClientClose {"
        "  background-color: #F1F5F9;"
        "  color: #475569;"
        "  border: 1px solid #CBD5E1;"
        "}"
        "QPushButton#pushButton_POP3ClientClose:hover {"
        "  background-color: #E2E8F0;"
        "}"
        "QPushButton:disabled {"
        "  background-color: #E2E8F0;"
        "  color: #94A3B8;"
        "}"
        );
}

// 析构函数：优雅结束会话并释放UI
FormPop3Client::~FormPop3Client()
{
    if (m_socket) {
        if (m_socket->state() == QAbstractSocket::ConnectedState) {
            sendCommand("QUIT"); // 尽量优雅结束POP3会话
        }
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    delete ui;
}

// 功能：连接POP3服务器——校验IP/端口/SSL可用性，建立连接后等待欢迎语自动登录
void FormPop3Client::on_pushButton_POP3ClientConnect_clicked()
{
    if (m_connected) {
        appendLog("[提示] 已连接服务器，无需重复连接", QColor("#666666"));
        return;
    }

    const QString ip = ui->comboBox_POP3ClientIP->currentText().trimmed();
    const int port = ui->spinBox_POP3ClientPort->value();
    const bool useSsl = ui->checkBox_POP3ClientSsl->isChecked();

    const auto ipValidation = InputValidator::validatorNetworkAddress(ip);
    if (!ipValidation.isValid) {
        HANDLE_ERROR(ErrorHandler::ValidationError, ErrorHandler::Warning,
                     ipValidation.errorMessage, this);
        ui->comboBox_POP3ClientIP->setFocus();
        return;
    }

    const auto portValidation = InputValidator::validatorPort(port);
    if (!portValidation.isValid) {
        HANDLE_ERROR(ErrorHandler::ValidationError, ErrorHandler::Warning,
                     portValidation.errorMessage, this);
        ui->spinBox_POP3ClientPort->setFocus();
        return;
    }

    // SSL 前置检查：本机Qt安装未带OpenSSL运行库时明确报错，而非静默失败
    if (useSsl && !QSslSocket::supportsSsl()) {
        HANDLE_ERROR(ErrorHandler::NetworkError, ErrorHandler::Critical,
                     "当前环境缺少 OpenSSL 3.x 运行库，无法建立SSL加密连接。\n"
                     "可将 C:\\Program Files\\Git\\mingw64\\bin 目录下的\n"
                     "libssl-3-x64.dll 与 libcrypto-3-x64.dll 拷贝到\n"
                     "NetTools.exe 同目录，或安装 Win64 OpenSSL 后重试。", this);
        return;
    }

    m_user = ui->lineEdit_POP3ClientUser->text().trimmed();
    m_password = ui->lineEdit_POP3ClientPass->text();

    {
        QSettings settings;                 // 持久化最新配置（密码不落盘）
        settings.setValue("POP3Client/lastIp", ip);
        settings.setValue("POP3Client/lastPort", port);
        settings.setValue("POP3Client/lastSsl", useSsl);
        settings.setValue("POP3Client/lastUser", m_user);
        settings.sync();
    }

    // 复用/重建套接字
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
    }
    m_socket = new QSslSocket(this);
    m_recvBuf.clear();
    m_inMultiLine = false;
    m_expectMultiLine = false;

    connect(m_socket, &QSslSocket::connected, this, [this]() {
        m_connected = true;
        appendLog(QString("[%1] TCP连接已建立，等待服务器欢迎语...")
                      .arg(QDateTime::currentDateTime().toString("hh:mm:ss")), QColor("#666666"));
    });

    connect(m_socket, &QSslSocket::encrypted, this, [this]() {
        appendLog(QString("[%1] SSL加密握手完成")
                      .arg(QDateTime::currentDateTime().toString("hh:mm:ss")), QColor("#008000"));
    });

    connect(m_socket, &QSslSocket::readyRead, this, [this]() {
        m_recvBuf.append(m_socket->readAll());
        parseResponses();
    });

    connect(m_socket, &QSslSocket::disconnected, this, [this]() {
        if (m_connected) {
            appendLog(QString("[%1] 与服务器的连接已断开")
                          .arg(QDateTime::currentDateTime().toString("hh:mm:ss")), QColor("#666666"));
        }
        resetConnectionState();
    });

    connect(m_socket, &QSslSocket::errorOccurred, this, [this](QAbstractSocket::SocketError error) {
        if (error == QAbstractSocket::RemoteHostClosedError) {
            return; // 远端正常关闭，disconnected信号中已处理
        }
        appendLog(QString("[%1] 连接错误: %2")
                      .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                      .arg(m_socket->errorString()), QColor("#CC0000"));
        resetConnectionState();
    });

    m_connected = false;
    m_loggedIn = false;
    m_loginStep = m_user.isEmpty() ? LoginStep::None : LoginStep::WaitingGreeting;
    ui->pushButton_POP3ClientConnect->setEnabled(false);
    ui->pushButton_POP3ClientDisconnect->setEnabled(true);
    ui->checkBox_POP3ClientSsl->setEnabled(false);

    appendLog(QString("[%1] 正在连接POP3服务器 %2:%3 %4...")
                  .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                  .arg(ip)
                  .arg(port)
                  .arg(useSsl ? QString("(SSL)") : QString("(明文)")), QColor("#666666"));

    if (useSsl) {
        m_socket->setPeerVerifyMode(QSslSocket::VerifyNone); // 调试助手：跳过证书校验
        appendLog("[提示] 调试模式已关闭证书校验", QColor("#666666"));
        m_socket->connectToHostEncrypted(ip, static_cast<quint16>(port));
    } else {
        m_socket->connectToHost(ip, static_cast<quint16>(port));
    }
}

// 功能：断开——发送QUIT结束会话并关闭连接
void FormPop3Client::on_pushButton_POP3ClientDisconnect_clicked()
{
    if (m_keepAliveTimer->isActive()) {
        m_keepAliveTimer->stop();
        ui->checkBox_POP3ClientAutoTesting->setChecked(false);
    }

    if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState) {
        sendCommand("QUIT"); // 优雅结束POP3会话
        m_socket->disconnectFromHost();
    }
    resetConnectionState();
    appendLog(QString("[%1] 已主动断开与服务器的连接")
                  .arg(QDateTime::currentDateTime().toString("hh:mm:ss")), QColor("#666666"));
}

// 功能：发送命令——原始命令框内容发送到服务器
void FormPop3Client::on_pushButton_POP3ClientSendMsg_clicked()
{
    const QString cmdText = ui->lineEdit_POP3ClientCmd->text().trimmed();
    if (cmdText.isEmpty()) {
        HANDLE_ERROR(ErrorHandler::ValidationError, ErrorHandler::Warning,
                     "命令不能为空！", this);
        ui->lineEdit_POP3ClientCmd->setFocus();
        return;
    }
    if (!m_connected) {
        HANDLE_ERROR(ErrorHandler::NetworkError, ErrorHandler::Warning,
                     "未连接服务器，无法发送命令。", this);
        return;
    }

    const QByteArray cmd = cmdText.toUtf8();
    // 与快捷按钮一致：LIST/RETR/UIDL/TOP/CAPA 后面跟多行响应，提前标记预期
    const QByteArray upper = cmd.toUpper();
    if (upper.startsWith("LIST") || upper.startsWith("RETR") || upper.startsWith("UIDL")
        || upper.startsWith("TOP") || upper.startsWith("CAPA")) {
        m_expectMultiLine = true;
    }
    sendCommand(cmd);
    ui->lineEdit_POP3ClientCmd->clear();
}

// 功能：关闭窗口——保存日志并退出程序
void FormPop3Client::on_pushButton_POP3ClientClose_clicked()
{
    saveLog();
    QCoreApplication::instance()->quit();
}

// 功能：自动保活开关——勾选后每30秒发送一次NOOP
void FormPop3Client::on_checkBox_POP3ClientAutoTesting_clicked()
{
    const bool isAuto = ui->checkBox_POP3ClientAutoTesting->isChecked();

    if (isAuto) {
        if (!m_connected) {
            HANDLE_ERROR(ErrorHandler::NetworkError, ErrorHandler::Warning,
                         "未连接服务器，无法开始自动保活。", this);
            ui->checkBox_POP3ClientAutoTesting->setChecked(false);
            return;
        }
        m_keepAliveTimer->start();
        appendLog("[提示] 自动保活已开启，每30秒发送一次NOOP", QColor("#666666"));
    } else {
        m_keepAliveTimer->stop();
        appendLog("[提示] 自动保活已停止", QColor("#666666"));
    }
}

// 功能：SSL勾选联动——110/995默认端口自动切换，并持久化
void FormPop3Client::on_checkBox_POP3ClientSsl_toggled(bool checked)
{
    if (checked && ui->spinBox_POP3ClientPort->value() == 110) {
        ui->spinBox_POP3ClientPort->setValue(995);
    } else if (!checked && ui->spinBox_POP3ClientPort->value() == 995) {
        ui->spinBox_POP3ClientPort->setValue(110);
    }

    QSettings settings;
    settings.setValue("POP3Client/lastSsl", checked);
    settings.sync();
}

// 功能：发送命令——补CRLF、写套接字、记[发送]日志（PASS命令脱敏）
void FormPop3Client::sendCommand(const QByteArray &cmd)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }
    m_socket->write(cmd + "\r\n");

    QString display = QString::fromUtf8(cmd);
    if (display.startsWith("PASS", Qt::CaseInsensitive)) {
        display = QString("PASS ****"); // 密码脱敏
    }
    appendLog(QString("[%1] [发送] %2")
                  .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                  .arg(display), QColor("#2563EB"));
}

// 功能：快捷按钮发送——带多行响应预期标记
void FormPop3Client::sendQuickCmd(const QByteArray &cmd)
{
    if (!m_connected) {
        HANDLE_ERROR(ErrorHandler::NetworkError, ErrorHandler::Warning,
                     "未连接服务器，无法发送命令。", this);
        return;
    }

    const QByteArray upper = cmd.toUpper();
    if (upper.startsWith("LIST") || upper.startsWith("RETR") || upper.startsWith("UIDL")
        || upper.startsWith("TOP") || upper.startsWith("CAPA")) {
        m_expectMultiLine = true;
    }
    sendCommand(cmd);
}

// 功能：推进自动登录步骤（USER/PASS）
void FormPop3Client::handleLoginProgress(bool ok, const QByteArray &line)
{
    switch (m_loginStep) {
    case LoginStep::WaitingGreeting:
        if (ok) {
            m_loginStep = LoginStep::WaitingUserOk;
            sendCommand("USER " + m_user.toUtf8());
        } else {
            m_loginStep = LoginStep::None; // 欢迎语为-ERR，放弃自动登录
        }
        break;
    case LoginStep::WaitingUserOk:
        if (ok) {
            m_loginStep = LoginStep::LoggedIn;
            sendCommand("PASS " + m_password.toUtf8()); // 日志中已脱敏
        } else {
            m_loginStep = LoginStep::None;
            appendLog(QString("[提示] 用户名未被接受: %1，可手动输入 USER/PASS 登录")
                          .arg(QString::fromUtf8(line)), QColor("#666666"));
        }
        break;
    case LoginStep::LoggedIn:
        if (ok) {
            m_loggedIn = true;
            m_loginStep = LoginStep::None;
            appendLog(QString("[%1] 登录成功，进入事务状态（TRANSACTION）")
                          .arg(QDateTime::currentDateTime().toString("hh:mm:ss")), QColor("#008000"));
        } else {
            m_loginStep = LoginStep::None;
            appendLog(QString("[提示] 登录失败: %1，可手动输入 USER/PASS 重试")
                          .arg(QString::fromUtf8(line)), QColor("#CC0000"));
        }
        break;
    case LoginStep::None:
    default:
        break;
    }
}

// 功能：解析接收缓冲区——单行/多行响应状态机（RFC 1939）
void FormPop3Client::parseResponses()
{
    while (true) {
        const Pop3Protocol::FeedResult fr = Pop3Protocol::feedLine(m_recvBuf);
        if (!fr.hasLine) {
            break; // 等待更多数据
        }
        const QByteArray line = fr.line;

        if (m_inMultiLine) {
            // 多行响应正文：终止行 "." 结束，其余行做点还原后按正文记录
            if (Pop3Protocol::isDotTerminator(line)) {
                m_inMultiLine = false;
                continue;
            }
            appendLog(QString::fromUtf8(Pop3Protocol::unstuffDotLine(line)));
            continue;
        }

        // 单行响应分类
        if (Pop3Protocol::isOkLine(line)) {
            appendLog(QString("[%1] [接收] %2")
                          .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                          .arg(QString::fromUtf8(line)), QColor("#008000"));
            if (m_expectMultiLine) {
                m_expectMultiLine = false;
                m_inMultiLine = true; // +OK 起始，进入多行态
                continue;
            }
            handleLoginProgress(true, line);
        } else if (Pop3Protocol::isErrLine(line)) {
            m_expectMultiLine = false; // -ERR 后不跟多行数据
            appendLog(QString("[%1] [接收] %2")
                          .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                          .arg(QString::fromUtf8(line)), QColor("#CC0000"));
            handleLoginProgress(false, line);
        } else {
            // 非标准行（服务器扩展输出），按普通文本记录
            appendLog(QString("[%1] [接收] %2")
                          .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                          .arg(QString::fromUtf8(line)), QColor("#666666"));
        }
    }
}

// 功能：复位连接状态与UI
void FormPop3Client::resetConnectionState()
{
    m_connected = false;
    m_loggedIn = false;
    m_inMultiLine = false;
    m_expectMultiLine = false;
    m_loginStep = LoginStep::None;
    m_recvBuf.clear();
    if (m_keepAliveTimer->isActive()) {
        m_keepAliveTimer->stop();
        ui->checkBox_POP3ClientAutoTesting->setChecked(false);
    }
    setCommunicationControls(false);
}

// 功能：批量更新与连接状态相关的控件
void FormPop3Client::setCommunicationControls(bool connected)
{
    ui->pushButton_POP3ClientConnect->setEnabled(!connected);
    ui->pushButton_POP3ClientDisconnect->setEnabled(connected);
    ui->pushButton_POP3ClientSendMsg->setEnabled(connected);
    ui->checkBox_POP3ClientAutoTesting->setEnabled(connected);
    ui->checkBox_POP3ClientSsl->setEnabled(!connected); // 连接中禁止切换加密模式
}

// 功能：追加彩色日志并自动滚动到底部
void FormPop3Client::appendLog(const QString &text, const QColor &color)
{
    if (!ui->plainTextEdit_POP3ClientMsg) {
        return;
    }
    if (color.isValid()) {
        ui->plainTextEdit_POP3ClientMsg->appendHtml(
            QString("<span style=\"color:%1;\">%2</span>")
                .arg(color.name())
                .arg(text.toHtmlEscaped()));
    } else {
        ui->plainTextEdit_POP3ClientMsg->appendPlainText(text);
    }
    trimLog();
}

// 功能：裁剪日志，防止内容过长导致UI卡顿
void FormPop3Client::trimLog(int keepBlocks, int trimStep)
{
    static const int kKeepDefault = 1000;   // 默认保留block数
    static const int kTrimDefault = 200;    // 默认删除步长
    const int keep = keepBlocks > 0 ? keepBlocks : kKeepDefault;
    const int step = trimStep > 0 ? trimStep : kTrimDefault;

    QTextDocument *doc = ui->plainTextEdit_POP3ClientMsg->document();
    const int blocks = doc->blockCount();
    if (blocks <= keep + step) {
        return;
    }

    QTextCursor cursor(doc);
    cursor.movePosition(QTextCursor::Start);
    const int removeBlocks = blocks - keep;
    for (int i = 0; i < removeBlocks; ++i) {
        cursor.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
    }
    cursor.removeSelectedText();
    cursor.deleteChar();
}

// 功能：保存日志（public接口，供主窗口关闭时调用）
void FormPop3Client::saveLog()
{
    if (ui && ui->plainTextEdit_POP3ClientMsg) {
        savePlainTextEditToFile(ui->plainTextEdit_POP3ClientMsg);
    }
}

// 功能：将日志文本保存到文件（与其他模块一致的追加式日志）
void FormPop3Client::savePlainTextEditToFile(QPlainTextEdit *plainTextEdit)
{
    if (!plainTextEdit) {
        qWarning() << "FormPop3Client::savePlainTextEditToFile: plainTextEdit is null";
        return;
    }

    // 与QSettings配置文件同一目录：%APPDATA%\NDATools
    QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir appDataDir(appDataPath);
    const QString appName = QApplication::applicationName();
    if (!appName.isEmpty() && (appDataPath.endsWith("/" + appName) ||
                               appDataPath.endsWith("\\" + appName))) {
        QDir parentDir = appDataDir;
        if (parentDir.cdUp()) {
            appDataPath = parentDir.absolutePath();
        }
    }
    QDir().mkpath(appDataPath); // 确保目录存在

    const QString fileName = QDir(appDataPath).filePath("POP3ClientLogfile.txt");

    const bool fileExists = QFile::exists(fileName);
    const QIODevice::OpenMode openMode = fileExists ?
                                             (QIODevice::Append | QIODevice::Text) :
                                             (QIODevice::WriteOnly | QIODevice::Text);

    QFile file(fileName);
    if (file.open(openMode)) {
        QTextStream out(&file);
        out.setEncoding(QStringConverter::Utf8);

        if (fileExists) {
            const QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
            out << "\n" << "========== " << timestamp << " ==========" << Qt::endl;
        }

        out << plainTextEdit->toPlainText() << Qt::endl;
        file.close();
        qDebug() << "POP3客户端日志已保存至:" << fileName;
        if (this->isVisible()) {
            QMessageBox::information(this, "成功", QString("日志已保存至 %1").arg(fileName));
        }
    } else {
        qWarning() << "POP3客户端日志保存失败:" << file.errorString();
        if (this->isVisible()) {
            QMessageBox::critical(this, "错误", "保存失败: " + file.errorString());
        }
    }
}

// 功能：窗口关闭——优雅结束会话并保存日志
void FormPop3Client::closeEvent(QCloseEvent *event)
{
    if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState) {
        sendCommand("QUIT"); // 优雅结束POP3会话
        m_socket->disconnectFromHost();
    }
    saveLog();
    event->accept();
}
