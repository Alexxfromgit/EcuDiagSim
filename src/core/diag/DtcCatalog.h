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
// ECM, engine OBD pack: air flow, oxygen sensor, mixture, misfire
constexpr quint32 P0101 = saeDtc('P', 0x0101);
constexpr quint32 P0102 = saeDtc('P', 0x0102);
constexpr quint32 P0103 = saeDtc('P', 0x0103);
constexpr quint32 P0131 = saeDtc('P', 0x0131);
constexpr quint32 P0132 = saeDtc('P', 0x0132);
constexpr quint32 P0134 = saeDtc('P', 0x0134);
constexpr quint32 P0171 = saeDtc('P', 0x0171);
constexpr quint32 P0172 = saeDtc('P', 0x0172);
constexpr quint32 P0300 = saeDtc('P', 0x0300);
constexpr quint32 misfireDtc(int cylinder) { return saeDtc('P', 0x0300 + static_cast<quint32>(cylinder)); }
// ECM: fuel level sender and oil pressure
constexpr quint32 P0462 = saeDtc('P', 0x0462);
constexpr quint32 P0463 = saeDtc('P', 0x0463);
constexpr quint32 P0522 = saeDtc('P', 0x0522);
constexpr quint32 P0523 = saeDtc('P', 0x0523);
constexpr quint32 P0524 = saeDtc('P', 0x0524);
// ECM: throttle adaptation (SAE J2012 P2176)
constexpr quint32 P2176 = saeDtc('P', 0x2176);
// ABS: wheel speed sensors FL, FR, RL, RR with a failure type byte
constexpr quint32 kWheelBase[4] = {0x0035, 0x0040, 0x0045, 0x0050};
constexpr quint8 FtbShortGround = 0x11;
constexpr quint8 FtbShortBattery = 0x12;
constexpr quint8 FtbOpen = 0x13;
constexpr quint8 FtbSignalInvalid = 0x29;
constexpr quint32 wheel(int index, quint8 ftb) { return saeDtc('C', kWheelBase[index], ftb); }
// ABS/ESC: steering angle sensor (C0460) and yaw rate sensor (C0196)
constexpr quint32 C0460Low = saeDtc('C', 0x0460, 0x11);
constexpr quint32 C0460Calibration = saeDtc('C', 0x0460, 0x54);
constexpr quint32 C0460Plausibility = saeDtc('C', 0x0460, 0x64);
constexpr quint32 C0196Low = saeDtc('C', 0x0196, 0x11);
constexpr quint32 C0196High = saeDtc('C', 0x0196, 0x12);
// SRS: driver / passenger frontal squib (SAE J2012 B0001 / B0010) with FTB 1A low / 1B high
// resistance, driver belt buckle switch circuit open, deployment commanded (crash record)
constexpr quint8 FtbResistanceLow = 0x1A;
constexpr quint8 FtbResistanceHigh = 0x1B;
constexpr quint32 squib(int index, quint8 ftb) { return saeDtc('B', index == 0 ? 0x0001 : 0x0010, ftb); }
constexpr quint32 B0050BeltSwitch = saeDtc('B', 0x0050, 0x13);
constexpr quint32 B0052Deployment = saeDtc('B', 0x0052, 0x00);
// BCM, tyre pressure monitoring: sensor FL, FR, RL, RR, FTB 0x87 = missing message
constexpr quint32 kTpmsBase[4] = {0x0750, 0x0755, 0x0760, 0x0765};
constexpr quint32 tpmsSensor(int index) { return saeDtc('C', kTpmsBase[index], 0x87); }
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
// Control module not configured (SAE J2012 U3000 "Control module", FTB 0x55 "not configured")
constexpr quint32 U3000NotConfigured = saeDtc('U', 0x3000, 0x55);
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
