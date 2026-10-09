#include "formchilddataconversion.h"
#include "ui_formchilddataconversion.h"
#include <QApplication>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QStringConverter>      // Qt6字符串编码转换器
#else
#include <QTextCodec>            // Qt5文本编码器
#endif
#ifdef _MSC_VER
#pragma execution_character_set("utf-8")  // MSVC编译器：设置执行字符集为UTF-8
#endif
static const int kMaxConvertLength = 50000;
static const int kAsyncConvertThreshold = 15000;
FormChildDataConversion::FormChildDataConversion(QWidget *parent)
    : QWidget(parent)
    ,inputUpdateTimer(new QTimer(this))  // 创建防抖定时器，父对象为this
    , ui(new Ui::FormChildDataConversion)
{
    ui->setupUi(this);
    ui->conversionTypeCombo->clear();
    ui->conversionTypeCombo->addItem("十进制 -> 二进制", "decimal_to_binary");
    ui->conversionTypeCombo->addItem("二进制 -> 十进制", "binary_to_decimal");
    ui->conversionTypeCombo->addItem("十进制 -> 八进制", "decimal_to_octal");
    ui->conversionTypeCombo->addItem("八进制 -> 十进制", "octal_to_decimal");
    ui->conversionTypeCombo->addItem("十进制 -> 十六进制", "decimal_to_hex");
    ui->conversionTypeCombo->addItem("十六进制 -> 十进制", "hex_to_decimal");
    ui->conversionTypeCombo->addItem("二进制 -> 八进制", "binary_to_octal");
    ui->conversionTypeCombo->addItem("八进制 -> 二进制", "octal_to_binary");
    ui->conversionTypeCombo->addItem("二进制 -> 十六进制", "binary_to_hex");
    ui->conversionTypeCombo->addItem("十六进制 -> 二进制", "hex_to_binary");
    ui->conversionTypeCombo->addItem("八进制 -> 十六进制", "octal_to_hex");
    ui->conversionTypeCombo->addItem("十六进制 -> 八进制", "hex_to_octal");
    inputUpdateTimer->setSingleShot(true);               // 设置为单次触发模式
    inputUpdateTimer->setInterval(300);                  // 设置延迟时间为 300ms
    connect(inputUpdateTimer, &QTimer::timeout, this, [this]() {
        QString text = ui->inputTextEdit->toPlainText(); // 获取输入文本
        updateInputInfo(text);                           // 更新输入统计信息
    });
    connectSignals();
    connect(&conversionWatcher, &QFutureWatcher<DataConverter::ConversionResult>::finished,
            this, &FormChildDataConversion::onConversionFinished);

    ui->inputTextEdit->setPlaceholderText("请输入要转换的数据...");
    ui->outputTextEdit->setPlaceholderText("转换结果将显示在这里...");
    QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));
    QString startMsg = QStringLiteral("[%1] 数据转换工具已启动").arg(timestamp);
    appendLog(startMsg);
}
FormChildDataConversion::~FormChildDataConversion()
{
    delete ui;
}
DataConverter::ConversionResult FormChildDataConversion::performConversion(
    const QString &conversionType, const QString &input)
{
    int fromBase = 0;
    int toBase = 0;
    if (conversionType == "decimal_to_binary")       { fromBase = 10; toBase = 2; }
    else if (conversionType == "binary_to_decimal")  { fromBase = 2;  toBase = 10; }
    else if (conversionType == "decimal_to_octal")   { fromBase = 10; toBase = 8; }
    else if (conversionType == "octal_to_decimal")   { fromBase = 8;  toBase = 10; }
    else if (conversionType == "decimal_to_hex")     { fromBase = 10; toBase = 16; }
    else if (conversionType == "hex_to_decimal")     { fromBase = 16; toBase = 10; }
    else if (conversionType == "binary_to_octal")    { fromBase = 2;  toBase = 8; }
    else if (conversionType == "octal_to_binary")    { fromBase = 8;  toBase = 2; }
    else if (conversionType == "binary_to_hex")      { fromBase = 2;  toBase = 16; }
    else if (conversionType == "hex_to_binary")      { fromBase = 16; toBase = 2; }
    else if (conversionType == "octal_to_hex")       { fromBase = 8;  toBase = 16; }
    else if (conversionType == "hex_to_octal")       { fromBase = 16; toBase = 8; }
    else {
        return DataConverter::ConversionResult(false, QString(), "未知的转换类型");
    }
    return DataConverter::convertBetween(input, fromBase, toBase);
}

