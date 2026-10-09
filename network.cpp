#include "network.h"
#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif
Network::Network(QObject *parent)
    : QObject(parent)
    ,testMessageCount(0)
    ,receiveBuffer()
    ,performanceTimer()
    ,bufferMutex()
{
    socket=new QTcpSocket(this);
    timer=new QTimer(this);
    receiveBuffer.reserve(8192);
    setSocketOptions();
    // 处理建立信号槽连接：
    connect(socket,&QTcpSocket::disconnected,this,&Network::ClientDisconnectedFunc,Qt::QueuedConnection);
    connect(socket,&QTcpSocket::readyRead,this,&Network::ReadServerMsg,Qt::DirectConnection);
    connect(socket,&QTcpSocket::connected,this,&Network::onConnected,Qt::QueuedConnection);
    // 旧式语法
    connect(socket,SIGNAL(errorOccurred(QAbstractSocket::SocketError)),
            this,SLOT(onSocketError(QAbstractSocket::SocketError)));

    connect(timer,&QTimer::timeout,this,&Network::StartTimerOutFunc,Qt::DirectConnection);
    // 设置定时器类型为精确定时器
    timer->setTimerType(Qt::PreciseTimer);
}
Network::~Network(void)
{
    qDebug()<<"Network析构函数开始";
    try{
        if(timer){
            timer->stop();
            timer->disconnect();
        }

        if(socket){
            socket->disconnect();

            if(socket->state()!=QAbstractSocket::UnconnectedState){
                socket->disconnectFromHost();
                if(socket->state()!=QAbstractSocket::UnconnectedState){
                    socket->abort();
                }
            }
        }

    }catch(const std::exception& e){
        qDebug()<<"Network析构时发生异常"<<e.what();
    }catch(...){
        qDebug()<<"Network析构时发生未知异常";
    }
        qDebug()<<"Network析构函数完成";
}
bool Network::ClientConnectToServer(QString ServerIpAddress, int ServerPort)
{
    QAbstractSocket::SocketState currentState=socket->state();  // 调用state()方法获取当前套接字状态
    if(currentState==QAbstractSocket::ConnectingState || currentState==QAbstractSocket::ConnectedState)
    {
        if(socket->peerAddress().toString()==ServerIpAddress &&
            socket->peerPort()==ServerPort){
            qDebug()<<"Already connected to"<<ServerIpAddress<<":"<<ServerPort;
            return true;
        }
        qDebug()<<"Disconnecting from current connection before connecting to new address";
        socket->disconnectFromHost();
        if(socket->state()!=QAbstractSocket::UnconnectedState){
            socket->abort(); // 立即强制关闭套接字
        }
    }
    if(socket->state()!=QAbstractSocket::UnconnectedState){
        qWarning()<<"Socket is not in unconnected state, aborting connection";
        return false;   // 返回false，表示连接请示失败
    }
    qDebug()<<"Connecting to "<<ServerIpAddress<<":"<<ServerPort;
    socket->connectToHost(ServerIpAddress,ServerPort);
    return true;
}
// 客户端发送文本消息到服务器：将UTF-8编码的字符串发送到服务器
void Network::ClientSendMsgToServer(const QString &StrData)
{
    if(!socket || socket->state()!=QAbstractSocket::ConnectedState){
        qWarning()<<"Socket not connected, cannot send data";
        return;
    }
    const QByteArray data=StrData.toUtf8();
    const qint64 bytesWritten=socket->write(data);
    if(bytesWritten<0){
        qWarning()<<"Write failed:"<<socket->errorString();
        return ;
    }
    if(bytesWritten>0){
        QMutexLocker locket(&bufferMutex);
    }
}
// 客户端发送原始字节数据到服务器：直接发送字节数组，不进行编码转换
void Network::ClientSendBytesToServer(const QByteArray &data)
{
    if(!socket || socket->state() !=QAbstractSocket::ConnectedState){
        qWarning()<<"Socket not connected, cannot send data";
        return;
    }
    const qint64 bytesWritten=socket->write(data);
    if(bytesWritten>0){
        QMutexLocker locket(&bufferMutex);
    }
}

