#include "diag/DtcCatalog.h"

namespace ecusim {

const QList<DtcCatalogEntry> &dtcCatalog()
{
    using namespace dtc;
    static const QList<DtcCatalogEntry> catalog = {
        {P0116, "ECM", "Engine coolant temperature sensor range/performance",
            "Signal jumps more than 10 degC within 100 ms, or does not move during warm-up. Two trips."},
        {P0117, "ECM", "Engine coolant temperature sensor circuit low",
            "Sensor voltage below 0.2 V (short to ground, reads very hot) for 0.5 s."},
        {P0118, "ECM", "Engine coolant temperature sensor circuit high",
            "Sensor voltage above 4.8 V (open circuit or short to battery, reads -40 degC) for 0.5 s."},
        {P0121, "ECM", "Throttle position sensor range/performance",
            "Measured throttle angle deviates more than 20 % from the commanded angle for 0.5 s. Two trips."},
        {P0122, "ECM", "Throttle position sensor circuit low",
            "Sensor voltage below 0.2 V (open circuit or short to ground) for 0.2 s."},
        {P0123, "ECM", "Throttle position sensor circuit high",
            "Sensor voltage above 4.8 V (short to battery) for 0.2 s."},
        {P0217, "ECM", "Engine over temperature", "Coolant temperature above 118 degC for 3 s."},
        {P0335, "ECM", "Crankshaft position sensor circuit",
            "No engine speed signal while cranking or while the engine was running."},
        {P0562, "ECM", "System voltage low", "Supply voltage below 11.0 V with the engine running for 3 s."},
        {P0700, "ECM", "Transmission control system (MIL request)",
            "The TCM requests the malfunction indicator lamp over CAN."},
        {P0218, "TCM", "Transmission fluid over temperature", "Fluid temperature above 135 degC for 3 s."},
        {P0500, "TCM", "Vehicle speed signal", "Vehicle speed from the ABS is missing or flagged invalid for 0.5 s."},
        {P0711, "TCM", "Transmission fluid temperature sensor range/performance",
            "Signal jumps more than 10 degC within 100 ms. Two trips."},
        {P0712, "TCM", "Transmission fluid temperature sensor circuit low", "Sensor voltage below 0.2 V for 0.5 s."},
        {P0713, "TCM", "Transmission fluid temperature sensor circuit high", "Sensor voltage above 4.8 V for 0.5 s."},
    };
    static const QList<DtcCatalogEntry> full = [] {
        QList<DtcCatalogEntry> list = catalog;
        static const char *kWheelText[4] = {"Left front wheel speed sensor", "Right front wheel speed sensor",
            "Left rear wheel speed sensor", "Right rear wheel speed sensor"};
        for (int i = 0; i < 4; ++i) {
            list.append({wheel(i, FtbShortGround), "ABS", kWheelText[i], "Sensor line shorted to ground for 0.2 s."});
            list.append({wheel(i, FtbShortBattery), "ABS", kWheelText[i], "Sensor line shorted to battery for 0.2 s."});
            list.append({wheel(i, FtbOpen), "ABS", kWheelText[i], "Sensor circuit open for 0.2 s."});
            list.append({wheel(i, FtbSignalInvalid), "ABS", kWheelText[i],
                "Speed deviates more than 25 % from the median of the other wheels above 10 km/h for 0.5 s."});
        }
        list.append({B1318, "BCM", "Battery voltage low", "Supply voltage below 11.0 V with ignition on for 3 s."});
        list.append({B1601, "BCM", "Left low beam circuit",
            "No lamp current although the low beam output is switched on (bulb or wiring open)."});
        list.append({U0100, "TCM, ABS, BCM", "Lost communication with ECM", "ECM messages missing on the bus."});
        list.append({U0101, "ECM, BCM", "Lost communication with TCM", "TCM messages missing on the bus."});
        list.append({U0121, "ECM, TCM, BCM", "Lost communication with ABS control module",
            "ABS messages missing on the bus."});
        list.append({U0140, "ECM, ABS", "Lost communication with body control module",
            "BCM messages missing on the bus."});
        list.append({U0415Counter, "ECM", "Invalid data received from ABS control module",
            "Alive counter of ABS_WheelSpeeds does not change for 10 frames."});
        list.append({U0415Checksum, "ECM", "Invalid data received from ABS control module",
            "Checksum of ABS_WheelSpeeds wrong for 10 frames."});
        return list;
    }();
    return full;
}

QString dtcDescription(quint32 code)
{
    for (const DtcCatalogEntry &e : dtcCatalog())
        if (e.code == code)
            return QString::fromLatin1(e.text);
    for (const DtcCatalogEntry &e : dtcCatalog())
        if ((e.code >> 8) == (code >> 8))
            return QString::fromLatin1(e.text);
    return {};
}

} // namespace ecusim
