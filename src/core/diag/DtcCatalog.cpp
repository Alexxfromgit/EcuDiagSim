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
        {P0101, "ECM", "Mass air flow sensor range/performance",
            "Measured air flow deviates more than 30 % from the speed-density model for 5 s, engine warm. Two trips."},
        {P0102, "ECM", "Mass air flow sensor circuit low", "Sensor voltage below 0.2 V for 1 s."},
        {P0103, "ECM", "Mass air flow sensor circuit high", "Sensor voltage above 4.8 V for 1 s."},
        {P0131, "ECM", "O2 sensor circuit low voltage (bank 1 sensor 1)",
            "Below 0.04 V for 5 s with the engine warm and running (a working cell stays above 0.08 V)."},
        {P0132, "ECM", "O2 sensor circuit high voltage (bank 1 sensor 1)", "Above 1.2 V for 2 s."},
        {P0134, "ECM", "O2 sensor circuit no activity detected (bank 1 sensor 1)",
            "The signal stays between 0.3 and 0.6 V for a 10 s window in which it should switch."},
        {P0171, "ECM", "System too lean (bank 1)",
            "Short + long term fuel trim above +23 % for 5 s in closed loop. Two trips."},
        {P0172, "ECM", "System too rich (bank 1)",
            "Short + long term fuel trim below -23 % for 5 s in closed loop. Two trips."},
        {P0300, "ECM", "Random/multiple cylinder misfire detected",
            "Misfires on more than one cylinder in a 200-revolution window."},
        {misfireDtc(1), "ECM", "Cylinder 1 misfire detected", "More than 5 % misfires of the cylinder in 200 revolutions."},
        {misfireDtc(2), "ECM", "Cylinder 2 misfire detected", "More than 5 % misfires of the cylinder in 200 revolutions."},
        {misfireDtc(3), "ECM", "Cylinder 3 misfire detected", "More than 5 % misfires of the cylinder in 200 revolutions."},
        {misfireDtc(4), "ECM", "Cylinder 4 misfire detected", "More than 5 % misfires of the cylinder in 200 revolutions."},
        {P0462, "ECM", "Fuel level sensor circuit low",
            "Sender voltage below 0.2 V (short to ground, reads full) for 2 s."},
        {P0463, "ECM", "Fuel level sensor circuit high",
            "Sender voltage above 4.8 V (open circuit, reads empty) for 2 s."},
        {P0522, "ECM", "Engine oil pressure sensor circuit low",
            "Sensor voltage below 0.2 V (open circuit or short to ground) for 2 s."},
        {P0523, "ECM", "Engine oil pressure sensor circuit high", "Sensor voltage above 4.8 V for 2 s."},
        {P0524, "ECM", "Engine oil pressure too low",
            "Below 0.4 bar with the engine running for more than 3 s, for 2 s."},
        {P2176, "ECM", "Throttle actuator control system - idle position not learned",
            "A new throttle body has not been adapted yet. Run routine 0x0201 (throttle adaptation)."},
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
        list.append({C0460Low, "ABS", "Steering angle sensor circuit low", "Sensor voltage below 0.2 V for 0.2 s."});
        list.append({C0460Calibration, "ABS", "Steering angle sensor missing calibration",
            "No zero point learned (new module or sensor). Run routine 0x0401 with the wheels straight."});
        list.append({C0460Plausibility, "ABS", "Steering angle sensor signal plausibility failure",
            "Above 30 km/h with no yaw for 3 s the steering angle reads more than 8 deg (e.g. after a wheel alignment)."});
        list.append({C0196Low, "ABS", "Yaw rate sensor circuit low", "Sensor voltage below 0.2 V for 0.2 s."});
        list.append({C0196High, "ABS", "Yaw rate sensor circuit high", "Sensor voltage above 4.8 V for 0.2 s."});
        for (int i = 0; i < 2; ++i) {
            const char *text = i == 0 ? "Driver frontal stage 1 deployment control" : "Passenger frontal stage 1 deployment control";
            list.append({squib(i, FtbResistanceLow), "SRS", text, "Squib circuit below 1.0 ohm for 2 s (short between the leads)."});
            list.append({squib(i, FtbResistanceHigh), "SRS", text,
                "Squib circuit above 4.0 ohm for 2 s (open: connector, clock spring, or a fired airbag)."});
        }
        list.append({B0050BeltSwitch, "SRS", "Driver seat belt buckle switch circuit open",
            "Neither the buckled nor the unbuckled resistance: the circuit is open, for 2 s."});
        list.append({B0052Deployment, "SRS", "Deployment commanded",
            "The airbags were fired. Stored with the crash record; cannot be cleared - the module must be replaced."});
        static const char *kTpmsText[4] = {"Left front tire pressure sensor", "Right front tire pressure sensor",
            "Left rear tire pressure sensor", "Right rear tire pressure sensor"};
        for (int i = 0; i < 4; ++i)
            list.append({tpmsSensor(i), "BCM", kTpmsText[i],
                "No radio message with the learned sensor ID for 10 s with the ignition on (flat battery, "
                "or a new sensor whose ID has not been learned)."});
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
        list.append({U3000NotConfigured, "ABS, BCM, SRS", "Control module not configured",
            "Variant coding (DID 0x0100) is blank, e.g. after the module was replaced. Write it with 0x2E."});
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
