/*
    SPDX-FileCopyrightText: 2011 Sebastian Kügler <sebas@kde.org>
    SPDX-FileCopyrightText: 2012 Marco Martin <mart@kde.org>
    SPDX-FileCopyrightText: 2015 Kai Uwe Broulik <kde@privat.broulik.de>
    SPDX-FileCopyrightText: 2022 Devin Lin <devin@kde.org>

    SPDX-License-Identifier: LGPL-2.0-or-later
*/

import QtQuick 2.2
import QtQuick.Controls 2.10 as QQC2
import QtQuick.Layouts 1.11

import org.kde.kirigami 2.10 as Kirigami
import org.kde.plasma.components 3.0 as PlasmaComponents
import org.kde.kcmutils
import org.kde.kirigamiaddons.formcard 1.0 as FormCard
import org.kde.kcm.power.mobile.private 1.0

SimpleKCM {
    id: powermanagementModule

    leftPadding: 0
    rightPadding: 0
    topPadding: 0
    bottomPadding: Kirigami.Units.gridUnit

    ColumnLayout {
        width: parent.width
        spacing: 0

        FormCard.FormHeader {
            title: i18n("Devices")
        }

        FormCard.FormCard {
            Repeater {
                model: kcm.batteries

                delegate: FormCard.AbstractFormDelegate {
                    Layout.fillWidth: true

                    onClicked: kcm.push("BatteryPage.qml", { "battery": model.battery, "vendor": model.vendor, "product": model.product, "currentUdi": model.udi })

                    contentItem: RowLayout {
                        spacing: Kirigami.Units.gridUnit

                        Kirigami.Icon {
                            implicitWidth: Kirigami.Units.iconSizes.smallMedium
                            implicitHeight: Kirigami.Units.iconSizes.smallMedium
                            Layout.rightMargin: Kirigami.Units.largeSpacing
                            source: {
                                switch (model.battery.type) {
                                    case 3: return model.battery.chargeState === 1 ? "battery-full-charging" : "battery-full"
                                    case 2: return "battery-ups"
                                    case 9: return "monitor"
                                    case 4: return "input-mouse"
                                    case 5: return "input-keyboard"
                                    case 1: return "phone"
                                    case 7: return "smartphone"
                                    default: return "paint-unknown"
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            QQC2.Label {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                wrapMode: Text.Wrap
                                maximumLineCount: 2
                                color: Kirigami.Theme.textColor
                                text: {
                                    let batteryType;
                                    switch (model.battery.type) {
                                        case 3: batteryType = i18n("Internal battery"); break;
                                        case 2: batteryType = i18n("UPS battery"); break;
                                        case 9: batteryType = i18n("Monitor battery"); break;
                                        case 4: batteryType = i18n("Mouse battery"); break;
                                        case 5: batteryType = i18n("Keyboard battery"); break;
                                        case 1: batteryType = i18n("PDA battery"); break;
                                        case 7: batteryType = i18n("Phone battery"); break;
                                        default: batteryType = i18n("Unknown battery"); break;
                                    }

                                    const chargePercent = i18nc("%1 is the charge percent, % is the percent sign", "%1%", Number(battery.chargePercent).toLocaleString(Qt.locale(), "f", 0));

                                    return (model.battery.chargeState === Battery.Charging) ? i18nc("%1 is battery type, %2 is charge percent", "%1 %2 (Charging)", batteryType, chargePercent) : i18nc("%1 is battery type, %2 is charge percent", "%1 %2", batteryType, chargePercent);
                                }
                            }

                            QQC2.ProgressBar {
                                Layout.fillWidth: true
                                from: 0
                                to: 100
                                value: model.battery.chargePercent
                            }
                        }

                        Kirigami.Icon {
                            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                            source: "arrow-right"
                            implicitWidth: Math.round(Kirigami.Units.iconSizes.small * 0.75)
                            implicitHeight: Math.round(Kirigami.Units.iconSizes.small * 0.75)
                        }
                    }
                }
            }
        }

        FormCard.FormHeader {
            title: i18n("Screen")
        }

        FormCard.FormCard {
            FormCard.FormComboBoxDelegate {
                id: dimScreenCombo
                text: i18nc("Part of a sentence like 'Dim screen after 5 minutes'", "Dim screen after")
                model: kcm.timeOptions()
                currentIndex: kcm.dimScreenIdx
                onCurrentIndexChanged: kcm.dimScreenIdx = currentIndex
            }

            FormCard.FormDelegateSeparator { above: dimScreenCombo; below: screenOffCombo }

            FormCard.FormComboBoxDelegate {
                id: screenOffCombo
                text: i18nc("Part of a sentence like 'Turn off screen after 5 minutes'", "Turn off screen after")
                model: kcm.timeOptions()
                currentIndex: kcm.screenOffIdx
                onCurrentIndexChanged: kcm.screenOffIdx = currentIndex
            }

            FormCard.FormDelegateSeparator { above: screenOffCombo; below: suspendCombo }

            FormCard.FormComboBoxDelegate {
                id: suspendCombo
                text: i18nc("Part of a sentence like 'Suspend device after 5 minutes'", "Suspend device after")
                model: kcm.timeOptions()
                currentIndex: kcm.suspendSessionIdx
                onCurrentIndexChanged: kcm.suspendSessionIdx = currentIndex
            }
        }

        // (K) pdx224: charge limiting via Sony's LRC interface. Applies on change
        // -- this KCM has no Apply button (setButtons(NoAdditionalButton)).
        FormCard.FormHeader {
            title: i18n("Battery Charge Limit")
            visible: kcm.isChargeStopThresholdSupported
        }

        FormCard.FormCard {
            visible: kcm.isChargeStopThresholdSupported

            // ComboBox, NOT FormSpinBoxDelegate. Two reasons, one of them fatal:
            //
            //  1. FormSpinBoxDelegate is BROKEN in the kirigami-addons on this
            //     image. Its own +/- buttons call spinbox.increase()/decrease(),
            //     which do not exist on this Qt's QQC2.SpinBox:
            //         FormSpinBoxDelegate.qml:171: TypeError: Property 'increase'
            //         of object SpinBox is not a function
            //     so the arrows are inert no matter what we bind to them, and
            //     `editable` defaults to false so typing does nothing either.
            //  2. Thresholds are coarse (5% steps) and this is a touch screen.
            //
            // ONE control only. The daemon already derives the resume point:
            //     if start <= 0 or start >= stop: start = max(1, stop - 5)
            // so writing start=0 means "auto 5-point band below stop" and there
            // is nothing for a second dropdown to usefully express. A resume
            // point is REQUIRED (start == stop would restart charging the
            // instant it stopped and cycle the charger every poll), it just does
            // not need to be a user decision.
            //
            // Use onActivated (emitted ONLY on user selection). currentIndex is a
            // binding, and ExternalServiceSettings::load() is an ASYNC KAuth job:
            // at startup the property still holds the "unsupported" sentinel (-1),
            // so an onCurrentIndexChanged handler would fire on the initial clamp
            // and write a bogus threshold back before the load ever returned.
            FormCard.FormComboBoxDelegate {
                id: chargeLimitCombo
                text: i18nc("@label:listbox Stop charging once the battery reaches this percentage", "Charge limit")
                textRole: "text"
                valueRole: "value"
                // 100% == charge normally, no limit. The daemon reads stop>=100
                // as disabled, so this needs no separate on/off switch.
                model: Array.from({length: 11}, function(_, i) {
                    var v = 50 + i * 5;
                    return {
                        text: v === 100 ? i18nc("@item:inlistbox No charge limit", "100% (no limit)")
                                        : i18nc("@item:inlistbox Charge percentage, %1 is a number", "%1%", v),
                        value: v
                    };
                })
                currentIndex: Math.max(0, Math.round((kcm.externalServiceSettings.chargeStopThreshold - 50) / 5))

                onActivated: (index) => {
                    if (!kcm.isChargeStopThresholdSupported) {
                        return;
                    }
                    kcm.externalServiceSettings.chargeStopThreshold = model[index].value;
                    // 0 = "let the daemon pick the resume point".
                    if (kcm.isChargeStartThresholdSupported) {
                        kcm.externalServiceSettings.chargeStartThreshold = 0;
                    }
                    kcm.saveChargeThresholds();
                }
            }
        }

        FormCard.FormHeader {
            title: i18n("Power Profile")
            visible: kcm.isPowerProfileSupported
        }

        FormCard.FormCard {
            visible: kcm.isPowerProfileSupported

            FormCard.FormComboBoxDelegate {
                id: powerProfileCombo
                text: i18nc("@label:combobox Power Save, Balanced or Performance profile", "Power profile")
                model: kcm.powerProfileModel
                textRole: "name"
                valueRole: "value"
                currentIndex: kcm.powerProfileIdx
                onCurrentIndexChanged: kcm.powerProfileIdx = currentIndex
            }
        }
    }
}
