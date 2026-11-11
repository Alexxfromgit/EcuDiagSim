#pragma once

#include "tester/DiagnosticTester.h"

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class AppController;

/// The scan tool as the UI sees it: guided OBD-II and UDS functions on top of the
/// DiagnosticTester, a decoded DTC table, a value table and a raw communication log.
class ScannerViewModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App")
    Q_PROPERTY(QStringList ecuNames READ ecuNames CONSTANT)
    Q_PROPERTY(int target READ target WRITE setTarget NOTIFY targetChanged)
    Q_PROPERTY(QVariantList log READ log NOTIFY logChanged)
    Q_PROPERTY(QVariantList dtcs READ dtcs NOTIFY dtcsChanged)
    Q_PROPERTY(QString dtcTitle READ dtcTitle NOTIFY dtcsChanged)
    Q_PROPERTY(QVariantList values READ values NOTIFY valuesChanged)
    Q_PROPERTY(QString valuesTitle READ valuesTitle NOTIFY valuesChanged)
    Q_PROPERTY(QVariantList ioControls READ ioControls NOTIFY targetChanged)
    Q_PROPERTY(QString sessionText READ sessionText NOTIFY stateChanged)
    Q_PROPERTY(bool unlocked READ unlocked NOTIFY stateChanged)
    Q_PROPERTY(bool keepAlive READ keepAlive WRITE setKeepAlive NOTIFY stateChanged)
    Q_PROPERTY(int pollMode READ pollMode WRITE setPollMode NOTIFY pollModeChanged)

public:
    explicit ScannerViewModel(AppController *app);

    QStringList ecuNames() const;
    int target() const { return m_target; }
    void setTarget(int target);
    QVariantList log() const { return m_log; }
    QVariantList dtcs() const { return m_dtcs; }
    QString dtcTitle() const { return m_dtcTitle; }
    QVariantList values() const { return m_values; }
    QString valuesTitle() const { return m_valuesTitle; }
    QVariantList ioControls() const;
    QString sessionText() const;
    bool unlocked() const { return m_unlocked[m_target]; }
    bool keepAlive() const;
    void setKeepAlive(bool on);
    /// 0 = off, 1 = OBD live data, 2 = UDS live data of the selected ECU
    int pollMode() const { return m_pollMode; }
    void setPollMode(int mode);

    // --- OBD-II (functional addressing, answered by ECM and TCM) ---
    Q_INVOKABLE void obdLiveData();
    Q_INVOKABLE void obdReadCodes();
    Q_INVOKABLE void obdClearCodes();
    Q_INVOKABLE void obdFreezeFrame();
    Q_INVOKABLE void obdVehicleInfo();

    // --- UDS (physical addressing to the selected ECU) ---
    Q_INVOKABLE void udsSession(int type);
    Q_INVOKABLE void udsUnlock();
    Q_INVOKABLE void udsReset();
    Q_INVOKABLE void udsReadDtcs(int statusMask);
    Q_INVOKABLE void udsSupportedDtcs();
    Q_INVOKABLE void udsClearDtcs();
    Q_INVOKABLE void udsSnapshot(const QString &dtcText);
    Q_INVOKABLE void udsIdentification();
    Q_INVOKABLE void udsLiveData();
    Q_INVOKABLE void udsIoControl(int did, int state); ///< state: 1 on, 0 off, -1 return control
    Q_INVOKABLE void sendRaw(const QString &hexText, bool functional);
    Q_INVOKABLE void clearLog();

    /// Hooked up to DiagnosticTester::onResult; also called by poll().
    void onResult(const ecusim::DiagResult &result);
    void poll();
    void simulationReset();

signals:
    void targetChanged();
    void logChanged();
    void dtcsChanged();
    void valuesChanged();
    void stateChanged();
    void pollModeChanged();

private:
    quint32 targetId() const { return 0x7E0 + static_cast<quint32>(m_target); }
    ecusim::DiagnosticTester &tester() const;
    void send(quint32 id, const QByteArray &request, ecusim::DiagnosticTester::Callback callback = {});
    void addLog(const QString &kind, const QString &text);
    void setDtcs(const QString &title, const QVariantList &list);
    void setValues(const QString &title, const QVariantList &list);
    static QVariantMap valueRow(const QString &ecu, const QString &name, const QString &value, const QString &unit);

    AppController *m_app;
    int m_target = 0;
    int m_pollMode = 0;
    int m_quietJobs = 0; ///< polling requests still in the queue; they are not logged
    QVariantList m_log;
    QVariantList m_dtcs;
    QVariantList m_values;
    QVariantList m_collect;
    QString m_dtcTitle;
    QString m_valuesTitle;
    int m_session[4] = {1, 1, 1, 1};
    bool m_unlocked[4] = {false, false, false, false};
};
