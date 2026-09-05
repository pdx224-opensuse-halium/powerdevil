/*  This file is part of the KDE project
    SPDX-FileCopyrightText: 2006 Kevin Ottens <ervin@kde.org>
    SPDX-FileCopyrightText: 2008-2010 Dario Freddi <drf@kde.org>
    SPDX-FileCopyrightText: 2010 Alejandro Fiestas <alex@eyeos.org>
    SPDX-FileCopyrightText: 2010-2013 Lukáš Tinkl <ltinkl@redhat.com>
    SPDX-FileCopyrightText: 2015 Kai Uwe Broulik <kde@privat.broulik.de>
    SPDX-FileCopyrightText: 2023 Nicolas Fella <nicolas.fella@gmx.de>

    SPDX-License-Identifier: LGPL-2.0-only

*/

#include "suspendcontroller.h"

#include <powerdevil_debug.h>

#include <QDBusConnection>
#include <QDBusConnectionInterface>

#include <QDir>
#include <QFile>

using namespace Qt::StringLiterals;

inline constexpr QLatin1StringView LOGIN1_SERVICE("org.freedesktop.login1");
inline constexpr QLatin1StringView CONSOLEKIT2_SERVICE("org.freedesktop.ConsoleKit");

inline static const QLatin1String s_wakeupSysFsPath("/sys/class/wakeup");

// Qualcomm (msm) kernels name the IRQ that resumed the AP here. On those SoCs
// this is the only working attribution -- /sys/class/wakeup/*/wakeup_count
// never increments. See the pdx224 patch notes.
inline static const QLatin1String s_socWakeupReasonPath("/sys/kernel/wakeup_reasons/last_resume_reason");

inline static const QLatin1String s_powerSupplyPath("/sys/class/power_supply");

// Wakeup sources WITH working counters. Unlike /sys/class/wakeup this does
// increment on this SoC, so it is the fallback of last resort for attribution.
inline static const QLatin1String s_debugWakeupSourcesPath("/sys/kernel/debug/wakeup_sources");

// (0/I) genirq per-IRQ counters. These keep counting across suspend, so a
// delta here is positive evidence rather than an inference.
inline static const QLatin1String s_irqPath("/sys/kernel/irq");

SuspendController::SuspendController()
    : QObject()
{
    // interfaces
    if (!QDBusConnection::systemBus().interface()->isServiceRegistered(LOGIN1_SERVICE)) {
        // Activate it.
        QDBusConnection::systemBus().interface()->startService(LOGIN1_SERVICE);
    }

    if (!QDBusConnection::systemBus().interface()->isServiceRegistered(CONSOLEKIT2_SERVICE)) {
        // Activate it.
        QDBusConnection::systemBus().interface()->startService(CONSOLEKIT2_SERVICE);
    }

    if (QDBusConnection::systemBus().interface()->isServiceRegistered(LOGIN1_SERVICE)) {
        m_login1Interface =
            new QDBusInterface(LOGIN1_SERVICE, u"/org/freedesktop/login1"_s, u"org.freedesktop.login1.Manager"_s, QDBusConnection::systemBus(), this);
    }

    // if login1 isn't available, try using the same interface with ConsoleKit2
    if (!m_login1Interface && QDBusConnection::systemBus().interface()->isServiceRegistered(CONSOLEKIT2_SERVICE)) {
        m_login1Interface = new QDBusInterface(CONSOLEKIT2_SERVICE,
                                               u"/org/freedesktop/ConsoleKit/Manager"_s,
                                               u"org.freedesktop.ConsoleKit.Manager"_s,
                                               QDBusConnection::systemBus(),
                                               this);
    }

    // "resuming" signal
    if (m_login1Interface) {
        connect(m_login1Interface.data(), SIGNAL(PrepareForSleep(bool)), this, SLOT(slotLogin1PrepareForSleep(bool)));
    }

#ifdef Q_OS_LINUX
    m_udevClient = new UdevQt::Client(this);
#endif
}

bool SuspendController::canSuspend() const
{
    return m_sessionManagement.canSuspend();
}

void SuspendController::suspend()
{
    m_sessionManagement.suspend();
}

bool SuspendController::canHibernate() const
{
    return m_sessionManagement.canHibernate();
}

void SuspendController::hibernate()
{
    m_sessionManagement.hibernate();
}

bool SuspendController::canHybridSuspend() const
{
    return m_sessionManagement.canHybridSuspend();
}