void FormChildDataConversion::on_convertButton_clicked()
{
    if (!ui || !ui->inputTextEdit || !ui->outputTextEdit || !ui->conversionTypeCombo) {
        qWarning() << "FormChildDataConversion::on_convertButton_clicked: UI or controls is null";
        return;
    }
    if (conversionRunning) {
        return;
    }
    QString input = ui->inputTextEdit->toPlainText();
    if (input.isEmpty()) {
        showMessage(QStringLiteral("请输入要转换的数据"), true);
        return;
    }
    if (input.length() > kMaxConvertLength) {
        showMessage(QStringLiteral("输入过长（>%1 字符），请分批转换。").arg(kMaxConvertLength), true);
        return;
    }
    QString conversionType = ui->conversionTypeCombo->currentData().toString();
    // currentText() 返回选项的显示文本（"十进制 → 二进制" 或 "二进制 → 十进制"）
    QString conversionTypeName = ui->conversionTypeCombo->currentText();
    QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));
    QString logMsg = QStringLiteral("[%1] [数据转换] 开始转换: %2, 输入长度: %3 字符")
                         .arg(timestamp, conversionTypeName)
                         .arg(input.length());

    appendLog(logMsg);

    if (input.length() > kAsyncConvertThreshold) {
        setConversionBusy(true);
        appendLog(QStringLiteral("[%1] [数据转换] 使用异步转换（输入长度超过阈值 %2）")
                      .arg(timestamp)
                      .arg(kAsyncConvertThreshold));
        auto future = QtConcurrent::run([input, conversionType]() {
            return performConversion(conversionType, input);
        });
        conversionWatcher.setFuture(future);
        return;  // 异步转换立即返回，不等待结果
    }
    DataConverter::ConversionResult result = performConversion(conversionType, input);
    showResult(result, ui->outputTextEdit);
}
void FormChildDataConversion::on_clearButton_clicked()
{
    if (!ui || !ui->inputTextEdit || !ui->outputTextEdit) {
        qWarning() << "FormChildDataConversion::on_clearButton_clicked: UI or controls is null";
        return;
    }
    ui->inputTextEdit->clear();
    ui->outputTextEdit->clear();
    updateInputInfo("");
    updateOutputInfo("");
    QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));
    QString logMsg = QStringLiteral("[%1] [数据转换] 清空数据").arg(timestamp);
    appendLog(logMsg);

}
void FormChildDataConversion::on_swapButton_clicked()
{
    if (!ui || !ui->inputTextEdit || !ui->outputTextEdit) {
        qWarning() << "FormChildDataConversion::on_swapButton_clicked: UI or controls is null";
        return;
    }
    QString inputText = ui->inputTextEdit->toPlainText();
    QString outputText = ui->outputTextEdit->toPlainText();
    ui->inputTextEdit->setPlainText(outputText);
    ui->outputTextEdit->setPlainText(inputText);
    updateInputInfo(outputText);
    updateOutputInfo(inputText);
    QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));
    QString logMsg = QStringLiteral("[%1] [数据转换] 交换输入输出数据").arg(timestamp);
    appendLog(logMsg);
}
void FormChildDataConversion::on_copyButton_clicked()
{
    if (!ui || !ui->outputTextEdit) {
        qWarning() << "FormChildDataConversion::on_copyButton_clicked: UI or outputTextEdit is null";
        return;
    }
    QString text = ui->outputTextEdit->toPlainText();
    if (text.isEmpty()) {
        showMessage(QStringLiteral("没有可复制的内容"), true);
        return;
    }
    copyToClipboard(text);
}
void FormChildDataConversion::saveLog()
{
    if (logEntries.isEmpty()) {
        return;
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
        qWarning() << "FormChildDataConversion::saveLog: 无法创建目录" << appDataPath;
        return;
    }
    QString fileName = QDir(appDataPath).filePath(QStringLiteral("DataConversionLogfile.txt"));
    bool fileExists = QFile::exists(fileName);
    QIODevice::OpenMode openMode = fileExists ?
                                       (QIODevice::Append | QIODevice::Text) :    // 追加模式（文件存在）
                                       (QIODevice::WriteOnly | QIODevice::Text);  // 创建模式（文件不存在）
    QFile file(fileName);
    if (file.open(openMode)) {
        QTextStream out(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        out.setCodec("UTF-8");
#else
        out.setEncoding(QStringConverter::Utf8);
#endif
        if (fileExists) {
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
        qDebug() << "数据转换工具日志已保存至:" << fileName;
        logEntries.clear();
        if (this->isVisible()) {
            // 性能优化：使用 QStringLiteral 避免临时字符串对象分配
            QMessageBox::information(this, QStringLiteral("成功"), QStringLiteral("日志已保存至 %1").arg(fileName));
        }
    } else {
        // 文件打开失败，记录警告并显示错误消息框
        qWarning() << "数据转换工具日志保存失败:" << file.errorString();
        if (this->isVisible()) {
            // 性能优化：使用 QStringLiteral 避免临时字符串对象分配
            QMessageBox::critical(this, QStringLiteral("错误"), QStringLiteral("保存失败: ") + file.errorString());
        }
    }
}
void FormChildDataConversion::onConversionFinished()
{

    if (!ui || !ui->outputTextEdit) {
        qWarning() << "FormChildDataConversion::onConversionFinished: UI or outputTextEdit is null";
        setConversionBusy(false);
        return;
    }
    setConversionBusy(false);
    auto result = conversionWatcher.result();
    QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));
    QString logMsg = QStringLiteral("[%1] [数据转换] 异步转换完成").arg(timestamp);
    appendLog(logMsg);
    showResult(result, ui->outputTextEdit);
}
void FormChildDataConversion::onInputTextChanged()
{
    if (!inputUpdateTimer) {
        qWarning() << "FormChildDataConversion::onInputTextChanged: inputUpdateTimer is null";
        return;
    }
    inputUpdateTimer->stop();
    inputUpdateTimer->start();
}
void FormChildDataConversion::showMessage(const QString &message, bool isError)
{
    if (isError) {
        QMessageBox::warning(this, QStringLiteral("错误"), message);
    } else {
        qDebug() << message;
    }
}

