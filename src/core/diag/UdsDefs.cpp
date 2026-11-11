#include "diag/UdsDefs.h"

#include <bit>

namespace ecusim {

QString nrcName(quint8 nrc)
{
    switch (nrc) {
    case Nrc::GeneralReject: return QStringLiteral("generalReject");
    case Nrc::ServiceNotSupported: return QStringLiteral("serviceNotSupported");
    case Nrc::SubFunctionNotSupported: return QStringLiteral("subFunctionNotSupported");
    case Nrc::IncorrectMessageLength: return QStringLiteral("incorrectMessageLengthOrInvalidFormat");
    case Nrc::ResponseTooLong: return QStringLiteral("responseTooLong");
    case Nrc::ConditionsNotCorrect: return QStringLiteral("conditionsNotCorrect");
    case Nrc::RequestSequenceError: return QStringLiteral("requestSequenceError");
    case Nrc::RequestOutOfRange: return QStringLiteral("requestOutOfRange");
    case Nrc::SecurityAccessDenied: return QStringLiteral("securityAccessDenied");
    case Nrc::InvalidKey: return QStringLiteral("invalidKey");
    case Nrc::ExceededNumberOfAttempts: return QStringLiteral("exceededNumberOfAttempts");
    case Nrc::RequiredTimeDelayNotExpired: return QStringLiteral("requiredTimeDelayNotExpired");
    case Nrc::ResponsePending: return QStringLiteral("requestCorrectlyReceived-ResponsePending");
    case Nrc::SubFunctionNotSupportedInActiveSession:
        return QStringLiteral("subFunctionNotSupportedInActiveSession");
    case Nrc::ServiceNotSupportedInActiveSession: return QStringLiteral("serviceNotSupportedInActiveSession");
    default: return QStringLiteral("NRC 0x%1").arg(nrc, 2, 16, QLatin1Char('0'));
    }
}

QString serviceName(quint8 sid)
{
    switch (sid) {
    case Sid::ObdCurrentData: return QStringLiteral("OBD Mode 01 - current data");
    case Sid::ObdFreezeFrame: return QStringLiteral("OBD Mode 02 - freeze frame");
    case Sid::ObdStoredDtcs: return QStringLiteral("OBD Mode 03 - stored DTCs");
    case Sid::ObdClearDtcs: return QStringLiteral("OBD Mode 04 - clear DTCs");
    case Sid::ObdPendingDtcs: return QStringLiteral("OBD Mode 07 - pending DTCs");
    case Sid::ObdVehicleInfo: return QStringLiteral("OBD Mode 09 - vehicle information");
    case Sid::DiagnosticSessionControl: return QStringLiteral("DiagnosticSessionControl");
    case Sid::EcuReset: return QStringLiteral("ECUReset");
    case Sid::ClearDiagnosticInformation: return QStringLiteral("ClearDiagnosticInformation");
    case Sid::ReadDtcInformation: return QStringLiteral("ReadDTCInformation");
    case Sid::ReadDataByIdentifier: return QStringLiteral("ReadDataByIdentifier");
    case Sid::SecurityAccess: return QStringLiteral("SecurityAccess");
    case Sid::InputOutputControlByIdentifier: return QStringLiteral("InputOutputControlByIdentifier");
    case Sid::TesterPresent: return QStringLiteral("TesterPresent");
    default: return QStringLiteral("Service 0x%1").arg(sid, 2, 16, QLatin1Char('0'));
    }
}

QString toHex(const QByteArray &data)
{
    return QString::fromLatin1(data.toHex(' ')).toUpper();
}

QByteArray fromHex(const QString &text)
{
    QString t = text;
    t.remove(QStringLiteral("0x"), Qt::CaseInsensitive);
    QString digits;
    for (const QChar c : std::as_const(t)) {
        if (c.isSpace() || c == QLatin1Char(',') || c == QLatin1Char(':') || c == QLatin1Char('-'))
            continue;
        const char l = c.toLatin1();
        const bool isHex = (l >= '0' && l <= '9') || (l >= 'a' && l <= 'f') || (l >= 'A' && l <= 'F');
        if (!isHex)
            return {};
        digits += c;
    }
    if (digits.isEmpty() || digits.size() % 2 != 0)
        return {};
    return QByteArray::fromHex(digits.toLatin1());
}

quint32 securityKeyFromSeed(quint32 seed)
{
    return std::rotl(seed ^ 0x5EC0DE5Au, 5) + 0x1234ABCDu;
}

} // namespace ecusim
