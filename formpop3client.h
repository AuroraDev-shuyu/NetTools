#ifndef FORMPOP3CLIENT_H
#define FORMPOP3CLIENT_H

#include <QWidget>

class QSslSocket;
class QTimer;
class QPlainTextEdit;
class QCloseEvent;

namespace Ui {
class FormPop3Client;
}

// POP3 客户端表单（命令调试式）：主机/端口/SSL/用户名/密码 + 原始命令框 + 快捷命令按钮。
// 协议解析用 pop3protocol.h 的 Pop3Protocol 命名空间；多行响应状态机在本类内编排。
class FormPop3Client : public QWidget
{
    Q_OBJECT

public:
    explicit FormPop3Client(QWidget *parent = nullptr);
    ~FormPop3Client();

    void saveLog();

private slots:
    void on_pushButton_POP3ClientConnect_clicked();
    void on_pushButton_POP3ClientDisconnect_clicked();
    void on_pushButton_POP3ClientSendMsg_clicked();
    void on_pushButton_POP3ClientClose_clicked();
    void on_checkBox_POP3ClientAutoTesting_clicked();
    void on_checkBox_POP3ClientSsl_toggled(bool checked);

private:
    // 自动登录步骤：None=不自动登录，WaitingGreeting=等欢迎语，WaitingUserOk=USER已发等+OK，LoggedIn=PASS通过
    enum class LoginStep { None, WaitingGreeting, WaitingUserOk, LoggedIn };

    Ui::FormPop3Client *ui;
    QSslSocket *m_socket = nullptr;         // POP3 连接（明文走 connectToHost，SSL 走 connectToHostEncrypted）
    QByteArray m_recvBuf;                   // 接收缓冲区
    QTimer *m_keepAliveTimer = nullptr;     // 自动保活定时器（NOOP 30s）
    bool m_connected = false;               // TCP/SSL 连接已建立
    bool m_loggedIn = false;                // USER/PASS 已完成，进入事务状态
    bool m_inMultiLine = false;             // 正在接收多行响应（LIST/RETR/UIDL/TOP/CAPA）
    bool m_expectMultiLine = false;         // 刚发的命令预期返回多行响应
    LoginStep m_loginStep = LoginStep::None;
    QString m_user;                         // 本次会话用户名（自动登录用，不落盘）
    QString m_password;                     // 本次会话密码（自动登录用，不落盘）

    void sendCommand(const QByteArray &cmd);            // 发送命令：补CRLF、写socket、记[发送]日志（PASS脱敏）
    void sendQuickCmd(const QByteArray &cmd);           // 快捷按钮发送（带多行预期标记）
    void startAutoLogin();                              // 收到+OK欢迎语后自动 USER/PASS
    void handleLoginProgress(bool ok, const QByteArray &line); // 推进自动登录步骤
    void parseResponses();                              // 解析接收缓冲（单行/多行状态机）
    void resetConnectionState();                        // 复位连接状态与UI
    void setCommunicationControls(bool connected);      // 批量更新按钮可用状态
    void appendLog(const QString &text, const QColor &color = QColor()); // 追加彩色日志并自动滚动
    void trimLog(int keepBlocks = 1000, int trimStep = 200); // 裁剪日志防止卡顿
    void savePlainTextEditToFile(QPlainTextEdit *plainTextEdit);
    void closeEvent(QCloseEvent *event) override;
};

#endif // FORMPOP3CLIENT_H
