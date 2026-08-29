#ifndef OTAPROTOCOL_H
#define OTAPROTOCOL_H

#include <QByteArray>
#include <QObject>
#include <optional>

/**
 * SOIP v2 — STM32 OTA/IAP Serial Protocol
 * Frame: SOF(AA55) VER CMD SEQ LEN PAYLOAD CRC16
 * CRC16-CCITT over [VER .. end of PAYLOAD]
 */
class OtaProtocol
{
public:
    static constexpr quint8 kVersion = 0x02;
    static constexpr quint8 kSof0 = 0xAA;
    static constexpr quint8 kSof1 = 0x55;
    static constexpr int kHeaderSize = 8;
    static constexpr int kCrcSize = 2;
    /** 与固件 BSP_OTA_CHUNK_SIZE 对齐 */
    static constexpr int kDefaultChunkSize = 256;
    static constexpr int kMaxPayload = 1024;
    static constexpr int kMaxRetries = 5;
    /** 停等窗口=1 */
    static constexpr int kDataPipelineWindow = 1;
    /** ACK 后短间隔（常见 TCP OTA CHUNK_DELAY） */
    static constexpr int kDataInterPacketDelayMs = 2;
    /** 单包 ACK 超时；板端 SENDOK 正常应 <100ms */
    static constexpr int kDataAckTimeoutMs = 3000;
    /** 超时后同序重发前间隔 */
    static constexpr int kDataRetryDelayMs = 20;
    static constexpr int kStartTransferTimeoutMs = 60000;

    enum class Cmd : quint8 {
        Ping = 0x01,
        GetInfo = 0x02,
        BeginSession = 0x03,
        Erase = 0x04,
        StartTransfer = 0x05,
        Data = 0x06,
        EndTransfer = 0x07,
        Verify = 0x08,
        Reset = 0x09,
        Abort = 0x0A,
        ResponseMask = 0x80
    };

    enum class Status : quint8 {
        Ok = 0x00,
        ErrCrc = 0x01,
        ErrCmd = 0x02,
        ErrState = 0x03,
        ErrFlash = 0x04,
        ErrVerify = 0x05,
        ErrTimeout = 0x06,
        ErrBusy = 0x07,
        ErrParam = 0x08,
        ErrSession = 0x09,
        ErrAborted = 0x0A,
        ErrUnknown = 0xFF
    };

    struct Frame {
        Cmd cmd = Cmd::Ping;
        quint16 seq = 0;
        QByteArray payload;
        bool valid = false;
    };

    struct DeviceInfo {
        QString deviceType;
        quint32 chipId = 0;
        QString bootVersion;
        QString protocolVersion;
        quint32 flashSize = 0;
        quint32 appAddress = 0;
        bool valid = false;
    };

    static QByteArray buildFrame(Cmd cmd, quint16 seq, const QByteArray &payload = {});
    static std::optional<Frame> tryParseFrame(QByteArray &rxBuffer);
    static bool verifyFrameCrc(const QByteArray &frameRaw);
    static QString statusToString(Status s);
    static QString cmdToString(Cmd c);

    static QByteArray payloadPing();
    static QByteArray payloadBeginSession(quint32 hostNonce);
    static QByteArray payloadStartTransfer(quint32 sessionToken, quint32 flashAddr,
                                           quint32 imageSize, quint32 imageCrc32);
    static QByteArray payloadData(quint32 sessionToken, quint32 offset, const QByteArray &chunk);
    static QByteArray payloadErase(quint32 sessionToken, quint32 flashAddr, quint32 size);
    static QByteArray payloadEndTransfer(quint32 sessionToken);
    static QByteArray payloadVerify(quint32 sessionToken, quint32 imageCrc32);
    static QByteArray payloadAbort(quint32 sessionToken);

    static Status parseResponseStatus(const Frame &frame);
    static DeviceInfo parseDeviceInfo(const Frame &frame);
    static quint32 parseSessionToken(const Frame &frame);
};

#endif // OTAPROTOCOL_H
