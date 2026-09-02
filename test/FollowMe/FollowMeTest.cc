/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "FollowMeTest.h"
#include "FollowMe.h"
#include "MultiVehicleManager.h"
#include "PositionManager.h"
#include "Vehicle.h"
#include "SettingsManager.h"
#include "AppSettings.h"

#include <QtCore/QBuffer>
#include <QtCore/QDateTime>
#include <QtTest/QTest>
#include <QtTest/QSignalSpy>

namespace {

bool injectPositionUpdate(QGCPositionManager *positionManager, const QGeoCoordinate &coordinate)
{
    QGeoPositionInfo update(coordinate, QDateTime::currentDateTimeUtc());
    update.setAttribute(QGeoPositionInfo::HorizontalAccuracy, 1.0);
    update.setAttribute(QGeoPositionInfo::VerticalAccuracy, 1.0);
    update.setAttribute(QGeoPositionInfo::Direction, 45.0);
    update.setAttribute(QGeoPositionInfo::GroundSpeed, 1.0);

    return QMetaObject::invokeMethod(positionManager, "_positionUpdated", Qt::DirectConnection,
                                     Q_ARG(QGeoPositionInfo, update));
}

int countMavlinkMessages(const QSignalSpy &spy, uint32_t messageId)
{
    mavlink_message_t parsingMessage{};
    mavlink_message_t decodedMessage{};
    mavlink_status_t parsingStatus{};
    mavlink_status_t decodedStatus{};
    int count = 0;

    for (const QList<QVariant> &arguments : spy) {
        const QByteArray bytes = arguments.at(0).toByteArray();
        for (const char byte : bytes) {
            if ((mavlink_frame_char_buffer(&parsingMessage, &parsingStatus, static_cast<uint8_t>(byte),
                                           &decodedMessage, &decodedStatus) == MAVLINK_FRAMING_OK)
                && (decodedMessage.msgid == messageId)) {
                ++count;
            }
        }
    }

    return count;
}

} // namespace

void FollowMeTest::_testFollowMe()
{
    FollowMe::instance()->init();
    QGCPositionManager::instance()->init();

    _connectMockLinkNoInitialConnectSequence();

    MultiVehicleManager *vehicleMgr = MultiVehicleManager::instance();
    Vehicle *vehicle = vehicleMgr->activeVehicle();
    vehicle->setFlightMode(vehicle->followFlightMode());
    SettingsManager::instance()->appSettings()->followTarget()->setRawValue(1);

    QSignalSpy spyGCSMotionReport(vehicle, &Vehicle::messagesSentChanged);

    QVERIFY(spyGCSMotionReport.wait(1500));

    SettingsManager::instance()->appSettings()->followTarget()->setRawValue(0);
    _disconnectMockLink();
}

void FollowMeTest::_testSignalLossAndRecovery()
{
    FollowMe::instance()->init();
    QGCPositionManager *const positionManager = QGCPositionManager::instance();
    positionManager->init();

    QBuffer nmeaDevice;
    QVERIFY(nmeaDevice.open(QIODevice::ReadOnly));
    positionManager->setNmeaSourceDevice(&nmeaDevice);

    _connectMockLinkNoInitialConnectSequence();

    Vehicle *const vehicle = MultiVehicleManager::instance()->activeVehicle();
    QVERIFY(vehicle);
    SettingsManager::instance()->appSettings()->followTarget()->setRawValue(1);

    QSignalSpy outgoingBytesSpy(_mockLink, &MockLink::writeBytesQueuedSignal);
    QVERIFY(injectPositionUpdate(positionManager, QGeoCoordinate(47.397742, 8.545594, 488.0)));
    QTRY_VERIFY_WITH_TIMEOUT(countMavlinkMessages(outgoingBytesSpy, MAVLINK_MSG_ID_FOLLOW_TARGET) > 0, 1500);

    QTest::qWait(3500);
    outgoingBytesSpy.clear();
    QTest::qWait(600);
    QCOMPARE(countMavlinkMessages(outgoingBytesSpy, MAVLINK_MSG_ID_FOLLOW_TARGET), 0);

    QVERIFY(injectPositionUpdate(positionManager, QGeoCoordinate(47.397752, 8.545604, 488.2)));
    QTRY_VERIFY_WITH_TIMEOUT(countMavlinkMessages(outgoingBytesSpy, MAVLINK_MSG_ID_FOLLOW_TARGET) > 0, 1500);

    positionManager->setNmeaSourceDevice(nullptr);
    QVERIFY(!positionManager->geoPositionInfo().isValid());

    SettingsManager::instance()->appSettings()->followTarget()->setRawValue(0);
    _disconnectMockLink();
}
