#include "UpdateService.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QPointer>
#include <QSettings>

#include <atomic>
#include <mutex>
#include <thread>

#ifdef ECUSIM_HAS_VELOPACK
#include "Velopack.hpp"
#endif

#ifndef ECUSIM_VERSION
#define ECUSIM_VERSION "0.0.0"
#endif

namespace {
const char *kAutoCheckKey = "updates/autoCheck";

void log(const QString &message)
{
    qInfo().noquote() << "[updates]" << message;
}
} // namespace

/// Everything the worker thread touches. It outlives the UpdateService if the window closes
/// while a download is still running; `owner` is cleared then, so no result is delivered.
struct UpdateService::Shared
{
    std::mutex mutex;
    QPointer<UpdateService> owner;
    std::atomic<int> progress{0};
#ifdef ECUSIM_HAS_VELOPACK
    std::unique_ptr<Velopack::UpdateManager> manager;
#endif
};

UpdateService::UpdateService(QObject *parent)
    : QObject(parent)
    , m_shared(std::make_shared<Shared>())
{
    m_shared->owner = this;
    m_progressTimer.setInterval(250);
    connect(&m_progressTimer, &QTimer::timeout, this, &UpdateService::stateChanged);

#ifdef ECUSIM_HAS_VELOPACK
    try {
        const QString feed = qEnvironmentVariable("ECUSIM_UPDATE_FEED");
        const std::string token = qEnvironmentVariable("ECUSIM_GITHUB_TOKEN").toStdString();
        if (!feed.isEmpty() && QDir(feed).exists()) {
            log(QStringLiteral("Using the local update feed %1").arg(feed));
            m_shared->manager = std::make_unique<Velopack::UpdateManager>(
                std::make_unique<Velopack::FileSource>(QDir::toNativeSeparators(feed).toStdString()));
        } else if (!feed.isEmpty()) {
            log(QStringLiteral("Using the update feed %1").arg(feed));
            m_shared->manager = std::make_unique<Velopack::UpdateManager>(
                std::make_unique<Velopack::HttpSource>(feed.toStdString()));
        } else {
            m_shared->manager = std::make_unique<Velopack::UpdateManager>(
                std::make_unique<Velopack::GithubSource>(kRepositoryUrl, token, false));
        }
        m_canUpdate = true;
        // An update downloaded in an earlier session that has not been installed yet.
        if (auto pending = m_shared->manager->UpdatePendingRestart())
            m_readyVersion = QString::fromStdString(pending->Version);
    } catch (const std::exception &e) {
        // Not installed by Velopack (a development build): nothing to update.
        log(QStringLiteral("Updates unavailable: %1").arg(QString::fromUtf8(e.what())));
        m_shared->manager.reset();
    }
#endif
}

UpdateService::~UpdateService()
{
    std::lock_guard lock(m_shared->mutex);
    m_shared->owner = nullptr;
}

QString UpdateService::currentVersion() const
{
    return QStringLiteral(ECUSIM_VERSION);
}

bool UpdateService::autoCheck() const
{
    return m_canUpdate && QSettings().value(QLatin1String(kAutoCheckKey), true).toBool();
}

void UpdateService::setAutoCheck(bool on)
{
    if (!m_canUpdate || autoCheck() == on)
        return;
    QSettings().setValue(QLatin1String(kAutoCheckKey), on);
    emit autoCheckChanged();
}

int UpdateService::progress() const
{
    return m_shared->progress.load();
}

QString UpdateService::readyText() const
{
    return m_readyVersion.isEmpty()
        ? QString()
        : QStringLiteral("ECU Diagnostics Simulator %1 is ready. Restart to update, or it installs the next time "
                         "you start the app.")
              .arg(m_readyVersion);
}

QString UpdateService::releaseNotesUrl(const QString &version)
{
    return QStringLiteral("%1/releases/tag/v%2").arg(QLatin1String(kRepositoryUrl), version);
}

