#include "dataconverter.h"
#include <algorithm>
namespace {
const char kDigitChars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
int charToDigit(QChar c)
{
    if (c >= '0' && c <= '9') return c.unicode() - '0';
    if (c >= 'a' && c <= 'z') return c.unicode() - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c.unicode() - 'A' + 10;
    return -1;
}
QString decimalMultiplyAdd(const QString &num, int mult, int add)
{
    QString result;
    result.reserve(num.size() + 2);
    int carry = add;
    for (int i = num.size() - 1; i >= 0; --i) {
        int digit = num.at(i).unicode() - '0';
        int product = digit * mult + carry;
        result.append(QChar('0' + product % 10));
        carry = product / 10;
    }
    while (carry > 0) {
        result.append(QChar('0' + carry % 10));
        carry /= 10;
    }
    std::reverse(result.begin(), result.end());
    return result;
}
QString decimalDivideBy(const QString &num, int divisor, int &remainder)
{
    QString quotient;
    quotient.reserve(num.size());
    int rem = 0;
    for (int i = 0; i < num.size(); ++i) {
        int current = rem * 10 + (num.at(i).unicode() - '0');
        int q = current / divisor;
        rem = current % divisor;
        if (!quotient.isEmpty() || q != 0) {
            quotient.append(QChar('0' + q));
        }
    }
    remainder = rem;
    return quotient.isEmpty() ? QString("0") : quotient;
}
QString formatBinaryGroups(const QString &bits)
{
    if (bits.size() <= 8) {
        return bits;
    }
    QString formatted;
    formatted.reserve(bits.size() + bits.size() / 8);
    for (int i = 0; i < bits.size(); i += 8) {
        if (i > 0) {
            formatted.append(' ');
        }
        formatted.append(bits.mid(i, 8));
    }
    return formatted;
}

QString binaryToPowerOfTwoBase(const QString &bits, int toBase)
{
    int groupSize = (toBase == 8) ? 3 : 4;  // 3位一组→八进制，4位一组→十六进制
    QString padded = bits;
    int rem = bits.size() % groupSize;
    if (rem != 0) {
        padded = QString(groupSize - rem, '0') + bits;  // 左侧补零对齐
    }
    QString result;
    result.reserve(padded.size() / groupSize);
    for (int i = 0; i < padded.size(); i += groupSize) {
        int value = 0;
        for (int j = 0; j < groupSize; ++j) {
            value = value * 2 + (padded.at(i + j).unicode() - '0');
        }
        result.append(kDigitChars[value]);
    }
    return result;
}

// 八进制/十六进制 → 二进制：每位展开为3/4位（O(n)）
QString powerOfTwoBaseToBinary(const QString &value, int fromBase)
{
    int bitsPerDigit = (fromBase == 8) ? 3 : 4;
    QString result;
    result.reserve(value.size() * bitsPerDigit);
    for (QChar c : value) {
        int digit = charToDigit(c);
        for (int j = bitsPerDigit - 1; j >= 0; --j) {
            result.append(QChar('0' + ((digit >> j) & 1)));
        }
    }
    int firstNonZero = result.indexOf('1');  // 去除前导零（保留至少一位）
    return (firstNonZero < 0) ? QString("0") : result.mid(firstNonZero);
}

}
const quint16 DataConverter::crc16Table[256]={
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
    0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
    0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
    0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
    0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
    0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
    0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
    0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
    0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
    0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
    0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
    0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
    0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
    0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
    0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
    0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
    0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
    0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
    0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
    0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
    0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
    0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0
};
const quint32 DataConverter::crc32Table[256]={
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F,
    0xE963A535, 0x9E6495A3, 0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
    0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91, 0x1DB71064, 0x6AB020F2,
    0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9,
    0xFA0F3D63, 0x8D080DF5, 0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
    0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, 0x35B5A8FA, 0x42B2986C,
    0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423,
    0xCFBA9599, 0xB8BDA50F, 0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
    0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D, 0x76DC4190, 0x01DB7106,
    0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D,
    0x91646C97, 0xE6635C01, 0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
    0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950,
    0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7,
    0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
    0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9, 0x5005713C, 0x270241AA,
    0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81,
    0xB7BD5C3B, 0xC0BA6CAD, 0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
    0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683, 0xE3630B12, 0x94643B84,
    0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB,
    0x196C3671, 0x6E6B06E7, 0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
    0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5, 0xD6D6A3E8, 0xA1D1937E,
    0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55,
    0x316E8EEF, 0x4669BE79, 0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
    0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F, 0xC5BA3BBE, 0xB2BD0B28,
    0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F,
    0x72076785, 0x05005713, 0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
    0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21, 0x86D3D2D4, 0xF1D4E242,
    0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69,
    0x616BFFD3, 0x166CCF45, 0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
    0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB, 0xAED16A4A, 0xD9D65ADC,
    0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693,
    0x54DE5729, 0x23D967BF, 0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
    0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
};


