// Broker集成测试：拉起真实FormMqttServer，用QTcpSocket模拟MQTT客户端验证端到端行为
// 覆盖：连接握手、QoS0/1/2投递、通配符订阅、retain存储/清除/下发、遗嘱发布、保活超时、出站重传
#include <gtest/gtest.h>

#include <QApplication>
#include <QCoreApplication>
#include <QEventLoop>
#include <QDeadlineTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QComboBox>
#include <QSpinBox>
#include <QMetaObject>
#include <cstdio>
#include <functional>

#include "formmqttserver.h"
#include "mqttprotocol.h"

// 功能：自旋事件循环直到条件满足或超时（超时返回false）
static bool waitUntil(const std::function<bool()> &condition, int timeoutMs)
{
    QDeadlineTimer deadline(timeoutMs);
    while (!condition()) {
        if (deadline.hasExpired()) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
    }
    return true;
}

// 功能：空转事件循环指定时长（等待Broker异步处理，无确认报文可等时使用）
static void drainEvents(int ms)
{
    QDeadlineTimer deadline(ms);
    while (!deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
    }
}

// 测试用MQTT客户端：封装QTcpSocket，自动把字节流切分为完整报文存入队列
class MqttTestClient : public QObject
{
public:
    explicit MqttTestClient(QObject *parent = nullptr)
        : QObject(parent)
    {
        m_socket = new QTcpSocket(this);
        connect(m_socket, &QTcpSocket::readyRead, this, &MqttTestClient::onReadyRead);
    }

    bool connectTo(quint16 port, int timeoutMs = 3000)
    {
        m_socket->connectToHost("127.0.0.1", port);
        return waitUntil([this]() {
            return m_socket->state() == QAbstractSocket::ConnectedState;
        }, timeoutMs);
    }

    bool isConnected() const { return m_socket->state() == QAbstractSocket::ConnectedState; }

    void sendRaw(const QByteArray &packet) { m_socket->write(packet); }

    // 发送CONNECT并等待CONNACK（返回码0=接受）
    bool sendConnect(const QString &clientId, quint16 keepAlive = 0,
                     const MqttProtocol::WillConfig &will = MqttProtocol::WillConfig())
    {
        sendRaw(MqttProtocol::makeConnectPacket(clientId, keepAlive, true, will));
        if (!waitForPacketType(MqttProtocol::ConnAck)) {
            return false;
        }
        return m_lastBody.size() >= 2 && static_cast<quint8>(m_lastBody.at(1)) == 0x00;
    }

    void sendPublish(const QString &topic, const QByteArray &payload,
                     quint8 qos = 0, bool retain = false, quint16 packetId = 0)
    {
        sendRaw(MqttProtocol::makePublishPacket(topic, payload, qos, retain, packetId));
    }

    bool sendSubscribe(quint16 packetId, const QString &filter, quint8 qos)
    {
        sendRaw(MqttProtocol::makeSubscribePacket(packetId, filter, qos));
        return waitForPacketType(MqttProtocol::SubAck);
    }

    bool sendUnsubscribe(quint16 packetId, const QString &filter)
    {
        sendRaw(MqttProtocol::makeUnsubscribePacket(packetId, filter));
        return waitForPacketType(MqttProtocol::UnsubAck);
    }

    void sendDisconnect() { sendRaw(MqttProtocol::makeDisconnectPacket()); }
    void sendPubAck(quint16 packetId) { sendRaw(MqttProtocol::makePubAckPacket(packetId)); }
    void sendPubRec(quint16 packetId) { sendRaw(MqttProtocol::makePubRecPacket(packetId)); }
    void sendPubRel(quint16 packetId) { sendRaw(MqttProtocol::makePubRelPacket(packetId)); }
    void sendPubComp(quint16 packetId) { sendRaw(MqttProtocol::makePubCompPacket(packetId)); }
    void abortNow() { m_socket->abort(); }

    bool waitForDisconnected(int timeoutMs = 3000)
    {
        return waitUntil([this]() {
            return m_socket->state() == QAbstractSocket::UnconnectedState;
        }, timeoutMs);
    }

