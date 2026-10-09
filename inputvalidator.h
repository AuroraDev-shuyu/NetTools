#ifndef INPUTVALIDATOR_H
#define INPUTVALIDATOR_H
#include <QString>
#include <QRegularExpression>
#include <QHostAddress>
#include <QAbstractSocket>
#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif
class InputValidator
{
public:
    struct ValidationResult {
        bool isValid;
        QString errorMessage;
        ValidationResult(bool valid = true, const QString &message = QString())
            : isValid(valid),
            errorMessage(message) {}
    };
    static ValidationResult validatorIPAddress(const QString &ipAddress);
    static ValidationResult validatorPort(int port);
    static ValidationResult validatorPort(const QString &portStr);
    static ValidationResult validatorHostname(const QString &hostname);
    static ValidationResult validatorNetworkAddress(const QString &address);
    static ValidationResult validatorFilePath(const QString &filePath, bool mustExist = false);
    static ValidationResult validatorNonEmptyString(const QString &str, const QString &fieldName = "字段");
    static ValidationResult validatorStringLength(const QString &str, int minLength, int maxLength, const QString &fieldName = "字段");
    static ValidationResult validatorNumberRange(int value, int minValue, int maxValue, const QString &fieldName = "数值");

    static ValidationResult validatorHexString(const QString &hexStr);
    static ValidationResult validatorBase64String(const QString &base64Str);
private:
    InputValidator()=default;
    static const QRegularExpression ipv4Regex;
    static const QRegularExpression ipv6Regex;
    static const QRegularExpression hostnameRegex;
    static const QRegularExpression hexRegex;
    static const QRegularExpression base64Regex;
};
#endif // INPUTVALIDATOR_H