// 任意精度进制转换：将 value（fromBase 进制字符串）转换为 toBase 进制字符串
DataConverter::ConversionResult DataConverter::convertBetween(const QString &value, int fromBase, int toBase)
{
    // 步骤1：进制合法性验证（仅支持2~36进制）
    if (fromBase < 2 || fromBase > 36 || toBase < 2 || toBase > 36) {
        return ConversionResult(false, "", "不支持的进制，仅支持2~36进制");
    }

    // 步骤2：输入清理--去除空白和冒号等常见分隔符（负号保留，稍后处理）
    if (value.isEmpty()) {
        return ConversionResult(false, "", "输入为空");
    }
    QString input = value;
    input.remove(QRegularExpression("[\\s:]"));
    if (fromBase == 16 && input.startsWith("0x", Qt::CaseInsensitive)) {
        input.remove(0, 2);  // 十六进制支持 0x/0X 前缀
    }

    // 步骤3：符号处理--支持负数，以符号 '-' 表示
    bool negative = input.startsWith('-');
    if (negative || input.startsWith('+')) {
        input.remove(0, 1);
    }
    if (input.isEmpty()) {
        return ConversionResult(false, "", "输入为空");
    }

    // 步骤4：字符合法性验证
    for (QChar c : input) {
        int digit = charToDigit(c);
        if (digit < 0 || digit >= fromBase) {
            return ConversionResult(false, "", QString("无效字符 '%1'（%2进制）").arg(c).arg(fromBase));
        }
    }

    // 步骤5：去除前导零（保留至少一位）
    int firstNonZero = 0;
    while (firstNonZero < input.size() - 1 && input.at(firstNonZero) == '0') {
        ++firstNonZero;
    }
    if (firstNonZero > 0) {
        input.remove(0, firstNonZero);
    }

    // 步骤6：2/8/16 进制之间直接按位分组转换（性能优化，避免经过十进制）
    QString result;
    if (fromBase != 10 && toBase != 10) {
        QString bits = (fromBase == 2) ? input : powerOfTwoBaseToBinary(input, fromBase);
        result = (toBase == 2) ? bits : binaryToPowerOfTwoBase(bits, toBase);
    } else {
        // 步骤7：先转换为十进制字符串（"乘基加数"法，任意精度）
        QString decimal = (fromBase == 10) ? input : QString("0");
        if (fromBase != 10) {
            for (QChar c : input) {
                decimal = decimalMultiplyAdd(decimal, fromBase, charToDigit(c));
            }
        }

        // 步骤8：十进制转换为目标进制（"除基取余"法，任意精度）
        if (toBase == 10) {
            result = decimal;
        } else {
            QString reversed;
            while (decimal != "0") {
                int remainder;
                decimal = decimalDivideBy(decimal, toBase, remainder);
                reversed.append(kDigitChars[remainder]);
            }
            std::reverse(reversed.begin(), reversed.end());
            result = reversed.isEmpty() ? QString("0") : reversed;
        }
    }

    // 步骤9：输出格式化--二进制按8位分组，负数加符号（零不带符号）
    if (toBase == 2 && result != "0") {
        result = formatBinaryGroups(result);
    }
    if (negative && result != "0") {
        result.prepend('-');
    }
    return ConversionResult(true, result);
}