    // 等待并弹出队列中最早的指定类型报文（FIFO查找，保持顺序）
    bool waitForPacketType(quint8 type, int timeoutMs = 3000)
    {
        QDeadlineTimer deadline(timeoutMs);
        for (;;) {
            for (int i = 0; i < m_types.size(); ++i) {
                if (MqttProtocol::packetType(m_types.at(i)) == type) {
                    m_lastTypeAndFlags = m_types.takeAt(i);
                    m_lastBody = m_bodies.takeAt(i);
                    return true;
                }
            }
            if (deadline.hasExpired()) {
                return false;
            }
            QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
        }
    }

    quint8 lastTypeAndFlags() const { return m_lastTypeAndFlags; }
    QByteArray lastBody() const { return m_lastBody; }
    int packetCount() const { return m_types.size(); }
    void clearPackets() { m_types.clear(); m_bodies.clear(); }

    // 解析最近一次收到的报文为PUBLISH信息
    MqttProtocol::PublishInfo lastPublish() const
    {
        return MqttProtocol::decodePublishPacket(m_lastTypeAndFlags, m_lastBody);
    }

private:
    void onReadyRead()
    {
        m_buffer.append(m_socket->readAll());
        int pos = 0;
        quint8 typeAndFlags = 0;
        QByteArray body;
        while (MqttProtocol::decodePacket(m_buffer, pos, typeAndFlags, body) ==
               MqttProtocol::DecodeResult::Ok) {
            m_types.append(typeAndFlags);
            m_bodies.append(body);
        }
        m_buffer.remove(0, pos);
    }

    QTcpSocket *m_socket = nullptr;
    QByteArray m_buffer;
    QList<quint8> m_types;      // 已切分的完整报文（固定报头首字节）
    QList<QByteArray> m_bodies; // 对应报文体
    quint8 m_lastTypeAndFlags = 0;
    QByteArray m_lastBody;
};

// 测试夹具：每个用例启动一个监听随机空闲端口的Broker实例
class BrokerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_server = new FormMqttServer;
        m_port = pickFreePort();
        ASSERT_NE(m_port, 0);

        // 通过控件名设置监听地址/端口，再以元对象方式调用私有槽（与点击按钮等效）
        auto *ipCombo = m_server->findChild<QComboBox *>("comboBox_MQTTServerIP");
        auto *portSpin = m_server->findChild<QSpinBox *>("spinBox_MQTTServerPort");
        ASSERT_NE(ipCombo, nullptr);
        ASSERT_NE(portSpin, nullptr);
        ipCombo->setEditText("127.0.0.1");
        portSpin->setValue(m_port);
        QMetaObject::invokeMethod(m_server, "on_pushButton_MQTTServerStart_clicked",
                                  Qt::DirectConnection);

        // 用一条普通TCP连接探测监听是否成功
        ASSERT_TRUE(waitUntil([this]() { return tcpProbeOk(); }, 3000));
    }

    void TearDown() override
    {
        if (m_server) {
            QMetaObject::invokeMethod(m_server, "on_pushButton_MQTTServerStop_clicked",
                                      Qt::DirectConnection);
            delete m_server;
            m_server = nullptr;
        }
    }

    FormMqttServer *m_server = nullptr;
    quint16 m_port = 0;

private:
    static quint16 pickFreePort()
    {
        QTcpServer probe;
        if (!probe.listen(QHostAddress::LocalHost, 0)) {
            return 0;
        }
        const quint16 port = probe.serverPort();
        probe.close();
        return port;
    }

    bool tcpProbeOk()
    {
        QTcpSocket probe;
        probe.connectToHost("127.0.0.1", m_port);
        if (!probe.waitForConnected(500)) {
            return false;
        }
        probe.disconnectFromHost();
        return true;
    }
};

// ==================== 连接握手 ====================

TEST_F(BrokerTest, ConnectReturnsConnAck)
{
    MqttTestClient client;
    ASSERT_TRUE(client.connectTo(m_port));
    ASSERT_TRUE(client.sendConnect("c1"));
    ASSERT_EQ(client.lastBody().size(), 2);
    ASSERT_EQ(static_cast<quint8>(client.lastBody().at(1)), 0x00); // 返回码0=接受
    client.sendDisconnect();
    ASSERT_TRUE(client.waitForDisconnected());
}

