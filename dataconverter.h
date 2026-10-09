
#ifndef DATACONVERTER_H
#define DATACONVERTER_H

#include <QString>
#include <QByteArray>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QtGlobal>
#include <QDebug>

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif


class DataConverter
{
public:
    struct ConversionResult{
        bool success;
        QString data;
        QString errorMessage;


        ConversionResult(bool ok=true,const QString &result=QString(),
                         const QString &error=QString())
            :success(ok),data(result),errorMessage(error){}
    };


    struct ChecksumResult{
        QString crc16;
        QString crc32;
        QString md5;
        QString sha1;
        QString sha256;
    };


    static ConversionResult convertBetween(const QString &value, int fromBase, int toBase);


    static ConversionResult decimalToBinary(const QString &decimalString);


    static ConversionResult binaryToDecimal(const QString &binaryString);




    static int countBytes(const QString &data);


    static int countCharacter(const QString &data);


    static QString getDataInfo(const QString &data);




    static ChecksumResult calculateChecksums(const QString &data);


    static ChecksumResult calculateChecksumsFromBytes(const QByteArray &data);


    static QString calculateMD5(const QString &data);


    static QString calculateSHA1(const QString &data);


    static QString calculateSHA256(const QString &data);


    static quint16 calculateCRC16(const QByteArray &data);


    static quint32 calculateCRC32(const QByteArray &data);


public:

    DataConverter()=default;
    static const quint16 crc16Table[256];
    static const quint32 crc32Table[256];

};

#endif // DATACONVERTER_H
