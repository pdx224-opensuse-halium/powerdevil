// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// SPDX-FileCopyrightText: 2020 Tomaz Canabrava <tcanabrava@kde.org>

#pragma once

#include "batterymodel.h"

#include <KQuickConfigModule>
#include <KSharedConfig>
#include <memory>

#include <PowerDevilProfileSettings.h>

class PowerProfileModel;

// (K) pdx224: the desktop KCM's charge-threshold backend, reused verbatim.
namespace PowerDevil
{
class ExternalServiceSettings;
}

class MobilePower : public KQuickConfigModule
{
    Q_OBJECT
    Q_PROPERTY(BatteryModel *batteries READ batteries CONSTANT)
    Q_PROPERTY(int dimScreenIdx READ dimScreenIdx WRITE setDimScreenIdx NOTIFY dimScreenIdxChanged)
    Q_PROPERTY(int screenOffIdx READ screenOffIdx WRITE setScreenOffIdx NOTIFY screenOffIdxChanged)
    Q_PROPERTY(int suspendSessionIdx READ suspendSessionIdx WRITE setSuspendSessionIdx NOTIFY suspendSessionIdxChanged)
    Q_PROPERTY(QObject *powerProfileModel READ powerProfileModel CONSTANT)
    Q_PROPERTY(int powerProfileIdx READ powerProfileIdx WRITE setPowerProfileIdx NOTIFY powerProfileIdxChanged)
    Q_PROPERTY(bool isPowerProfileSupported READ isPowerProfileSupported NOTIFY isPowerProfileSupportedChanged)
    // (K) pdx224 charge limiting. The thresholds themselves live on the shared
    // ExternalServiceSettings object so QML can bind to them directly; only the
    // "is it supported" flags need mirroring here, matching PowerKCM.
    Q_PROPERTY(QObject *externalServiceSettings READ externalServiceSettings CONSTANT)
    Q_PROPERTY(bool isChargeStopThresholdSupported READ isChargeStopThresholdSupported NOTIFY isChargeStopThresholdSupportedChanged)
    Q_PROPERTY(bool isChargeStartThresholdSupported READ isChargeStartThresholdSupported NOTIFY isChargeStartThresholdSupportedChanged)

public:
    MobilePower(QObject *parent, const KPluginMetaData &metaData);

    Q_INVOKABLE QStringList timeOptions() const;

    void setDimScreenIdx(int idx);
    void setScreenOffIdx(int idx);
    void setSuspendSessionIdx(int idx);
    int dimScreenIdx();
    int screenOffIdx();
    int suspendSessionIdx();

    QObject *powerProfileModel() const;
    void setPowerProfileIdx(int idx);
    int powerProfileIdx() const;
    bool isPowerProfileSupported() const;

    BatteryModel *batteries();

    // (K) pdx224
    QObject *externalServiceSettings() const;
    bool isChargeStopThresholdSupported() const;
    bool isChargeStartThresholdSupported() const;
    // KAbstractConfigModule::save() is a plain virtual -- NOT Q_INVOKABLE and not
    // in a Q_SLOTS block -- so `kcm.save()` from QML throws
    // "Property 'save' ... is not a function" and aborts the handler mid-way.
    // Every stock control on this page dodges that by writing a C++ property
    // whose setter calls save() itself; the thresholds live on a separate
    // object, so they need this explicit entry point.
    Q_INVOKABLE void saveChargeThresholds();

    Q_SIGNAL void dimScreenIdxChanged();
    Q_SIGNAL void screenOffIdxChanged();
    Q_SIGNAL void suspendSessionIdxChanged();
    Q_SIGNAL void powerProfileIdxChanged();
    Q_SIGNAL void isPowerProfileSupportedChanged();
    // (K) pdx224
    Q_SIGNAL void isChargeStopThresholdSupportedChanged();
    Q_SIGNAL void isChargeStartThresholdSupportedChanged();

    QString stringForValue(int value);

    void load() override;
    void save() override;

private:
    BatteryModel *m_batteries;
    PowerProfileModel *m_powerProfileModel;
    PowerDevil::ExternalServiceSettings *m_externalServiceSettings; // (K) pdx224

    PowerDevil::ProfileSettings *m_settingsAC;
    PowerDevil::ProfileSettings *m_settingsBattery;
    PowerDevil::ProfileSettings *m_settingsLowBattery;
    QList<PowerDevil::ProfileSettings *> m_settings;

    int m_suspendSessionTime;
    int m_dimScreenTime;
    bool m_dimScreen;
    int m_screenOffTime;
    bool m_screenOff;
    QString m_powerProfile;
};
