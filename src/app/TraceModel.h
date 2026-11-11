#pragma once

#include "can/VirtualCanBus.h"

#include <QAbstractListModel>
#include <QHash>
#include <QtQml/qqmlregistration.h>

#include <vector>

namespace ecusim {
class DbcDatabase;
}

/// List model behind the CAN trace view.
///
/// Frames arrive at simulation speed (hundreds per second); they are collected and
/// handed to the view in batches by flush(), which the application calls at ~30 Hz.
/// Two display modes, as in every CAN analyser: chronological (scrolling log) and
/// fixed (one row per identifier, updated in place, with the measured cycle time).
class TraceModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App")
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    Q_PROPERTY(bool fixedMode READ fixedMode WRITE setFixedMode NOTIFY fixedModeChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles { TimeRole = Qt::UserRole + 1, IdRole, DlcRole, DataRole, NameRole, DecodedRole, SenderRole,
        KindRole, CycleRole, CountRole };
    static constexpr int kMaxRows = 3000;

    explicit TraceModel(QObject *parent = nullptr);

    void setDatabase(const ecusim::DbcDatabase *dbc) { m_dbc = dbc; }
    /// Called from the bus listener for every frame.
    void append(const ecusim::BusFrame &frame);
    /// Moves collected frames into the model.
    void flush();

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool paused() const { return m_paused; }
    void setPaused(bool paused);
    bool fixedMode() const { return m_fixed; }
    void setFixedMode(bool fixed);
    QString filter() const { return m_filter; }
    void setFilter(const QString &filter);
    int count() const { return static_cast<int>(m_rows.size()); }

    Q_INVOKABLE void clear();

    static QString describeIsoTp(const QByteArray &payload);

signals:
    void pausedChanged();
    void fixedModeChanged();
    void filterChanged();
    void countChanged();

private:
    struct Row
    {
        ecusim::BusFrame frame;
        qint64 cycleUs = 0;
        quint64 count = 1;
    };

    bool accepts(const ecusim::BusFrame &frame) const;

    const ecusim::DbcDatabase *m_dbc = nullptr;
    std::vector<Row> m_rows;
    std::vector<ecusim::BusFrame> m_pending;
    QHash<quint32, int> m_rowOfId; ///< fixed mode only
    bool m_paused = false;
    bool m_fixed = false;
    QString m_filter;
};