void FormChildDataConversion::setConversionBusy(bool busy)
{
    conversionRunning = busy;
    ui->convertButton->setEnabled(!busy);
    ui->clearButton->setEnabled(!busy);
    ui->swapButton->setEnabled(!busy);
    ui->copyButton->setEnabled(!busy);
}
void FormChildDataConversion::showResult(const DataConverter::ConversionResult &result, QTextEdit *outputEdit)
{
    if (!outputEdit) {
        qWarning() << "FormChildDataConversion::showResult: outputEdit is null";
        return;
    }
    QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));
    if (result.success) {
        outputEdit->setPlainText(result.data);           // 显示转换结果
        updateOutputInfo(result.data);                   // 更新输出统计信息
        showMessage(QStringLiteral("转换成功"));        // 显示成功提示
        QString logMsg = QStringLiteral("[%1] [数据转换] 转换成功，输出长度: %2 字符")
                             .arg(timestamp)
                             .arg(result.data.length());
        appendLog(logMsg);
    } else {
        outputEdit->setPlainText(QStringLiteral(""));   // 清空输出文本框
        updateOutputInfo(QStringLiteral(""));           // 更新输出统计信息（设置为空）
        showMessage(result.errorMessage, true);          // 显示错误
        QString logMsg = QStringLiteral("[%1] [数据转换] 转换失败: %2")
                             .arg(timestamp)
                             .arg(result.errorMessage);
        appendLog(logMsg);
    }
}
void FormChildDataConversion::updateInputInfo(const QString &text)
{
    if (!ui || !ui->inputInfoLabel) {
        return;
    }
    QString info = DataConverter::getDataInfo(text);
    ui->inputInfoLabel->setText(info);

}
void FormChildDataConversion::updateOutputInfo(const QString &text)
{
    QString info = DataConverter::getDataInfo(text);
    ui->outputInfoLabel->setText(info);
}