// 将十进制字符串转换为二进制字符串（结果按每8位一组用空格分隔）
DataConverter::ConversionResult DataConverter::decimalToBinary(const QString &decimalString)
{
    return convertBetween(decimalString, 10, 2);
}

// 将二进制字符串转换为十进制字符串
DataConverter::ConversionResult DataConverter::binaryToDecimal(const QString &binaryString)
{
    return convertBetween(binaryString, 2, 10);
}

// 统计字符串的字符数
int DataConverter::countBytes(const QString &data)
{
    // 将字符串转换为UTF-8编码字节数组，返回字节数组的长度
    return data.toUtf8().size();

}

// 获取字符串的统计信息（字符数和字节数）
QString DataConverter::getDataInfo(const QString &data)
{
    // 获取字符数（Unicode字符个数）
    int chars=countCharacter(data);

    // 获取字节数（UTF-8编码后的字节数）
    int bytes=countBytes(data);

    return QStringLiteral("字符数：%1，字节数：%2").arg(chars).arg(bytes);
}



// 计算所有常用校验值
DataConverter::ChecksumResult DataConverter::calculateChecksums(const QString &data)
{
    // 将字符串转换为UTF-8编码的字节数组
    QByteArray bytes=data.toUtf8();

    // 调用字节数组版本的函数进行计算
    return calculateChecksumsFromBytes(bytes);

}
DataConverter::ChecksumResult DataConverter::calculateChecksumsFromBytes(const QByteArray &data)
{
    ChecksumResult result;

    result.crc16 = QString::number(calculateCRC16(data), 16).toUpper().rightJustified(4, '0');
    result.crc32 = QString::number(calculateCRC32(data), 16).toUpper().rightJustified(8, '0');
    result.md5 = QCryptographicHash::hash(data, QCryptographicHash::Md5).toHex().toUpper();
    result.sha1 = QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex().toUpper();
    result.sha256 = QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex().toUpper();

    return result;
}

// 单独计算MD5哈希值
QString DataConverter::calculateMD5(const QString &data)
{
    // 将字符串转换为UTF-8编码的字节数组
    QByteArray bytes=data.toUtf8();

    // 计算MD5哈希值并转换为十进制大写字符串
    return QCryptographicHash::hash(bytes, QCryptographicHash::Md5).toHex().toUpper();

}

// 单独计算SHA1哈希值
QString DataConverter::calculateSHA1(const QString &data)
{
    // 将字符串转换为UTF-8编码的字节数组
    QByteArray bytes=data.toUtf8();

    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex().toUpper();

}

// 单独计算SHA256哈希值
QString DataConverter::calculateSHA256(const QString &data)
{
    // 将字符串转换为UTF-8编码的字节数组
    QByteArray bytes=data.toUtf8();

    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toUpper();
}

// 计算CRC16校验值
quint16 DataConverter::calculateCRC16(const QByteArray &data)
{
    quint16 crc=0xFFFF;

    // 遍历输入数据的每个字节
    for(int i=0;i<data.size();i++){
        // 将字节转换为无符号8位整数（确保值在0-255范围内）
        quint8 byte = static_cast<quint8>(data[i]);

        // 查找表算法
        // ^ 异或 相同为0，不同为1
        crc = (crc << 8) ^ crc16Table[((crc >> 8) ^ byte) & 0xFF];
    }

    return crc;

}

// 计算CRC32校验值
quint32 DataConverter::calculateCRC32(const QByteArray &data)
{
    quint32 crc=0xFFFFFFFF;

    // 遍历输入数据的每个字节
    for(int i=0;i<data.size();i++){
        // 将字节转换为无符号8位整数（确保值在0-255范围内）
        quint8 byte = static_cast<quint8>(data[i]);

        // 查找表算法
        // ^ 异或 相同为0，不同为1
        crc = (crc >> 8) ^ crc32Table[(crc ^ byte) & 0xFF];
    }

    return crc;
}

// 统计字符串的字符数（Qt字符长度）
int DataConverter::countCharacter(const QString &data)
{
    // 返回字符串的字符数（Qt的QString::length()返回Unicode字符个数）
    return data.length();

}


