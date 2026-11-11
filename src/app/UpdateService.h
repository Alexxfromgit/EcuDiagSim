#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <memory>

/// Checks GitHub Releases for a newer version and stages it with Velopack.
///
/// Every package knows its own channel (win-x64, linux-x64, osx-arm64, ...), so it only ever sees
/// builds for its platform. Only copies that Velopack installed (installer or portable zip) can
/// update; a development build reports canUpdate == false.
///
/// For testing, ECUSIM_UPDATE_FEED names a local folder of `vpk pack` output (or an HTTP feed)
/// instead of GitHub, and ECUSIM_GITHUB_TOKEN lets a tester read a private fork's releases.
class UpdateService : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App")
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(bool canUpdate READ canUpdate CONSTANT)
    Q_PROPERTY(bool autoCheck READ autoCheck WRITE setAutoCheck NOTIFY autoCheckChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(int progress READ progress NOTIFY stateChanged)
    Q_PROPERTY(QString readyVersion READ readyVersion NOTIFY stateChanged)
    Q_PROPERTY(bool updateReady READ updateReady NOTIFY stateChanged)
    Q_PROPERTY(QString readyText READ readyText NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(QString releaseNotesUrl READ releaseNotesUrl NOTIFY stateChanged)

public:
    static constexpr const char *kRepositoryUrl = "https://github.com/Alexxfromgit/EcuDiagSim";
    static constexpr int kStartupDelayMs = 10000;

    explicit UpdateService(QObject *parent = nullptr);
    ~UpdateService() override;

    QString currentVersion() const;
    bool canUpdate() const { return m_canUpdate; }
    bool autoCheck() const;
    void setAutoCheck(bool on);
    bool busy() const { return m_busy; }
    int progress() const;
    QString readyVersion() const { return m_readyVersion; }
    bool updateReady() const { return !m_readyVersion.isEmpty() && !m_dismissed; }
    QString readyText() const;
    QString statusText() const { return m_statusText; }
    /// Release notes of the staged update, or of the running version.
    QString releaseNotesUrl() const;

    /// Background check a little after start, unless turned off or not possible.
    void checkOnStartup();

    Q_INVOKABLE void check();           ///< "Check for updates": reports the outcome in statusText
    Q_INVOKABLE void restartToUpdate(); ///< quits and lets Velopack install the staged update
    Q_INVOKABLE void dismiss();
    static QString releaseNotesUrl(const QString &version);

signals:
    void autoCheckChanged();
    void stateChanged();

private:
    struct Shared;
    void start(bool userInitiated);
    void finished(bool userInitiated, const QString &version, const QString &error);
    void setStatus(const QString &text);

    std::shared_ptr<Shared> m_shared; ///< Velopack state, shared with the worker thread
    QTimer m_progressTimer;
    bool m_canUpdate = false;
    bool m_busy = false;
    bool m_dismissed = false;
    QString m_readyVersion;
    QString m_statusText;
};
