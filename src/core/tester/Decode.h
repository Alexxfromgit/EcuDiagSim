#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

namespace ecusim {

/// The scan tool's knowledge about data identifiers ("ODX light").
struct DidInfo
{
    enum Kind { U8, U16, Ascii, Raw };
    quint16 did;
    const char *name;
    Kind kind;
    int length;
    double factor;
    double offset;
    const char *unit;
};

const QList<DidInfo> &didCatalog();
const DidInfo *didInfo(quint16 did);

struct DecodedValue
{
    quint16 did = 0;
    QString name;
    QString value;
    QString unit;
    double number = 0.0; ///< numeric value where applicable
};

/// Decodes the data of one DID. OBD PIDs are DIDs 0xF400 + PID.
DecodedValue decodeDid(quint16 did, const QByteArray &data);

struct DtcInfo
{
    quint32 code = 0;
    quint8 status = 0;
    QString text;
};

/// Parses a positive ReadDTCInformation response (59 02 / 59 0A).
QList<DtcInfo> parseUdsDtcList(const QByteArray &response);
/// Parses an OBD Mode 03 / 07 response (43 / 47). Codes are returned as 3-byte DTCs with FTB 0.
QList<DtcInfo> parseObdDtcList(const QByteArray &response);
/// Parses the records of a 59 04 response into decoded values.
QList<DecodedValue> parseSnapshot(const QByteArray &response);
/// Parses a 62 response (one or more DIDs) or a 41 response (one or more PIDs).
QList<DecodedValue> parseDataResponse(const QByteArray &response);

/// One line of plain language for a request/response pair, e.g.
/// "negative response: securityAccessDenied (0x33)".
QString explainResponse(const QByteArray &request, const QByteArray &response);

} // namespace ecusim
