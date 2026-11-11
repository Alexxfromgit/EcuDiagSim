#pragma once

#include <QByteArray>
#include <QString>

namespace ecusim {

/// ISO 14229-1 negative response codes used by the simulator.
namespace Nrc {
enum : quint8 {
    GeneralReject = 0x10,
    ServiceNotSupported = 0x11,
    SubFunctionNotSupported = 0x12,
    IncorrectMessageLength = 0x13,
    ResponseTooLong = 0x14,
    ConditionsNotCorrect = 0x22,
    RequestSequenceError = 0x24,
    RequestOutOfRange = 0x31,
    SecurityAccessDenied = 0x33,
    InvalidKey = 0x35,
    ExceededNumberOfAttempts = 0x36,
    RequiredTimeDelayNotExpired = 0x37,
    ResponsePending = 0x78,
    SubFunctionNotSupportedInActiveSession = 0x7E,
    ServiceNotSupportedInActiveSession = 0x7F,
};
} // namespace Nrc

namespace Sid {
enum : quint8 {
    ObdCurrentData = 0x01,
    ObdFreezeFrame = 0x02,
    ObdStoredDtcs = 0x03,
    ObdClearDtcs = 0x04,
    ObdPendingDtcs = 0x07,
    ObdVehicleInfo = 0x09,
    DiagnosticSessionControl = 0x10,
    EcuReset = 0x11,
    ClearDiagnosticInformation = 0x14,
    ReadDtcInformation = 0x19,
    ReadDataByIdentifier = 0x22,
    SecurityAccess = 0x27,
    InputOutputControlByIdentifier = 0x2F,
    TesterPresent = 0x3E,
    NegativeResponse = 0x7F,
};
} // namespace Sid

/// Diagnostic CAN identifiers (ISO 15765-4, 11 bit).
namespace DiagId {
constexpr quint32 Functional = 0x7DF;
constexpr quint32 EcmRequest = 0x7E0;
constexpr quint32 TcmRequest = 0x7E1;
constexpr quint32 AbsRequest = 0x7E2;
constexpr quint32 BcmRequest = 0x7E3;
constexpr quint32 ResponseOffset = 8; ///< response ID = request ID + 8
inline bool isDiagnostic(quint32 id) { return id >= 0x7DF && id <= 0x7EF; }
} // namespace DiagId

QString nrcName(quint8 nrc);
QString serviceName(quint8 sid);

/// "7F 22 31"
QString toHex(const QByteArray &data);
/// Accepts "7F2231", "7F 22 31", "0x7F,0x22". Returns an empty array on bad input.
QByteArray fromHex(const QString &text);

/// Educational seed/key algorithm for SecurityAccess (0x27). Not secure by design:
/// key = rotl32(seed XOR 0x5EC0DE5A, 5) + 0x1234ABCD
quint32 securityKeyFromSeed(quint32 seed);

} // namespace ecusim