TEST_F(BrokerTest, PingReqReturnsPingResp)
{
    MqttTestClient client;
    ASSERT_TRUE(client.connectTo(m_port));
    ASSERT_TRUE(client.sendConnect("c2"));
    client.sendRaw(MqttProtocol::makePingReqPacket());
    ASSERT_TRUE(client.waitForPacketType(MqttProtocol::PingResp));
}

TEST_F(BrokerTest, InvalidProtocolNameDisconnects)
{
    MqttTestClient client;
    ASSERT_TRUE(client.connectTo(m_port));
    // 手工构造协议名错误的CONNECT（协议名"XXXX"）
    QByteArray body;
    body.append(MqttProtocol::encodeUtf8String("XXXX"));
    body.append(static_cast<char>(0x04));
    body.append(static_cast<char>(0x02));
    body.append(static_cast<char>(0x00));
    body.append(static_cast<char>(0x00));
    body.append(MqttProtocol::encodeUtf8String("bad"));
    client.sendRaw(MqttProtocol::encodePacket(MqttProtocol::Connect, body));
    ASSERT_TRUE(client.waitForDisconnected());
}

TEST_F(BrokerTest, WillQos3Rejected)
{
    MqttTestClient client;
    ASSERT_TRUE(client.connectTo(m_port));
    MqttProtocol::WillConfig will;
    will.topic = "will/t";
    will.qos = 3; // 非法：遗嘱QoS只能是0/1/2
    client.sendRaw(MqttProtocol::makeConnectPacket("bad", 0, true, will));
    ASSERT_TRUE(client.waitForDisconnected());
}

TEST_F(BrokerTest, DuplicateClientIdKicksOldConnection)
{
    MqttTestClient first;
    ASSERT_TRUE(first.connectTo(m_port));
    ASSERT_TRUE(first.sendConnect("dup"));

    MqttTestClient second;
    ASSERT_TRUE(second.connectTo(m_port));
    ASSERT_TRUE(second.sendConnect("dup"));

    ASSERT_TRUE(first.waitForDisconnected()); // 旧连接被断开
    ASSERT_TRUE(second.isConnected());
}

// ==================== 发布与订阅 ====================

TEST_F(BrokerTest, Qos0PublishDeliveredToMatchingSubscriber)
{
    MqttTestClient sub;
    ASSERT_TRUE(sub.connectTo(m_port));
    ASSERT_TRUE(sub.sendConnect("sub"));
    ASSERT_TRUE(sub.sendSubscribe(1, "sensors/#", 0));

    MqttTestClient pub;
    ASSERT_TRUE(pub.connectTo(m_port));
    ASSERT_TRUE(pub.sendConnect("pub"));
    pub.sendPublish("sensors/temp", "25.5", 0);

    ASSERT_TRUE(sub.waitForPacketType(MqttProtocol::Publish));
    const auto info = sub.lastPublish();
    ASSERT_TRUE(info.valid);
    ASSERT_EQ(info.topic, QString("sensors/temp"));
    ASSERT_EQ(info.payload, QByteArray("25.5"));
    ASSERT_EQ(info.qos, 0);
    ASSERT_FALSE(info.retain);
}

TEST_F(BrokerTest, NoDeliveryToNonMatchingSubscriber)
{
    MqttTestClient sub;
    ASSERT_TRUE(sub.connectTo(m_port));
    ASSERT_TRUE(sub.sendConnect("sub"));
    ASSERT_TRUE(sub.sendSubscribe(1, "a/b", 0));

    MqttTestClient pub;
    ASSERT_TRUE(pub.connectTo(m_port));
    ASSERT_TRUE(pub.sendConnect("pub"));
    pub.sendPublish("a/c", "x", 0);

    ASSERT_FALSE(sub.waitForPacketType(MqttProtocol::Publish, 500)); // 超时未收到
}