void SuspendController::hybridSuspend()
{
    m_sessionManagement.hybridSuspend();
}

bool SuspendController::canSuspendThenHibernate() const
{
    return m_sessionManagement.canSuspendThenHibernate();
}

void SuspendController::suspendThenHibernate()
{
    m_sessionManagement.suspendThenHibernate();
}

void SuspendController::slotLogin1PrepareForSleep(bool active)
{
#ifdef Q_OS_LINUX
    snapshotWakeupCounts(active);
#endif

    if (active) {
        Q_EMIT aboutToSuspend();
    } else {
        Q_EMIT resumeFromSuspend();
    }
}

#ifdef Q_OS_LINUX
void SuspendController::snapshotWakeupCounts(bool active)
{
    // clear out previously recorded values
    if (active) {
        m_wakeupCounts.clear();
    } else {
        m_lastWakeupSources.clear();
    }

    // read all wakeups, if we are waking up, diff against previous values
    const QStringList wakeups = QDir(s_wakeupSysFsPath).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &wakeup : wakeups) {
        QDir wakeupDir(QString(s_wakeupSysFsPath + u'/' + wakeup));

        // read wakeup source name
        QFile file(wakeupDir.absoluteFilePath(u"name"_s));
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        QString name;
        QTextStream stream(&file);
        stream >> name;

        // read wakeup count
        QFile countFile(wakeupDir.absoluteFilePath(u"wakeup_count"_s));
        int count = 0;
        if (countFile.open(QIODevice::ReadOnly)) {
            count = countFile.readAll().trimmed().toInt();
        }

        // if the count after waking up is higher than before waking up,
        // then this wakeup source is responsible for the wakeup
        if (active) {
            m_wakeupCounts.insert(name, count);
        } else {
            if (m_wakeupCounts.value(name, 0) < count) {
                m_lastWakeupSources << wakeupDir.absoluteFilePath(u"device"_s);
            }
        }
    }

    // (B) sample every power_supply "online" so we can tell a genuine charger
    // plug/unplug from battery-level noise that merely ticks the same sources.
    {
        QHash<QString, QString> online;
        const QStringList supplies = QDir(s_powerSupplyPath).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &supply : supplies) {
            QFile f(QString(s_powerSupplyPath + u'/' + supply + u"/online"));
            if (f.open(QIODevice::ReadOnly)) {
                online.insert(supply, QString::fromLatin1(f.readAll()).trimmed());
            }
        }
        if (active) {
            m_powerSupplyOnline = online;
            m_powerSupplyChanged = false;
        } else {
            m_powerSupplyChanged = (online != m_powerSupplyOnline);
            m_powerSupplyOnline = online;
        }
    }

    // (D) sample /sys/kernel/debug/wakeup_sources. Columns are:
    //   name  active_count  event_count  wakeup_count  expire_count  ...
    // Names repeat (several `qrtr_ws` rows), so aggregate by name. This counter
    // DOES move on this SoC, unlike /sys/class/wakeup/*/wakeup_count.
    {
        QHash<QString, qint64> counts;
        QFile f(s_debugWakeupSourcesPath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            f.readLine(); // header
            while (!f.atEnd()) {
                const QString line = QString::fromLatin1(f.readLine());
                // simplified() collapses tab/space runs -- avoids needing QRegularExpression
                const QStringList col = line.simplified().split(u' ', Qt::SkipEmptyParts);
                if (col.size() < 3) {
                    continue;
                }
                bool ok = false;
                const qint64 active = col.at(1).toLongLong(&ok);
                if (ok) {
                    counts[col.at(0)] += active;
                }
            }
        }
        if (active) {
            m_debugWakeupCounts = counts;
            m_debugWakeupDelta.clear();
        } else {
            m_debugWakeupDelta.clear();
            for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
                if (it.value() > m_debugWakeupCounts.value(it.key(), 0)) {
                    m_debugWakeupDelta << it.key();
                }
            }
            m_debugWakeupCounts = counts;
        }
    }

    // (0/I) sample the input IRQs. Resolved by name every time rather than cached:
    // IRQ numbers are assigned at probe and are not stable across boots, so a
    // hardcoded number would silently point at an unrelated device.
    {
        QHash<QString, qint64> irqs;
        const QStringList entries = QDir(s_irqPath).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &irq : entries) {
            QFile actions(QString(s_irqPath + u'/' + irq + u"/actions"));
            if (!actions.open(QIODevice::ReadOnly)) {
                continue;
            }
            const QString name = QString::fromLatin1(actions.readAll()).trimmed().toLower();
            if (name.isEmpty()) {
                continue;
            }
            // Only the handful of IRQs a human can physically trigger. Anything
            // not on this list is background and must not force the screen on.
            if (!(name.contains(u"pwrkey"_s) || name.contains(u"powerkey"_s) || name.contains(u"resin"_s)
                  || name.contains(u"gpio_keys"_s) || name.contains(u"gpio-keys"_s) || name.contains(u"volume"_s)
                  || name.contains(u"fp_detect"_s))) {
                continue;
            }

            QFile counts(QString(s_irqPath + u'/' + irq + u"/per_cpu_count"));
            if (!counts.open(QIODevice::ReadOnly)) {
                continue;
            }
            qint64 total = 0;
            const QList<QByteArray> percpu = counts.readAll().trimmed().split(',');
            for (const QByteArray &c : percpu) {
                total += c.trimmed().toLongLong();
            }
            // Key on irq+name, not name alone: two controllers can expose the
            // same handler name, and aggregating them would hide a delta.
            irqs.insert(irq + u':' + name, total);
        }

        if (active) {
            m_userIrqCounts = irqs;
            m_userIrqFired = false;
            m_userIrqFiredNames.clear();
        } else {
            m_userIrqFiredNames.clear();
            for (auto it = irqs.cbegin(); it != irqs.cend(); ++it) {
                if (it.value() > m_userIrqCounts.value(it.key(), it.value())) {
                    m_userIrqFiredNames << it.key();
                }
            }
            m_userIrqFired = !m_userIrqFiredNames.isEmpty();
            m_userIrqCounts = irqs;
        }
    }

    if (!active) {
        // (A) FALLBACK: on some SoCs (Qualcomm msm) /sys/class/wakeup/*/wakeup_count
        // never increments, so the loop above finds nothing. Ask the SoC instead.
        // Read it here, at PrepareForSleep(false): the node is only valid
        // immediately after resume.
        m_socWakeupReason.clear();
        if (m_lastWakeupSources.isEmpty()) {
            QFile reasonFile(s_socWakeupReasonPath);
            if (reasonFile.open(QIODevice::ReadOnly)) {
                m_socWakeupReason = QString::fromLatin1(reasonFile.readAll()).trimmed();
                if (!m_socWakeupReason.isEmpty()) {
                    qCDebug(POWERDEVIL) << "No /sys/class/wakeup candidates; SoC wakeup reason:" << m_socWakeupReason;
                }
            }
        }
        qCDebug(POWERDEVIL) << "Wakeup source of type" << lastWakeupType() << "resumed from sleep, devices:" << m_lastWakeupSources
                            << "power-supply online changed:" << m_powerSupplyChanged
                            << "debug wakeup delta:" << m_debugWakeupDelta << "input IRQs fired:" << m_userIrqFiredNames;
    }
}
#endif

