# SPDX-License-Identifier: BSD-3-Clause
# SPDX-FileCopyrightText: 2020 Tomaz Canabrava <tcanabrava@kde.org>

add_definitions(-DTRANSLATION_DOMAIN=\"kcm_mobile_power\")

kcmutils_add_qml_kcm(kcm_mobile_power SOURCES ${kcm_mobile_power_SRCS})

target_sources(kcm_mobile_power PRIVATE
    # (K) pdx224: compile the DESKTOP kcm's threshold backend into this module
    # too, rather than duplicating the KAuth calls.
    ${CMAKE_SOURCE_DIR}/kcm/ExternalServiceSettings.cpp
    mobilepower.cpp
    batterymodel.cpp
    statisticsprovider.cpp
    PowerProfileModel.cpp
)
target_include_directories(kcm_mobile_power PRIVATE ${CMAKE_SOURCE_DIR}/kcm) # (K) pdx224

target_link_libraries(kcm_mobile_power PRIVATE
    # NB: KF6::AuthCore, NOT KF6::Auth -- the latter is not a target here (the
    # find_package COMPONENT is spelled "Auth" but exports AuthCore + AuthWidgets).
    KF6::AuthCore # (K) pdx224, for ExternalServiceSettings
    Qt::DBus
    Qt::Core
    KF6::CoreAddons
    KF6::I18n
    KF6::Kirigami
    KF6::KCMUtilsQuick
    KF6::ConfigCore
    KF6::Solid
    powerdevilcore
)

