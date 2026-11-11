#include "tester/Decode.h"

#include "diag/DtcCatalog.h"
#include "diag/DtcManager.h"
#include "diag/UdsDefs.h"

namespace ecusim {

namespace {
quint8 u8(const QByteArray &a, qsizetype i)
{
    return static_cast<quint8>(a.at(i));
}
quint32 u24(const QByteArray &a, qsizetype i)
{
    return (quint32{u8(a, i)} << 16) | (quint32{u8(a, i + 1)} << 8) | u8(a, i + 2);
}
} // namespace

const QList<DidInfo> &didCatalog()
{
    static const QList<DidInfo> catalog = {
        {0xF186, "Active diagnostic session", DidInfo::U8, 1, 1, 0, ""},
        {0xF187, "Spare part number", DidInfo::Ascii, 12, 1, 0, ""},
        {0xF18C, "ECU serial number", DidInfo::Ascii, 12, 1, 0, ""},
        {0xF190, "VIN", DidInfo::Ascii, 17, 1, 0, ""},
        {0xF195, "Software version", DidInfo::Ascii, 12, 1, 0, ""},
        // OBD PIDs mapped to DIDs 0xF4xx
        {0xF400, "Supported PIDs 01-20", DidInfo::Raw, 4, 1, 0, ""},
        {0xF401, "Monitor status", DidInfo::Raw, 4, 1, 0, ""},
        {0xF404, "Calculated engine load", DidInfo::U8, 1, 100.0 / 255.0, 0, "%"},
        {0xF405, "Engine coolant temperature", DidInfo::U8, 1, 1, -40, "°C"},
        {0xF40C, "Engine speed", DidInfo::U16, 2, 0.25, 0, "rpm"},
        {0xF40D, "Vehicle speed", DidInfo::U8, 1, 1, 0, "km/h"},
        {0xF411, "Throttle position", DidInfo::U8, 1, 100.0 / 255.0, 0, "%"},
        {0xF41F, "Run time since engine start", DidInfo::U16, 2, 1, 0, "s"},
        {0xF420, "Supported PIDs 21-40", DidInfo::Raw, 4, 1, 0, ""},
        {0xF440, "Supported PIDs 41-60", DidInfo::Raw, 4, 1, 0, ""},
        {0xF442, "Control module voltage", DidInfo::U16, 2, 0.001, 0, "V"},
        // ECM
        {0x1001, "Coolant sensor voltage", DidInfo::U16, 2, 0.001, 0, "V"},
        {0x1002, "Throttle sensor voltage", DidInfo::U16, 2, 0.001, 0, "V"},
        {0x1003, "Engine state (bit0 running, bit1 limp, bit2 MIL)", DidInfo::U8, 1, 1, 0, ""},
        // TCM
        {0x2001, "Current gear", DidInfo::U8, 1, 1, 0, ""},
        {0x2002, "Transmission fluid temperature", DidInfo::U8, 1, 1, -40, "°C"},
        {0x2003, "Fluid temp. sensor voltage", DidInfo::U16, 2, 0.001, 0, "V"},
        // ABS
        {0x3001, "Wheel speed FL", DidInfo::U16, 2, 0.01, 0, "km/h"},
        {0x3002, "Wheel speed FR", DidInfo::U16, 2, 0.01, 0, "km/h"},
        {0x3003, "Wheel speed RL", DidInfo::U16, 2, 0.01, 0, "km/h"},
        {0x3004, "Wheel speed RR", DidInfo::U16, 2, 0.01, 0, "km/h"},
        {0x3005, "Brake pressure", DidInfo::U8, 1, 1, 0, "bar"},
        {0x3010, "ABS warning lamp output", DidInfo::U8, 1, 1, 0, ""},
        // BCM
        {0x4001, "Battery voltage", DidInfo::U8, 1, 0.1, 0, "V"},
        {0x4002, "Exterior lamps (bit0 low, bit1 high, bit2 park, bit3 left, bit4 right)", DidInfo::U8, 1, 1, 0, ""},
        {0x4003, "Doors (bit0-3 ajar FL/FR/RL/RR, bit7 locked)", DidInfo::U8, 1, 1, 0, ""},
        {0x4010, "Low beam output", DidInfo::U8, 1, 1, 0, ""},
        {0x4011, "Door lock output", DidInfo::U8, 1, 1, 0, ""},
    };
    return catalog;
}

const DidInfo *didInfo(quint16 did)
{
    for (const DidInfo &d : didCatalog())
        if (d.did == did)
            return &d;
    return nullptr;
}

DecodedValue decodeDid(quint16 did, const QByteArray &data)
{
    DecodedValue v;
    v.did = did;
    const DidInfo *info = didInfo(did);
    if (!info) {
        v.name = QStringLiteral("DID 0x%1").arg(did, 4, 16, QLatin1Char('0')).toUpper();
        v.value = toHex(data);
        return v;
    }
    v.name = QString::fromUtf8(info->name);
    v.unit = QString::fromUtf8(info->unit);

    if (did == 0xF401 && data.size() >= 1) {
        const quint8 a = u8(data, 0);
        v.number = a & 0x7F;
        v.value = QStringLiteral("MIL %1, %2 confirmed DTC(s)").arg(QLatin1String(a & 0x80 ? "ON" : "off")).arg(a & 0x7F);
        return v;
    }
    switch (info->kind) {
    case DidInfo::U8:
    case DidInfo::U16: {
        const int need = info->kind == DidInfo::U8 ? 1 : 2;
        if (data.size() < need) {
            v.value = toHex(data);
            break;
        }
        const double raw = need == 1 ? u8(data, 0) : (u8(data, 0) << 8) | u8(data, 1);
        v.number = raw * info->factor + info->offset;
        const bool integral = info->factor == 1.0;
        v.value = QString::number(v.number, 'f', integral ? 0 : (info->factor < 0.1 ? 2 : 1));
        break;
    }
    case DidInfo::Ascii:
        v.value = QString::fromLatin1(data).trimmed();
        break;
    case DidInfo::Raw:
        v.value = toHex(data);
        break;
    }
    return v;
}

QList<DtcInfo> parseUdsDtcList(const QByteArray &response)
{
    QList<DtcInfo> list;
    if (response.size() < 3 || u8(response, 0) != 0x59)
        return list;
    for (qsizetype i = 3; i + 3 < response.size(); i += 4) {
        DtcInfo d;
        d.code = u24(response, i);
        d.status = u8(response, i + 3);
        d.text = dtcDescription(d.code);
        list.append(d);
    }
    return list;
}

QList<DtcInfo> parseObdDtcList(const QByteArray &response)
{
    QList<DtcInfo> list;
    if (response.size() < 2)
        return list;
    const quint8 sid = u8(response, 0);
    if (sid != 0x43 && sid != 0x47)
        return list;
    const int count = u8(response, 1);
    for (qsizetype i = 2; i + 1 < response.size() && list.size() < count; i += 2) {
        DtcInfo d;
        d.code = ((quint32{u8(response, i)} << 8) | u8(response, i + 1)) << 8;
        d.status = sid == 0x43 ? DtcStatus::Confirmed : DtcStatus::Pending;
        d.text = dtcDescription(d.code);
        list.append(d);
    }
    return list;
}

QList<DecodedValue> parseSnapshot(const QByteArray &response)
{
    QList<DecodedValue> values;
    // 59 04 DTC(3) status [record count (DID(2) data)*]
    if (response.size() < 8 || u8(response, 0) != 0x59 || u8(response, 1) != 0x04)
        return values;
    const int count = u8(response, 7);
    qsizetype pos = 8;
    for (int n = 0; n < count && pos + 2 <= response.size(); ++n) {
        const quint16 did = static_cast<quint16>((u8(response, pos) << 8) | u8(response, pos + 1));
        pos += 2;
        const DidInfo *info = didInfo(did);
        if (!info)
            break; // unknown length: cannot continue parsing
        values.append(decodeDid(did, response.mid(pos, info->length)));
        pos += info->length;
    }
    return values;
}

QList<DecodedValue> parseDataResponse(const QByteArray &response)
{
    QList<DecodedValue> values;
    if (response.isEmpty())
        return values;
    const quint8 sid = u8(response, 0);
    qsizetype pos = 1;
    if (sid == 0x62) {
        while (pos + 2 <= response.size()) {
            const quint16 did = static_cast<quint16>((u8(response, pos) << 8) | u8(response, pos + 1));
            pos += 2;
            const DidInfo *info = didInfo(did);
            const qsizetype len = info ? info->length : response.size() - pos;
            values.append(decodeDid(did, response.mid(pos, len)));
            pos += len;
        }
    } else if (sid == 0x41) {
        while (pos < response.size()) {
            const quint16 did = static_cast<quint16>(0xF400 | u8(response, pos));
            pos += 1;
            const DidInfo *info = didInfo(did);
            const qsizetype len = info ? info->length : response.size() - pos;
            values.append(decodeDid(did, response.mid(pos, len)));
            pos += len;
        }
    }
    return values;
}

QString explainResponse(const QByteArray &request, const QByteArray &response)
{
    if (response.isEmpty())
        return QStringLiteral("no response");
    const quint8 sid = u8(response, 0);
    if (sid == 0x7F && response.size() >= 3) {
        return QStringLiteral("negative response to %1: %2 (0x%3)")
            .arg(serviceName(u8(response, 1)), nrcName(u8(response, 2)))
            .arg(u8(response, 2), 2, 16, QLatin1Char('0'));
    }
    const quint8 reqSid = static_cast<quint8>(sid - 0x40);
    QString text = QStringLiteral("positive response to %1").arg(serviceName(reqSid));

    switch (reqSid) {
    case Sid::DiagnosticSessionControl:
        if (response.size() >= 2)
            text += u8(response, 1) == 0x03 ? QStringLiteral(": extended session active")
                                            : QStringLiteral(": default session active");
        break;
    case Sid::ReadDtcInformation:
        if (response.size() >= 2 && (u8(response, 1) == 0x02 || u8(response, 1) == 0x0A))
            text += QStringLiteral(": %1 DTC(s)").arg(parseUdsDtcList(response).size());
        else if (response.size() >= 6 && u8(response, 1) == 0x01)
            text += QStringLiteral(": count = %1").arg((u8(response, 4) << 8) | u8(response, 5));
        else if (response.size() >= 6 && (u8(response, 1) == 0x04 || u8(response, 1) == 0x06))
            text += QStringLiteral(": %1").arg(dtcToString(u24(response, 2)));
        break;
    case Sid::ObdStoredDtcs:
    case Sid::ObdPendingDtcs:
        text += QStringLiteral(": %1 DTC(s)").arg(parseObdDtcList(response).size());
        break;
    case Sid::SecurityAccess:
        if (response.size() >= 2)
            text += u8(response, 1) == 0x01 ? QStringLiteral(": seed received") : QStringLiteral(": ECU unlocked");
        break;
    case Sid::ReadDataByIdentifier:
    case Sid::ObdCurrentData: {
        QStringList parts;
        for (const DecodedValue &v : parseDataResponse(response))
            parts << QStringLiteral("%1 = %2%3").arg(v.name, v.value, v.unit.isEmpty() ? QString() : QLatin1Char(' ') + v.unit);
        if (!parts.isEmpty())
            text += QStringLiteral(": ") + parts.join(QStringLiteral("; "));
        break;
    }
    default:
        break;
    }
    Q_UNUSED(request);
    return text;
}

} // namespace ecusim