TEST_F(BrokerTest, WildcardFilters)
{
    MqttTestClient hashSub;
    ASSERT_TRUE(hashSub.connectTo(m_port));
    ASSERT_TRUE(hashSub.sendConnect("hashSub"));
    ASSERT_TRUE(hashSub.sendSubscribe(1, "#", 0));

    MqttTestClient plusSub;
    ASSERT_TRUE(plusSub.connectTo(m_port));
    ASSERT_TRUE(plusSub.sendConnect("plusSub"));
    ASSERT_TRUE(plusSub.sendSubscribe(1, "a/+", 0));

    MqttTestClient pub;
    ASSERT_TRUE(pub.connectTo(m_port));
    ASSERT_TRUE(pub.sendConnect("pub"));

    pub.sendPublish("a/b", "1", 0);
    ASSERT_TRUE(hashSub.waitForPacketType(MqttProtocol::Publish));
    ASSERT_TRUE(plusSub.waitForPacketType(MqttProtocol::Publish));
    ASSERT_EQ(plusSub.lastPublish().topic, QString("a/b"));

    pub.sendPublish("a/b/c", "2", 0); // 多层主题："a/+"不匹配
    ASSERT_TRUE(hashSub.waitForPacketType(MqttProtocol::Publish));
    ASSERT_EQ(hashSub.lastPublish().payload, QByteArray("2"));
    ASSERT_FALSE(plusSub.waitForPacketType(MqttProtocol::Publish, 400));

    pub.sendPublish("c", "3", 0);
    ASSERT_TRUE(hashSub.waitForPacketType(MqttProtocol::Publish));
    ASSERT_EQ(hashSub.lastPublish().payload, QByteArray("3"));
}

TEST_F(BrokerTest, Qos1PublishAckAndDelivery)
{
    MqttTestClient sub;
    ASSERT_TRUE(sub.connectTo(m_port));
    ASSERT_TRUE(sub.sendConnect("sub"));
    ASSERT_TRUE(sub.sendSubscribe(1, "q1", 1)); // 授权QoS1

    MqttTestClient pub;
    ASSERT_TRUE(pub.connectTo(m_port));
    ASSERT_TRUE(pub.sendConnect("pub"));
    pub.sendPublish("q1", "data", 1, false, 10);

    ASSERT_TRUE(pub.waitForPacketType(MqttProtocol::PubAck)); // 发布者收到确认
    ASSERT_TRUE(sub.waitForPacketType(MqttProtocol::Publish));
    const auto info = sub.lastPublish();
    ASSERT_EQ(info.qos, 1);
    ASSERT_EQ(info.payload, QByteArray("data"));
    sub.sendPubAck(info.packetId); // 完成出站QoS1，避免重传噪音
}

TEST_F(BrokerTest, QosDowngradedToGrantedQos)
{
    MqttTestClient sub;
    ASSERT_TRUE(sub.connectTo(m_port));
    ASSERT_TRUE(sub.sendConnect("sub"));
    ASSERT_TRUE(sub.sendSubscribe(1, "downgrade", 0)); // 授权QoS0

    MqttTestClient pub;
    ASSERT_TRUE(pub.connectTo(m_port));
    ASSERT_TRUE(pub.sendConnect("pub"));
    pub.sendPublish("downgrade", "x", 1, false, 11); // 发布QoS1

    ASSERT_TRUE(pub.waitForPacketType(MqttProtocol::PubAck));
    ASSERT_TRUE(sub.waitForPacketType(MqttProtocol::Publish));
    ASSERT_EQ(sub.lastPublish().qos, 0); // 按min(1, 0)=0投递
}

TEST_F(BrokerTest, Qos2InboundFourStepHandshake)
{
    MqttTestClient sub;
    ASSERT_TRUE(sub.connectTo(m_port));
    ASSERT_TRUE(sub.sendConnect("sub"));
    ASSERT_TRUE(sub.sendSubscribe(1, "q2", 2));

    MqttTestClient pub;
    ASSERT_TRUE(pub.connectTo(m_port));
    ASSERT_TRUE(pub.sendConnect("pub"));
    pub.sendPublish("q2", "exactly-once", 2, false, 20);

    ASSERT_TRUE(pub.waitForPacketType(MqttProtocol::PubRec)); // 第一步：PUBLISH→PUBREC
    // 第二步前订阅者不应收到消息（PUBREL到达才投递）
    ASSERT_FALSE(sub.waitForPacketType(MqttProtocol::Publish, 300));

    pub.sendPubRel(20); // 第三步：PUBREL
    ASSERT_TRUE(pub.waitForPacketType(MqttProtocol::PubComp)); // 第四步：PUBCOMP

    ASSERT_TRUE(sub.waitForPacketType(MqttProtocol::Publish));
    const auto info = sub.lastPublish();
    ASSERT_EQ(info.payload, QByteArray("exactly-once"));
    ASSERT_EQ(info.qos, 2);
    const quint16 outPacketId = info.packetId;
    sub.sendPubRec(outPacketId); // 完成订阅者侧出站QoS2握手
    ASSERT_TRUE(sub.waitForPacketType(MqttProtocol::PubRel));
    sub.sendPubComp(outPacketId);
}