SuspendController::WakeupSources SuspendController::lastWakeupType()
{
    // (0/I) POSITIVE EVIDENCE FIRST. Every other path here works by elimination:
    // it looks for a background source to blame and, failing to find one, leaves
    // UnknownSource so the screen comes on. That inverts badly on this SoC --
    // last_resume_reason is empty on ~3 of every 4 resumes, so path (D) ORs
    // whatever background chatter happened to tick into Telephony|Network and the
    // screen stays dark on a genuine press.
    //
    // An advanced input IRQ is direct proof a human pressed something, so it wins
    // outright and short-circuits before any suppression can apply.
    if (m_userIrqFired) {
        qCDebug(POWERDEVIL) << "input IRQ advanced across suspend - user intent:" << m_userIrqFiredNames;
        return WakeupSources(WakeupSource::UnknownSource);
    }

    WakeupSources sources = WakeupSource::UnknownSource;
    for (const QString &wakeupDevice : m_lastWakeupSources) {
        UdevQt::Device device = m_udevClient->deviceBySysfsPath(wakeupDevice);
        if (device.subsystem() == u"power_supply"_s) {
            sources |= WakeupSource::PowerManagement;
        } else if (device.subsystem() == u"rpmsg"_s) {
            sources |= WakeupSource::Telephony;
        } else if (device.driver().contains(u"alarmtimer"_s) || device.driver().contains(u"rtc"_s)) {
            sources |= WakeupSource::Timer;
        } else if (device.driver().contains(u"ath"_s) || device.driver().contains(u"iwl"_s) || device.driver().contains(u"qca"_s)
                   || device.driver().contains(u"cnss"_s)) {
            sources |= WakeupSource::Network;
        }
    }

    // (B) A genuine charger plug/unplug MUST still light the display, so bail out
    // before any suppression below. Only an actual `online` transition counts --
    // battery-level ticks and charger-notifier chatter leave it unchanged.
    if (m_powerSupplyChanged) {
        qCDebug(POWERDEVIL) << "power-supply online changed (plug/unplug) - treating as user-visible";
        return sources;
    }

    // (A) SoC fallback (Qualcomm): classify from the IRQ name, e.g. "40 ipa",
    // "305 threshold", "306 pm8xxx_rtc_alarm", "302 pmic_pwrkey".
    if (sources == WakeupSources(WakeupSource::UnknownSource) && !m_socWakeupReason.isEmpty()) {
        const QString reason = m_socWakeupReason.toLower();
        auto has = [&reason](const char *needle) {
            return reason.contains(QLatin1String(needle));
        };

        // User intent MUST stay UnknownSource: the display is SUPPOSED to come on
        // when the user presses the power button or touches the fingerprint reader.
        if (has("pwrkey") || has("resin") || has("gpio_keys") || has("gpio-keys") || has("fp_detect")) {
            return sources;
        }

        if (has("rtc") || has("alarm")) {
            sources |= WakeupSource::Timer;
        }
        // ipa/gsi are Qualcomm's IP accelerator; it carries BOTH the WiFi and the
        // cellular data path, so either way it is network traffic.
        if (has("ipa") || has("gsi") || has("wlan") || has("cnss") || has("pcie") || has("wifi")) {
            sources |= WakeupSource::Network;
        }
        if (has("modem") || has("mss") || has("qrtr") || has("smp2p") || has("rpmsg") || has("glink")) {
            sources |= WakeupSource::Telephony;
        }
        // adc_tm threshold / battery-current-limit / thermal watchdogs
        if (has("threshold") || has("bcl") || has("therm") || has("temp") || has("adc") || has("batt") || has("charger")) {
            sources |= WakeupSource::PowerManagement;
        }
    }

    // (D) LAST RESORT: last_resume_reason is frequently EMPTY on this SoC, and
    // m_lastWakeupSources is always empty, so classify from the debugfs
    // wakeup_sources names that advanced across the suspend. Measured examples:
    //   qrtr_ws / ta_qmi_wakelock / rmt_storage_* -> modem      (Telephony)
    //   qcom_rx_wakelock / wlan_* / mgmt_txrx     -> wifi       (Network)
    //   IPA_* / rmnet_*                           -> data path  (Network)
    //   battery / *battery_charger / pmic_glink   -> battery    (PowerManagement)
    // NOTE eventpoll/NETLINK/hal_bluetooth_lock/qup_uart advance on EVERY resume
    // (they are userspace and BT-hook noise) and deliberately match nothing here.
    if (sources == WakeupSources(WakeupSource::UnknownSource)) {
        for (const QString &name : m_debugWakeupDelta) {
            const QString n = name.toLower();
            auto has = [&n](const char *needle) {
                return n.contains(QLatin1String(needle));
            };

            // user intent first -- never suppress these
            if (has("powerkey") || has("pwrkey") || has("resin") || has("gpio_keys") || has("fp_detect")) {
                return WakeupSources(WakeupSource::UnknownSource);
            }
            if (has("rtc") || has("alarm")) {
                sources |= WakeupSource::Timer;
            }
            if (has("qrtr") || has("qmi") || has("rmt_storage") || has("smp2p") || has("glink") || has("modem")
                || has("sscrpcd")) {
                sources |= WakeupSource::Telephony;
            }
            if (has("wlan") || has("qcom_rx") || has("mgmt_txrx") || has("vdev") || has("cnss") || has("ipa")
                || has("rmnet")) {
                sources |= WakeupSource::Network;
            }
            if (has("battery") || has("charger") || has("pmic_glink") || has("therm") || has("bcl")) {
                sources |= WakeupSource::PowerManagement;
            }
        }
        if (sources != WakeupSources(WakeupSource::UnknownSource)) {
            qCDebug(POWERDEVIL) << "classified from debugfs wakeup_sources delta:" << m_debugWakeupDelta;
        }
    }

    return sources;
}

QStringList SuspendController::wakeupDevices()
{
    return m_lastWakeupSources;
}

#include "moc_suspendcontroller.cpp"