// 设置套接字选项：配置TCP套接字的性能参数和连接参数
void Network::setSocketOptions()
{
    if(!socket)
        return;
    socket->setSocketOption(QAbstractSocket::LowDelayOption,1);
    socket->setSocketOption(QAbstractSocket::KeepAliveOption,1);
    socket->setReadBufferSize(16384);
    socket->setSocketOption(QAbstractSocket::SendBufferSizeSocketOption,32768);
    socket->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption,32768);
}
Network::ConnectionStats Network::getConnectionStats() const
{
    ConnectionStats stats;
    return stats;
}
void Network::ClientDisconnectedFunc(void)
{
    if(!timer){
        qWarning()<<"Network::ClientDisconnectedFunc: timer is null";
        return;
    }
    timer->stop();
}
void Network::DisconnectFromHost()
{
    if(!socket){
        qWarning()<<"Network::DisconnectFromHost: socket is null";
        return;
    }
    socket->disconnectFromHost();
}
void Network::ReadServerMsg()
{
    if(!socket || socket->state()!=QAbstractSocket::ConnectedState){
        qWarning()<<"Socket not ready, skip read";
        return;
    }
    QMutexLocker locker(&bufferMutex);
    const qint64 bytesAvailable=socket->bytesAvailable();
    if(bytesAvailable<=0){
        return;
    }
    static const qint64 kMaxBytes=1024*1024; // 1048576 1MB
    if(bytesAvailable>kMaxBytes){
        qWarning()<<"Too much incoming data, drop packet of size"<<bytesAvailable;

        socket->read(bytesAvailable);

        return;
    }
    if(receiveBuffer.capacity()<bytesAvailable){
        // 扩展缓冲区容量：bytesAvailable*2
        receiveBuffer.reserve(bytesAvailable*2);
    }
    receiveBuffer=socket->readAll();
    strTempData=QString::fromUtf8(receiveBuffer);
    locker.unlock();
    emit dataReceived(strTempData);
#ifdef QT_DEBUG
    static qint64 totalBytesReceived=0;
    totalBytesReceived+=bytesAvailable;
    qDebug()<<"Received"<<bytesAvailable<<"bytes，total:"<<totalBytesReceived;
#endif
}
// 启动定时器超时功能：启动自动测试模式，定时向服务器发送测试消息
void Network::StartTimerOutFunc()
{
    if(!socket){
        qWarning()<<"Network::StartTimerOutFunc: socket is null";
        return;
    }
    if(!timer){
        qWarning()<<"Network::StartTimerOutFunc: timer is null";
        return;
    }
    if(socket->state()!=QAbstractSocket::ConnectedState){
        timer->stop();
        return;
    }
    const int currentCount=testMessageCount.fetch_add(1);
    QString messageToSend;
    if(!autoTestMessage.isEmpty()){
        messageToSend=autoTestMessage;
        if(messageToSend.contains("%1")){
            messageToSend=messageToSend.arg(currentCount);
        }
    }
    else
    {
        messageToSend=QStringLiteral("\n[Prompt:Client automatic testing(%1)]").arg(currentCount);
    }
    ClientSendMsgToServer(messageToSend);
    if(!timer->isActive()){
        timer->start(1500);
    }
}
void Network::setAutoTestMessage(const QString &message)
{
    autoTestMessage=message;
}
void Network::StopTimerOutFunc()
{
    timer->stop();
}
// 连接建立回调：当socket成功连接到服务器时自动调用
void Network::onConnected()
{
    testMessageCount.store(0);
    performanceTimer.start();
    setSocketOptions();
    emit connectionEstablished();
}
// 套接字错误处理回调：当socket发生错误时自动调用
void Network::onSocketError(QAbstractSocket::SocketError error)
{
    QString errorString;
    switch(error){
    case QAbstractSocket::ConnectionRefusedError:
        errorString="连接被拒绝";
        break;
    case QAbstractSocket::RemoteHostClosedError:
        errorString="远程主机关闭连接";
        break;
    case QAbstractSocket::HostNotFoundError:
        errorString="主机未找到";
        break;
    case QAbstractSocket::SocketTimeoutError:
        errorString="连接超时";
        break;
    case QAbstractSocket::NetworkError:
        errorString="网络错误";
        break;
    default:
        errorString=socket->errorString();
        break;
    }
    emit connectionFailed(errorString);
}