TEST_F(BrokerTest, Qos2DuplicatePublishNotDeliveredTwice)
{
    MqttTestClient sub;
    ASSERT_TRUE(sub.connectTo(m_port));
    ASSERT_TRUE(sub.sendConnect("sub"));
    ASSERT_TRUE(sub.sendSubscribe(1, "dup2", 2));

    MqttTestClient pub;
    ASSERT_TRUE(pub.connectTo(m_port));
    ASSERT_TRUE(pub.sendConnect("pub"));

    const QByteArray publish = MqttProtocol::makePublishPacket("dup2", "once", 2, false, 30);
    pub.sendRaw(publish);
    ASSERT_TRUE(pub.waitForPacketType(MqttProtocol::PubRec));
    pub.sendRaw(publish); // 模拟重传：同报文标识符重复PUBLISH
    ASSERT_TRUE(pub.waitForPacketType(MqttProtocol::PubRec)); // 只重发PUBREC
    pub.sendPubRel(30);
    ASSERT_TRUE(pub.waitForPacketType(MqttProtocol::PubComp));

    ASSERT_TRUE(sub.waitForPacketType(MqttProtocol::Publish));
    ASSERT_EQ(sub.lastPublish().payload, QByteArray("once"));
    const quint16 outPacketId = sub.lastPublish().packetId;
    sub.sendPubRec(outPacketId);
    ASSERT_TRUE(sub.waitForPacketType(MqttProtocol::PubRel));
    sub.sendPubComp(outPacketId);

    ASSERT_FALSE(sub.waitForPacketType(MqttProtocol::Publish, 400)); // 无重复投递
}

TEST_F(BrokerTest, UnsubscribeStopsDelivery)
{
    MqttTestClient sub;
    ASSERT_TRUE(sub.connectTo(m_port));
    ASSERT_TRUE(sub.sendConnect("sub"));
    ASSERT_TRUE(sub.sendSubscribe(1, "u/#", 0));
    ASSERT_TRUE(sub.sendUnsubscribe(2, "u/#"));

    MqttTestClient pub;
    ASSERT_TRUE(pub.connectTo(m_port));
    ASSERT_TRUE(pub.sendConnect("pub"));
    pub.sendPublish("u/x", "x", 0);

    ASSERT_FALSE(sub.waitForPacketType(MqttProtocol::Publish, 500));
}

// ==================== Retain保留消息 ====================

TEST_F(BrokerTest, RetainedMessageDeliveredToNewSubscriber)
{
    MqttTestClient pub;
    ASSERT_TRUE(pub.connectTo(m_port));
    ASSERT_TRUE(pub.sendConnect("pub"));
    pub.sendPublish("r/status", "online", 0, true); // retain=1

    MqttTestClient sub;
    ASSERT_TRUE(sub.connectTo(m_port));
    ASSERT_TRUE(sub.sendConnect("sub"));
    ASSERT_TRUE(sub.sendSubscribe(1, "r/#", 0));

    ASSERT_TRUE(sub.waitForPacketType(MqttProtocol::Publish));
    const auto info = sub.lastPublish();
    ASSERT_EQ(info.topic, QString("r/status"));
    ASSERT_EQ(info.payload, QByteArray("online"));
    ASSERT_TRUE(info.retain); // 下发保留消息时retain标志置1
}

