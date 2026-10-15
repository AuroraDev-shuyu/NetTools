#ifndef HEXDUMP_H
#define HEXDUMP_H

#include <QByteArray>
#include <QString>

// 通用十六进制转储，供各表单与调试输出共用：
//   0000  68 65 6C 6C 6F 20 77 6F 72 6C 64 0A           |hello world.   |
// data 较大时调用方应自行截断（例如只取前若干 KB），避免生成超大字符串。
inline QString hexDump(const QByteArray &data, int bytesPerLine = 16)
{
    if (bytesPerLine <= 0) {
        bytesPerLine = 16;
    }
    static const char hexChars[] = "0123456789ABCDEF";
    QString out;
    out.reserve(data.size() * 4 + 64);
    for (int off = 0; off < data.size(); off += bytesPerLine) {
        const int lineLen = qMin(bytesPerLine, data.size() - off);
        QByteArray line;
        line.reserve(4 + bytesPerLine * 3 + 2 + bytesPerLine + 2);
        line.append(hexChars[(off >> 12) & 0xF]);
        line.append(hexChars[(off >> 8) & 0xF]);
        line.append(hexChars[(off >> 4) & 0xF]);
        line.append(hexChars[off & 0xF]);
        line.append("  ");
        for (int i = 0; i < bytesPerLine; ++i) {
            if (i < lineLen) {
                const uchar c = uchar(data[off + i]);
                line.append(hexChars[c >> 4]);
                line.append(hexChars[c & 0xF]);
                line.append(' ');
            } else {
                line.append("   ");
            }
        }
        line.append(" |");
        for (int i = 0; i < lineLen; ++i) {
            const uchar c = uchar(data[off + i]);
            line.append((c >= 0x20 && c < 0x7F) ? char(c) : '.');
        }
        line.append('|');
        out += QString::fromLatin1(line);
        out += QLatin1Char('\n');
    }
    return out;
}

#endif // HEXDUMP_H