void UpdateService::setStatus(const QString &text)
{
    m_statusText = text;
    emit stateChanged();
}

void UpdateService::checkOnStartup()
{
    if (!m_canUpdate || !autoCheck() || !m_readyVersion.isEmpty())
        return;
    QTimer::singleShot(kStartupDelayMs, this, [this] {
        if (!m_busy && m_readyVersion.isEmpty())
            start(false);
    });
}

void UpdateService::check()
{
    if (!m_canUpdate) {
        setStatus(QStringLiteral("Updates are only available in the installed app (installer or portable "
                                 "download from GitHub Releases)."));
        return;
    }
    if (m_busy)
        return;
    setStatus(QStringLiteral("Checking for updates…"));
    start(true);
}

void UpdateService::start(bool userInitiated)
{
#ifdef ECUSIM_HAS_VELOPACK
    m_busy = true;
    m_shared->progress = 0;
    m_progressTimer.start();
    emit stateChanged();

    // Velopack's calls block on the network, so they run on their own thread. The thread is
    // detached: if the app quits during a download, the process simply ends.
    std::thread([shared = m_shared, userInitiated] {
        QString version;
        QString error;
        try {
            if (auto update = shared->manager->CheckForUpdates()) {
                shared->manager->DownloadUpdates(
                    *update,
                    [](void *data, size_t percent) {
                        static_cast<Shared *>(data)->progress = static_cast<int>(percent);
                    },
                    shared.get());
                version = QString::fromStdString(update->TargetFullRelease.Version);
            }
        } catch (const std::exception &e) {
            error = QString::fromUtf8(e.what());
        }
        std::lock_guard lock(shared->mutex);
        if (UpdateService *owner = shared->owner) {
            QMetaObject::invokeMethod(
                owner, [owner, userInitiated, version, error] { owner->finished(userInitiated, version, error); },
                Qt::QueuedConnection);
        }
    }).detach();
#else
    Q_UNUSED(userInitiated);
#endif
}

void UpdateService::finished(bool userInitiated, const QString &version, const QString &error)
{
    m_busy = false;
    m_progressTimer.stop();
    if (!error.isEmpty()) {
        log(QStringLiteral("Update check failed: %1").arg(error));
        // Offline, rate-limited or no feed yet: an automatic check stays quiet.
        m_statusText = userInitiated
            ? QStringLiteral("Couldn't check for updates. Check your internet connection and try again.")
            : QString();
    } else if (!version.isEmpty()) {
        log(QStringLiteral("Version %1 downloaded").arg(version));
        m_readyVersion = version;
        m_dismissed = false;
        m_statusText.clear();
    } else {
        m_statusText = userInitiated && m_readyVersion.isEmpty()
            ? QStringLiteral("ECU Diagnostics Simulator %1 is the latest version.").arg(currentVersion())
            : QString();
    }
    emit stateChanged();
}

void UpdateService::restartToUpdate()
{
#ifdef ECUSIM_HAS_VELOPACK
    if (!m_shared->manager || m_busy)
        return;
    try {
        if (auto pending = m_shared->manager->UpdatePendingRestart()) {
            // Starts Velopack's updater, which waits for this process to exit, installs the
            // update and starts the new version.
            m_shared->manager->WaitExitThenApplyUpdates(*pending, false, true);
            QCoreApplication::quit();
        }
    } catch (const std::exception &e) {
        log(QStringLiteral("Could not apply the update: %1").arg(QString::fromUtf8(e.what())));
        setStatus(QStringLiteral("The update could not be installed. It will be tried again at the next start."));
    }
#endif
}

void UpdateService::dismiss()
{
    m_dismissed = true;
    m_statusText.clear();
    emit stateChanged();
}

QString UpdateService::releaseNotesUrl() const
{
    return releaseNotesUrl(m_readyVersion.isEmpty() ? currentVersion() : m_readyVersion);
}