TEST_F(BrokerTest, RetainedMessageClearedByEmptyPayload)
{
    MqttTestClient pub;
    ASSERT_TRUE(pub.connectTo(m_port));
    ASSERT_TRUE(pub.sendConnect("pub"));
    pub.sendPublish("r/clear", "value", 0, true);

    MqttTestClient sub1;
    ASSERT_TRUE(sub1.connectTo(m_port));
    ASSERT_TRUE(sub1.sendConnect("sub1"));
    ASSERT_TRUE(sub1.sendSubscribe(1, "r/clear", 0));
    ASSERT_TRUE(sub1.waitForPacketType(MqttProtocol::Publish)); // 确认已存储
    ASSERT_EQ(sub1.lastPublish().payload, QByteArray("value"));

    pub.sendPublish("r/clear", QByteArray(), 0, true); // 空载荷清除
    drainEvents(200);

    MqttTestClient sub2;
    ASSERT_TRUE(sub2.connectTo(m_port));
    ASSERT_TRUE(sub2.sendConnect("sub2"));
    ASSERT_TRUE(sub2.sendSubscribe(1, "r/clear", 0));
    ASSERT_FALSE(sub2.waitForPacketType(MqttProtocol::Publish, 500)); // 已清除
}

TEST_F(BrokerTest, RetainedQosCappedByGrantedQos)
{
    MqttTestClient pub;
    ASSERT_TRUE(pub.connectTo(m_port));
    ASSERT_TRUE(pub.sendConnect("pub"));
    pub.sendPublish("r/q", "v", 2, true, 40); // 以QoS2发布并保留
    ASSERT_TRUE(pub.waitForPacketType(MqttProtocol::PubRec));
    pub.sendPubRel(40);
    ASSERT_TRUE(pub.waitForPacketType(MqttProtocol::PubComp));

    MqttTestClient sub;
    ASSERT_TRUE(sub.connectTo(m_port));
    ASSERT_TRUE(sub.sendConnect("sub"));
    ASSERT_TRUE(sub.sendSubscribe(1, "r/q", 1)); // 授权QoS1

    ASSERT_TRUE(sub.waitForPacketType(MqttProtocol::Publish));
    const auto info = sub.lastPublish();
    ASSERT_EQ(info.qos, 1); // min(存储QoS2, 授权QoS1)=1
    ASSERT_TRUE(info.retain);
    sub.sendPubAck(info.packetId);
}

// ==================== Will遗嘱 ====================

TEST_F(BrokerTest, WillPublishedOnAbnormalDisconnect)
{
    MqttTestClient sub;
    ASSERT_TRUE(sub.connectTo(m_port));
    ASSERT_TRUE(sub.sendConnect("sub"));
    ASSERT_TRUE(sub.sendSubscribe(1, "will/#", 0));

    MqttTestClient device;
    ASSERT_TRUE(device.connectTo(m_port));
    MqttProtocol::WillConfig will;
    will.topic = "will/dev1";
    will.message = "offline";
    ASSERT_TRUE(device.sendConnect("dev1", 0, will));

    device.abortNow(); // 模拟异常断开（进程崩溃/断网，未发DISCONNECT）
    ASSERT_TRUE(sub.waitForPacketType(MqttProtocol::Publish));
    const auto info = sub.lastPublish();
    ASSERT_EQ(info.topic, QString("will/dev1"));
    ASSERT_EQ(info.payload, QByteArray("offline"));
    ASSERT_EQ(info.qos, 0);
    ASSERT_FALSE(info.retain);
}

TEST_F(BrokerTest, NoWillOnCleanDisconnect)
{
    MqttTestClient sub;
    ASSERT_TRUE(sub.connectTo(m_port));
    ASSERT_TRUE(sub.sendConnect("sub"));
    ASSERT_TRUE(sub.sendSubscribe(1, "will/#", 0));

    MqttTestClient device;
    ASSERT_TRUE(device.connectTo(m_port));
    MqttProtocol::WillConfig will;
    will.topic = "will/dev2";
    will.message = "offline";
    ASSERT_TRUE(device.sendConnect("dev2", 0, will));

    device.sendDisconnect(); // 干净断开：遗嘱作废
    ASSERT_TRUE(device.waitForDisconnected());
    ASSERT_FALSE(sub.waitForPacketType(MqttProtocol::Publish, 600));
}

