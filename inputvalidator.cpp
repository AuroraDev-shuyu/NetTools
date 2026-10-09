#include "inputvalidator.h"

#include <QFileInfo>
#include <QUrl>
#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif
const QRegularExpression InputValidator::ipv4Regex(
    "^(?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$"
    );
const QRegularExpression InputValidator::ipv6Regex(
    "^(?:[0-9a-fA-F]{1,4}:){7}[0-9a-fA-F]{1,4}$|^::1$|^::$"
    );
const QRegularExpression InputValidator::hostnameRegex(
    "^(?:[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?(?:\\.[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?)*)$"
    );
const QRegularExpression InputValidator::hexRegex(
    "^[0-9a-fA-F]+$"
    );
const QRegularExpression InputValidator::base64Regex(
    "^[A-Za-z0-9+/]*={0,2}$"
    );
// 验证输入的字符串是否为有效的IPv4或IPv6地址
InputValidator::ValidationResult InputValidator::validatorIPAddress(const QString &ipAddress)
{
    if (ipAddress.isEmpty()) {
        return ValidationResult(false, "IP地址不能为空");
    }
    QHostAddress address(ipAddress);
    if (address.isNull()) {
        return ValidationResult(false, "IP地址格式无效");
    }
    if (address.protocol() != QAbstractSocket::IPv4Protocol &&
        address.protocol() != QAbstractSocket::IPv6Protocol) {
        return ValidationResult(false, "不支持的IP地址类型");
    }
    return ValidationResult(true);
}
InputValidator::ValidationResult InputValidator::validatorPort(int port)
{
    if (port < 1 || port > 65535) {
        return ValidationResult(false, "端口号必须在1-65535范围内");
    }
    return ValidationResult(true);
}
InputValidator::ValidationResult InputValidator::validatorPort(const QString &portStr)
{   
    if (portStr.isEmpty()) {
        return ValidationResult(false, "端口号不能为空");
    }
    bool ok;
    int port = portStr.toInt(&ok);
    if (!ok) {
        return ValidationResult(false, "端口号必须是数字");
    }
    return validatorPort(port);
}
InputValidator::ValidationResult InputValidator::validatorHostname(const QString &hostname)
{
    if (hostname.isEmpty()) {
        return ValidationResult(false, "主机名不能为空");
    }
    if (hostname.length() > 253) {
        return ValidationResult(false, "主机名长度不能超过253个字符");
    }
    if (!hostnameRegex.match(hostname).hasMatch()) {
        return ValidationResult(false, "主机名格式无效");
    }
    return ValidationResult(true);
}
//尝试按IP地址校验，失败则按主机名校验
InputValidator::ValidationResult InputValidator::validatorNetworkAddress(const QString &address)
{
    if (address.isEmpty()) {
        return ValidationResult(false, "网络地址不能为空");
    }
    ValidationResult ipResult = validatorIPAddress(address);
    if (ipResult.isValid) {
        return ipResult;
    }
    ValidationResult hostnameResult = validatorHostname(address);
    if(hostnameResult.isValid){
        return hostnameResult;
    }
    return ValidationResult(false,"无效的网络地址格式（既不是有效IP也不是有效的主机名）");
}
// 验证文件路径格式，可选检查文件是否存在
InputValidator::ValidationResult InputValidator::validatorFilePath(const QString &filePath, bool mustExist)
{
    if(filePath.isEmpty()){
        return ValidationResult(false,"文件路径不能为空");
    }
    QFileInfo fileInfo(filePath);
    if (mustExist && !fileInfo.exists()) {
        return ValidationResult(false,"指定的文件不存在");
    }
    if (mustExist && !fileInfo.isFile()) {
        return ValidationResult(false,"指定的路径不是文件");
    }
    return ValidationResult(true);
}
// 验证字符串不为空且不仅包含空白字符
InputValidator::ValidationResult InputValidator::validatorNonEmptyString(const QString &str, const QString &fieldName)
{
    if (str.isEmpty()) {
        return ValidationResult(false,QString("%1不能为空").arg(fieldName));
    }
    if (str.trimmed().isEmpty()) {
        return ValidationResult(false,QString("%1不能只包含空白字符").arg(fieldName));
    }
    return ValidationResult(true);
}
// 验证字符串长度是否在指定范围内
InputValidator::ValidationResult InputValidator::validatorStringLength(const QString &str, int minLength, int maxLength, const QString &fieldName)
{
    int length=str.length();
    if(length<minLength){
        return ValidationResult(false, QString("%1长度不能少于%2个字符").arg(fieldName).arg(minLength));
    }
    if(length>maxLength){
        return ValidationResult(false, QString("%1长度不能超过%2个字符").arg(fieldName).arg(maxLength));
    }
    return ValidationResult(true);
}
// 验证整数是否在指定范围内
InputValidator::ValidationResult InputValidator::validatorNumberRange(int value, int minValue, int maxValue, const QString &fieldName)
{
    if(value<minValue){
        return ValidationResult(false, QString("%1不能小于%2").arg(fieldName).arg(minValue));
    }
    if(value<maxValue){
        return ValidationResult(false, QString("%1不能大于%2").arg(fieldName).arg(maxValue));
    }
    return ValidationResult(true);
}
InputValidator::ValidationResult InputValidator::validatorHexString(const QString &hexStr)
{
    if(hexStr.isEmpty()){
        return ValidationResult(false,"十六进制字符串不能为空");
    }
    if (!hexRegex.match(hexStr).hasMatch()) {
        return ValidationResult(false,"十六进制字符串只能包含0-9 a-f");
    }
    if (hexStr.length() % 2 != 0) {
        return ValidationResult(false,"十六进制字符串长度必须是偶数");
    }
    return ValidationResult(true);
}
// Base64字符串校验函数：验证字符串是否符合Base64编码格式
InputValidator::ValidationResult InputValidator::validatorBase64String(const QString &base64Str)
{
    if(base64Str.isEmpty()){
        return ValidationResult(false,"Base64字符串不能为空");
    }
    if (!base64Regex.match(base64Str).hasMatch()) {
        return ValidationResult(false,"Base64字符串格式无效");
    }
    if (base64Str.length() % 4 != 0) {
        return ValidationResult(false,"Base64字符串长度必须是4的倍数");
    }
    return ValidationResult(true);
}

