#pragma once

#include <QList>
#include <QString>

namespace ecusim {

/// Compile-time version of makeDtc(): sae4 holds the four digits after the system letter
/// as hex, e.g. saeDtc('C', 0x0035, 0x13) for C0035-13.
constexpr quint32 saeDtc(char system, quint32 sae4, quint8 ftb = 0x00)
{
    const quint32 sys = system == 'C' ? 1u : system == 'B' ? 2u : system == 'U' ? 3u : 0u;
    return ((((sys << 14) | (sae4 & 0x3FFF)) << 8) | ftb);
}

/// All DTCs known to the virtual vehicle. Powertrain codes follow SAE J2012; the body
/// codes (B1318, B1601) are manufacturer specific examples.
namespace dtc {
// ECM
constexpr quint32 P0116 = saeDtc('P', 0x0116);
constexpr quint32 P0117 = saeDtc('P', 0x0117);
constexpr quint32 P0118 = saeDtc('P', 0x0118);
constexpr quint32 P0121 = saeDtc('P', 0x0121);
constexpr quint32 P0122 = saeDtc('P', 0x0122);
constexpr quint32 P0123 = saeDtc('P', 0x0123);
constexpr quint32 P0217 = saeDtc('P', 0x0217);
constexpr quint32 P0335 = saeDtc('P', 0x0335);
constexpr quint32 P0562 = saeDtc('P', 0x0562);
constexpr quint32 P0700 = saeDtc('P', 0x0700);
// TCM
constexpr quint32 P0218 = saeDtc('P', 0x0218);
constexpr quint32 P0500 = saeDtc('P', 0x0500);
constexpr quint32 P0711 = saeDtc('P', 0x0711);
constexpr quint32 P0712 = saeDtc('P', 0x0712);
constexpr quint32 P0713 = saeDtc('P', 0x0713);
// ABS: wheel speed sensors FL, FR, RL, RR with a failure type byte
constexpr quint32 kWheelBase[4] = {0x0035, 0x0040, 0x0045, 0x0050};
constexpr quint8 FtbShortGround = 0x11;
constexpr quint8 FtbShortBattery = 0x12;
constexpr quint8 FtbOpen = 0x13;
constexpr quint8 FtbSignalInvalid = 0x29;
constexpr quint32 wheel(int index, quint8 ftb) { return saeDtc('C', kWheelBase[index], ftb); }
// BCM
constexpr quint32 B1318 = saeDtc('B', 0x1318, 0x16);
constexpr quint32 B1601 = saeDtc('B', 0x1601, 0x13);
// Network
constexpr quint32 U0100 = saeDtc('U', 0x0100, 0x87);
constexpr quint32 U0101 = saeDtc('U', 0x0101, 0x87);
constexpr quint32 U0121 = saeDtc('U', 0x0121, 0x87);
constexpr quint32 U0140 = saeDtc('U', 0x0140, 0x87);
constexpr quint32 U0415Counter = saeDtc('U', 0x0415, 0x82);
constexpr quint32 U0415Checksum = saeDtc('U', 0x0415, 0x83);
} // namespace dtc

struct DtcCatalogEntry
{
    quint32 code;
    const char *ecus;
    const char *text;
    const char *detection;
};

/// The scan tool's "DTC database": text for every code, used by ECUs and the tester alike.
const QList<DtcCatalogEntry> &dtcCatalog();
/// Description for a 3-byte DTC; falls back to matching without the failure type byte.
QString dtcDescription(quint32 code);

} // namespace ecusim