TEST_F(BrokerTest, WillRetainedStoredForFutureSubscribers)
{
    MqttTestClient sub1;
    ASSERT_TRUE(sub1.connectTo(m_port));
    ASSERT_TRUE(sub1.sendConnect("sub1"));
    ASSERT_TRUE(sub1.sendSubscribe(1, "will/#", 0));

    MqttTestClient device;
    ASSERT_TRUE(device.connectTo(m_port));
    MqttProtocol::WillConfig will;
    will.topic = "will/dev3";
    will.message = "gone";
    will.retain = true;
    ASSERT_TRUE(device.sendConnect("dev3", 0, will));

    device.abortNow();
    // 当前订阅者收到遗嘱（retain位=Will Retain）
    ASSERT_TRUE(sub1.waitForPacketType(MqttProtocol::Publish));
    ASSERT_EQ(sub1.lastPublish().payload, QByteArray("gone"));
    ASSERT_TRUE(sub1.lastPublish().retain);

    // 遗嘱已入保留消息存储：新订阅者也能收到
    MqttTestClient sub2;
    ASSERT_TRUE(sub2.connectTo(m_port));
    ASSERT_TRUE(sub2.sendConnect("sub2"));
    ASSERT_TRUE(sub2.sendSubscribe(1, "will/#", 0));
    ASSERT_TRUE(sub2.waitForPacketType(MqttProtocol::Publish));
    ASSERT_EQ(sub2.lastPublish().topic, QString("will/dev3"));
    ASSERT_EQ(sub2.lastPublish().payload, QByteArray("gone"));
    ASSERT_TRUE(sub2.lastPublish().retain);
}

TEST_F(BrokerTest, KeepAliveTimeoutPublishesWill)
{
    MqttTestClient sub;
    ASSERT_TRUE(sub.connectTo(m_port));
    ASSERT_TRUE(sub.sendConnect("sub"));
    ASSERT_TRUE(sub.sendSubscribe(1, "will/#", 0));

    MqttTestClient device;
    ASSERT_TRUE(device.connectTo(m_port));
    MqttProtocol::WillConfig will;
    will.topic = "will/dev4";
    will.message = "timeout";
    ASSERT_TRUE(device.sendConnect("dev4", 1, will)); // keepalive=1s，之后保持静默

    // 1.5倍阈值+1秒扫描周期：约2.5秒内被Broker按异常断开处理
    ASSERT_TRUE(device.waitForDisconnected(5000));
    ASSERT_TRUE(sub.waitForPacketType(MqttProtocol::Publish));
    ASSERT_EQ(sub.lastPublish().topic, QString("will/dev4"));
    ASSERT_EQ(sub.lastPublish().payload, QByteArray("timeout"));
}

// ==================== 出站重传 ====================

TEST_F(BrokerTest, Qos1DeliveryRetransmittedWithoutAck)
{
    MqttTestClient sub;
    ASSERT_TRUE(sub.connectTo(m_port));
    ASSERT_TRUE(sub.sendConnect("sub"));
    ASSERT_TRUE(sub.sendSubscribe(1, "retry", 1));

    MqttTestClient pub;
    ASSERT_TRUE(pub.connectTo(m_port));
    ASSERT_TRUE(pub.sendConnect("pub"));
    pub.sendPublish("retry", "r", 1, false, 50);
    ASSERT_TRUE(pub.waitForPacketType(MqttProtocol::PubAck));

    ASSERT_TRUE(sub.waitForPacketType(MqttProtocol::Publish));
    const quint16 packetId = sub.lastPublish().packetId;
    // 故意不发送PUBACK：约5秒后Broker重传同一报文标识符
    ASSERT_TRUE(sub.waitForPacketType(MqttProtocol::Publish, 8000));
    ASSERT_EQ(sub.lastPublish().packetId, packetId);
    ASSERT_EQ(sub.lastPublish().payload, QByteArray("r"));
    sub.sendPubAck(packetId); // 结束重传
}

// 自定义main：GTest断言之外还需要QApplication支撑QWidget与事件循环
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("NetToolsBrokerTest"); // 隔离QSettings，避免污染真实配置
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