void FormChildDataConversion::copyToClipboard(const QString &text)
{
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(text);
    showMessage(QStringLiteral("已复制到剪贴板"));
    QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));
    QString logMsg = QStringLiteral("[%1] [数据转换] 复制结果到剪贴板，长度: %2 字符")
                         .arg(timestamp)
                         .arg(text.length());
    appendLog(logMsg);
}
void FormChildDataConversion::connectSignals()
{
    if (!ui || !ui->inputTextEdit) {
        qWarning() << "FormChildDataConversion::connectSignals: UI or inputTextEdit is null";
        return;
    }
    connect(ui->inputTextEdit, &QTextEdit::textChanged, this, &FormChildDataConversion::onInputTextChanged);
}
void FormChildDataConversion::appendLog(const QString &message)
{
    logEntries.enqueue(message);
    qDebug() << "数据转换工具日志:" << message;
    while (logEntries.size() > 1000) {
        logEntries.dequeue();  // 删除最旧的条目（队列头部）
    }
}
void FormChildDataConversion::trimLog()
{
    // UI日志显示已删除，不需要裁剪
}
void FormChildDataConversion::savePlainTextEditToFile(QPlainTextEdit* plainTextEdit)
{
    if (!plainTextEdit) {
        qWarning() << "FormChildDataConversion::savePlainTextEditToFile: plainTextEdit is null";
        return;
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
        qWarning() << "FormChildDataConversion::savePlainTextEditToFile: 无法创建目录" << appDataPath;
        return;
    }
    QString fileName = QDir(appDataPath).filePath("DataConversionLogfile.txt");
    bool fileExists = QFile::exists(fileName);
    QIODevice::OpenMode openMode = fileExists ?
                                       (QIODevice::Append | QIODevice::Text) :    // 追加模式（文件存在）
                                       (QIODevice::WriteOnly | QIODevice::Text);  // 创建模式（文件不存在）
    QFile file(fileName);
    if (file.open(openMode)) {
        QTextStream out(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        out.setCodec("UTF-8"); // Qt5 设置编码
#else
        out.setEncoding(QStringConverter::Utf8); // Qt6 设置编码
#endif
        if (fileExists) {
            QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
            out << "\n" << "========== " << timestamp << " ==========" << endl;
#else
            out << "\n" << "========== " << timestamp << " ==========" << Qt::endl;
#endif
        }
        QString content = plainTextEdit->toPlainText();
        if (!content.isEmpty()) {
            out << content;
        }
        file.close();
        qDebug() << "数据转换工具日志已保存至:" << fileName;
        if (this->isVisible()) {
            QMessageBox::information(this, "成功", QString("日志已保存至 %1").arg(fileName));
        }
    } else {
        qWarning() << "数据转换工具日志保存失败:" << file.errorString();
        if (this->isVisible()) {
            QMessageBox::critical(this, "错误", "保存失败: " + file.errorString());
        }
    }
}
void FormChildDataConversion::closeEvent(QCloseEvent *event)
{
    saveLog();
    event->accept();
}

