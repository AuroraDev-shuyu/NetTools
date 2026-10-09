#include "formchilddatavalidation.h"
#include "ui_formchilddatavalidation.h"
#include <QApplication>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QStringConverter>      // Qt6字符串编码转换器
#else
#include <QTextCodec>            // Qt5文本编码器
#endif

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")  // MSVC编译器：设置执行字符集为UTF-8
#endif

/*
主要功能：
1、调用父类QWidget构造函数，初始化窗口基础属性
2、创建并初始化UI界面（通ui->setupUi()）
3、连接信号和槽
4、设置输入框的占位符文本，提示用户输入
5、连接异步计算完成信号到槽函数，用于处理异步计算结果
6、初始化日志系统，记录工具启动信息
*/
static const int kMaxChecksumLength = 200000;
static const int kAsyncChecksumThreshold = 40000;
FormChildDataValidation::FormChildDataValidation(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::FormChildDataValidation)
{
    ui->setupUi(this);
    connectSignals();
    ui->checksumInputEdit->setPlaceholderText("请输入要计算校验值的数据...");
    connect(&checksumWatcher, &QFutureWatcher<DataConverter::ChecksumResult>::finished,
            this, &FormChildDataValidation::onChecksumFinished);
    QString startMsg = QString("[%1] 数据校验工具已启动")
                           .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));
    appendLog(startMsg);
}
FormChildDataValidation::~FormChildDataValidation()
{
    delete ui;
}
void FormChildDataValidation::on_calculateButton_clicked()
{
    if (!ui || !ui->checksumInputEdit || !ui->upperCaseCheckBox ||
        !ui->crc16Edit || !ui->crc32Edit || !ui->md5Edit ||
        !ui->sha1Edit || !ui->sha256Edit) {
        qWarning() << "FormChildDataValidation::on_calculateButton_clicked: UI or controls is null";
        return;
    }
    if (checksumRunning) {
        return;
    }
    QString input = ui->checksumInputEdit->toPlainText();
    if (input.isEmpty()) {
        showMessage(QStringLiteral("请输入要计算校验值的数据"), true);
        return;
    }
    if (input.length() > kMaxChecksumLength) {
        showMessage(QStringLiteral("输入过长（>%1 字符），请分批计算。").arg(kMaxChecksumLength), true);
        return;
    }
    if (input.length() > kAsyncChecksumThreshold) {
        setChecksumBusy(true);
        QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));
        QString logMsg = QStringLiteral("[%1] [数据校验] 开始计算校验值，输入长度: %2 字符，使用异步计算")
                             .arg(timestamp)
                             .arg(input.length());
        appendLog(logMsg);
        appendLog(QStringLiteral("[%1] [数据校验] 使用异步计算（输入长度超过阈值 %2）")
                      .arg(timestamp)
                      .arg(kAsyncChecksumThreshold));
        auto future = QtConcurrent::run([input]() {
            return DataConverter::calculateChecksums(input);
        });
        checksumWatcher.setFuture(future);
        return;
    }
    QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));
    QString logMsg = QStringLiteral("[%1] [数据校验] 开始计算校验值，输入长度: %2 字符")
                         .arg(timestamp)
                         .arg(input.length());
    appendLog(logMsg);
    if (input.length() > kAsyncChecksumThreshold) {
        appendLog(QStringLiteral("[%1] [数据校验] 使用异步计算（输入长度超过阈值 %2）")
                      .arg(timestamp)
                      .arg(kAsyncChecksumThreshold));
    }
    DataConverter::ChecksumResult result = DataConverter::calculateChecksums(input);
    bool isUpperCase = ui->upperCaseCheckBox->isChecked();
    ui->crc16Edit->setText(isUpperCase ? result.crc16 : result.crc16.toLower());
    ui->crc32Edit->setText(isUpperCase ? result.crc32 : result.crc32.toLower());
    ui->md5Edit->setText(isUpperCase ? result.md5 : result.md5.toLower());
    ui->sha1Edit->setText(isUpperCase ? result.sha1 : result.sha1.toLower());
    ui->sha256Edit->setText(isUpperCase ? result.sha256 : result.sha256.toLower());
    showMessage(QStringLiteral("校验值计算完成"));
    logMsg = QStringLiteral("[%1] [数据校验] 校验值计算完成").arg(timestamp);
    appendLog(logMsg);
}
void FormChildDataValidation::saveLog()
{
    if (logEntries.isEmpty()) {
        return; // 没有日志需要保存
    }
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
    if (!QDir().mkpath(appDataPath)) {
        qWarning() << "FormChildDataValidation::saveLog: 无法创建目录" << appDataPath;
        return;
    }
    QString fileName = QDir(appDataPath).filePath(QStringLiteral("DataConversionLogfile.txt"));
    bool fileExists = QFile::exists(fileName);
    QIODevice::OpenMode openMode = fileExists ?
                                       (QIODevice::Append | QIODevice::Text) :
                                       (QIODevice::WriteOnly | QIODevice::Text);
    QFile file(fileName);
    if (file.open(openMode)) {
        QTextStream out(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        out.setCodec("UTF-8");
#else
        out.setEncoding(QStringConverter::Utf8); // Qt6设置编码
#endif
        if (fileExists) {
            // 性能优化：使用 QStringLiteral 避免临时字符串对象分配
            QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
            out << "\n" << QStringLiteral("========== ") << timestamp << QStringLiteral(" ==========") << endl;
#else
            out << "\n" << QStringLiteral("========== ") << timestamp << QStringLiteral(" ==========") << Qt::endl;
#endif
        }
        for (const QString &entry : logEntries) {
            out << entry;
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
            out << endl;
#else
            out << Qt::endl;
#endif
        }
        file.close();
        qDebug() << "数据校验工具日志已保存至:" << fileName;
        logEntries.clear();
        if (this->isVisible()) {
            // 性能优化：使用 QStringLiteral 避免临时字符串对象分配
            QMessageBox::information(this, QStringLiteral("成功"), QStringLiteral("日志已保存至 %1").arg(fileName));
        }
    } else {
        qWarning() << "数据校验工具日志保存失败:" << file.errorString();
        if (this->isVisible()) {
            // 性能优化：使用 QStringLiteral 避免临时字符串对象分配
            QMessageBox::critical(this, QStringLiteral("错误"), QStringLiteral("保存失败: ") + file.errorString());
        }
    }
}
// 连接信号和槽
void FormChildDataValidation::connectSignals()
{
    if (!ui || !ui->copyHashButton || !ui->upperCaseCheckBox ||
        !ui->crc16Edit || !ui->crc32Edit || !ui->md5Edit ||
        !ui->sha1Edit || !ui->sha256Edit) {
        qWarning() << "FormChildDataValidation::connectSignals: UI or controls is null";
        return;
    }

    // 连接复制按钮的点击信号到lambda表达式
    connect(ui->copyHashButton, &QPushButton::clicked, [this]() {
        // 企业级标准：在 lambda 内部再次检查 UI 控件，确保安全访问
        if (!ui || !ui->crc16Edit || !ui->crc32Edit || !ui->md5Edit ||
            !ui->sha1Edit || !ui->sha256Edit) {
            qWarning() << "FormChildDataValidation::copyHashButton clicked: UI or controls is null";
            return;
        }
        QString allHashes = QStringLiteral("CRC16: %1\nCRC32: %2\nMD5: %3\nSHA1: %4\nSHA256: %5")
                                .arg(ui->crc16Edit->text())      // CRC16 校验值
                                .arg(ui->crc32Edit->text())  // CRC32 校验值
                                .arg(ui->md5Edit->text())    // MD5 哈希值
                                .arg(ui->sha1Edit->text())   // SHA1 哈希值
                                .arg(ui->sha256Edit->text()); // SHA256 哈希值
        copyToClipboard(allHashes);
    });
    // 连接大小写复选框的状态变化信号到槽函数
    connect(ui->upperCaseCheckBox, &QCheckBox::toggled, this, &FormChildDataValidation::onUpperCaseToggled);
}
void FormChildDataValidation::copyToClipboard(const QString &text)
{
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(text);
    showMessage(QStringLiteral("已复制到剪贴板"));
    QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));
    QString logMsg = QStringLiteral("[%1] [数据校验] 复制校验值到剪贴板").arg(timestamp);
    appendLog(logMsg);

}
void FormChildDataValidation::showMessage(const QString &message, bool isError)
{
    if (isError) {
        QMessageBox::warning(this, QStringLiteral("错误"), message);
    } else {
        qDebug() << message;
    }
}
// 设置校验计算忙碌状态
void FormChildDataValidation::setChecksumBusy(bool busy)
{
    checksumRunning = busy;
    ui->calculateButton->setEnabled(!busy);
    ui->copyHashButton->setEnabled(!busy);
}
void FormChildDataValidation::appendLog(const QString &message)
{
    logEntries.enqueue(message);
    qDebug() << "数据校验工具日志:" << message;
    while (logEntries.size() > 1000) {
        logEntries.dequeue();
    }
}
void FormChildDataValidation::closeEvent(QCloseEvent *event)
{
    saveLog();
    event->accept();
}
// 大小写复选框状态变化槽函数
void FormChildDataValidation::onUpperCaseToggled(bool enabled)
{
    if (!ui || !ui->crc16Edit || !ui->crc32Edit || !ui->md5Edit ||
        !ui->sha1Edit || !ui->sha256Edit) {
        qWarning() << "FormChildDataValidation::onUpperCaseToggled: UI or controls is null";
        return;
    }
    if (!ui->crc16Edit->text().isEmpty()) {
        if (enabled) {
            ui->crc16Edit->setText(ui->crc16Edit->text().toUpper());
            ui->crc32Edit->setText(ui->crc32Edit->text().toUpper());
            ui->md5Edit->setText(ui->md5Edit->text().toUpper());
            ui->sha1Edit->setText(ui->sha1Edit->text().toUpper());
            ui->sha256Edit->setText(ui->sha256Edit->text().toUpper());
        } else {
            ui->crc16Edit->setText(ui->crc16Edit->text().toLower());
            ui->crc32Edit->setText(ui->crc32Edit->text().toLower());
            ui->md5Edit->setText(ui->md5Edit->text().toLower());
            ui->sha1Edit->setText(ui->sha1Edit->text().toLower());
            ui->sha256Edit->setText(ui->sha256Edit->text().toLower());
        }
    }
}
// 异步校验计算完成槽函数
void FormChildDataValidation::onChecksumFinished()
{
    if (!ui || !ui->upperCaseCheckBox || !ui->crc16Edit || !ui->crc32Edit ||
        !ui->md5Edit || !ui->sha1Edit || !ui->sha256Edit) {
        qWarning() << "FormChildDataValidation::onChecksumFinished: UI or controls is null";
        // 即使 UI 控件为空，也要解除忙碌状态，避免按钮永久禁用
        setChecksumBusy(false);
        return;
    }
    setChecksumBusy(false);
    auto result = checksumWatcher.result();
    bool isUpperCase = ui->upperCaseCheckBox->isChecked();
    ui->crc16Edit->setText(isUpperCase ? result.crc16 : result.crc16.toLower());
    ui->crc32Edit->setText(isUpperCase ? result.crc32 : result.crc32.toLower());
    ui->md5Edit->setText(isUpperCase ? result.md5 : result.md5.toLower());
    ui->sha1Edit->setText(isUpperCase ? result.sha1 : result.sha1.toLower());
    ui->sha256Edit->setText(isUpperCase ? result.sha256 : result.sha256.toLower());
    showMessage(QStringLiteral("校验值计算完成"));
    QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));
    QString logMsg = QStringLiteral("[%1] [数据校验] 异步校验值计算完成").arg(timestamp);
    appendLog(logMsg);
}



