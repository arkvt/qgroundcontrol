/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/


#include "MissionControllerTest.h"
#include "MissionController.h"
#include "PlanMasterController.h"
#include "SimpleMissionItem.h"
#include "TakeoffMissionItem.h"
#include "MissionSettingsItem.h"
#include "SettingsManager.h"
#include "AppSettings.h"
#include "PlanViewSettings.h"
#include "MultiSignalSpy.h"

#include <QtTest/QTest>

MissionControllerTest::MissionControllerTest(void)
{
    
}

void MissionControllerTest::cleanup(void)
{
    delete _masterController;
    delete _multiSpyMissionController;
    delete _multiSpyMissionItem;

    _masterController           = nullptr;
    _missionController          = nullptr;
    _multiSpyMissionController  = nullptr;
    _multiSpyMissionItem        = nullptr;

    MissionControllerManagerTest::cleanup();
}

void MissionControllerTest::_initForFirmwareType(MAV_AUTOPILOT firmwareType)
{
    MissionControllerManagerTest::_initForFirmwareType(firmwareType);

    // VisualMissionItem signals
    _rgVisualItemSignals[coordinateChangedSignalIndex] = SIGNAL(coordinateChanged(const QGeoCoordinate&));

    // MissionController signals
    _rgMissionControllerSignals[visualItemsChangedSignalIndex] =    SIGNAL(visualItemsChanged());

    // Master controller pulls offline vehicle info from settings
    SettingsManager::instance()->appSettings()->offlineEditingFirmwareClass()->setRawValue(QGCMAVLink::firmwareClass(firmwareType));
    _masterController = new PlanMasterController(this);
    _masterController->setFlyView(false);
    _missionController = _masterController->missionController();

    _multiSpyMissionController = new MultiSignalSpy();
    Q_CHECK_PTR(_multiSpyMissionController);
    QCOMPARE(_multiSpyMissionController->init(_missionController, _rgMissionControllerSignals, _cMissionControllerSignals), true);

    _masterController->start();

    // All signals should some through on start
    QCOMPARE(_multiSpyMissionController->checkOnlySignalsByMask(visualItemsChangedSignalMask), true);
    _multiSpyMissionController->clearAllSignals();

    QmlObjectListModel* visualItems = _missionController->visualItems();
    QVERIFY(visualItems);

    // Empty vehicle only has home position
    QCOMPARE(visualItems->count(), 1);

    // Mission Settings should be in first slot
    MissionSettingsItem* settingsItem = visualItems->value<MissionSettingsItem*>(0);
    QVERIFY(settingsItem);

    // Offline vehicle, so no home position
    QCOMPARE(settingsItem->coordinate().isValid(), false);

    // Empty mission, so no child items possible
    QCOMPARE(settingsItem->childItems()->count(), 0);

    // No waypoint lines
    QmlObjectListModel* simpleFlightPathSegments = _missionController->simpleFlightPathSegments();
    QVERIFY(simpleFlightPathSegments);
    QCOMPARE(simpleFlightPathSegments->count(), 0);
}

void MissionControllerTest::_testEmptyVehicleWorker(MAV_AUTOPILOT firmwareType)
{
    _initForFirmwareType(firmwareType);

    // FYI: A significant amount of empty vehicle testing is in _initForFirmwareType since that
    // sets up an empty vehicle

    QmlObjectListModel* visualItems = _missionController->visualItems();
    QVERIFY(visualItems);
    VisualMissionItem* visualItem = visualItems->value<VisualMissionItem*>(0);
    QVERIFY(visualItem);

    _setupVisualItemSignals(visualItem);
}

void MissionControllerTest::_testEmptyVehiclePX4(void)
{
    _testEmptyVehicleWorker(MAV_AUTOPILOT_PX4);
}

void MissionControllerTest::_testEmptyVehicleAPM(void)
{
    _testEmptyVehicleWorker(MAV_AUTOPILOT_ARDUPILOTMEGA);
}

#if 0
void MissionControllerTest::_testOfflineToOnlineWorker(MAV_AUTOPILOT firmwareType)
{
    // Start offline and add item
    _missionController = new MissionController();
    Q_CHECK_PTR(_missionController);
    _missionController->start(false /* flyView */);
    _missionController->insertSimpleMissionItem(QGeoCoordinate(37.803784, -122.462276), _missionController->visualItems()->count());

    // Go online to empty vehicle
    MissionControllerManagerTest::_initForFirmwareType(firmwareType);

#if 1
    // Due to current limitations, offline items will go away
    QCOMPARE(_missionController->visualItems()->count(), 1);
#else
    //Make sure our offline mission items are still there
    QCOMPARE(_missionController->visualItems()->count(), 2);
#endif
}

void MissionControllerTest::_testOfflineToOnlineAPM(void)
{
    _testOfflineToOnlineWorker(MAV_AUTOPILOT_ARDUPILOTMEGA);
}

void MissionControllerTest::_testOfflineToOnlinePX4(void)
{
    _testOfflineToOnlineWorker(MAV_AUTOPILOT_PX4);
}
#endif

void MissionControllerTest::_setupVisualItemSignals(VisualMissionItem* visualItem)
{
    delete _multiSpyMissionItem;

    _multiSpyMissionItem = new MultiSignalSpy();
    Q_CHECK_PTR(_multiSpyMissionItem);
    QCOMPARE(_multiSpyMissionItem->init(visualItem, _rgVisualItemSignals, _cVisualItemSignals), true);
}

void MissionControllerTest::_testGimbalRecalc(void)
{
    _initForFirmwareType(MAV_AUTOPILOT_PX4);
    _missionController->insertSimpleMissionItem(QGeoCoordinate(0, 0), 1);
    _missionController->insertSimpleMissionItem(QGeoCoordinate(0, 0), 2);
    _missionController->insertSimpleMissionItem(QGeoCoordinate(0, 0), 3);
    _missionController->insertSimpleMissionItem(QGeoCoordinate(0, 0), 4);

    // No specific gimbal yaw set yet
    for (int i=1; i<_missionController->visualItems()->count(); i++) {
        VisualMissionItem* visualItem = _missionController->visualItems()->value<VisualMissionItem*>(i);
        QVERIFY(qIsNaN(visualItem->missionGimbalYaw()));
    }

    // Specify gimbal yaw on settings item should generate yaw on all subsequent items
    const int yawIndex = 2;
    SimpleMissionItem* item = _missionController->visualItems()->value<SimpleMissionItem*>(yawIndex);
    item->cameraSection()->setSpecifyGimbal(true);
    item->cameraSection()->gimbalYaw()->setRawValue(0.0);
    SettingsManager::instance()->planViewSettings()->showGimbalOnlyWhenSet()->setRawValue(false);
    QTest::qWait(500); // Recalcs in MissionController are queued to remove dups. Allow return to main message loop.
    for (int i=1; i<_missionController->visualItems()->count(); i++) {
        //qDebug() << i;
        VisualMissionItem* visualItem = _missionController->visualItems()->value<VisualMissionItem*>(i);
        if (i >= yawIndex) {
            QCOMPARE(visualItem->missionGimbalYaw(), 0.0);
        } else {
            QVERIFY(qIsNaN(visualItem->missionGimbalYaw()));
        }
    }
}

void MissionControllerTest::_testVehicleYawRecalc(void)
{
    _initForFirmwareType(MAV_AUTOPILOT_PX4);

    double wpDistance   = 1000;
    double wpAngleInc   = 45;
    double wpAngle      = 0;

    int cMissionItems = 4;
    QGeoCoordinate currentCoord(0, 0);
    _missionController->insertSimpleMissionItem(currentCoord, 1);
    for (int i=2; i<=cMissionItems; i++) {
        wpAngle += wpAngleInc;
        currentCoord = currentCoord.atDistanceAndAzimuth(wpDistance, wpAngle);
        _missionController->insertSimpleMissionItem(currentCoord, i);
    }

    QTest::qWait(500); // Recalcs in MissionController are queued to remove dups. Allow return to main message loop.

    // No specific vehicle yaw set yet. Vehicle yaw should track flight path.
    double expectedVehicleYaw = wpAngleInc;
    for (int i=2; i<cMissionItems; i++) {
        //qDebug() << i;
        VisualMissionItem* visualItem = _missionController->visualItems()->value<VisualMissionItem*>(i);
        QCOMPARE(visualItem->missionVehicleYaw(), expectedVehicleYaw);
        if (i <= cMissionItems - 1) {
            expectedVehicleYaw += wpAngleInc;
        }
    }

    SimpleMissionItem* simpleItem = _missionController->visualItems()->value<SimpleMissionItem*>(3);
    simpleItem->missionItem().setParam4(66);

    QTest::qWait(500); // Recalcs in MissionController are queued to remove dups. Allow return to main message loop.

    // All item should track vehicle path except for the one changed
    expectedVehicleYaw = wpAngleInc;
    for (int i=2; i<cMissionItems; i++) {
        //qDebug() << i;
        VisualMissionItem* visualItem = _missionController->visualItems()->value<VisualMissionItem*>(i);
        QCOMPARE(visualItem->missionVehicleYaw(), i == 3 ? 66.0 : expectedVehicleYaw);
        if (i <= cMissionItems - 1) {
            expectedVehicleYaw += wpAngleInc;
        }
    }
}

void MissionControllerTest::_testTakeoffToFirstWaypointDistance(void)
{
    _initForFirmwareType(MAV_AUTOPILOT_ARDUPILOTMEGA);

    const QGeoCoordinate takeoffCoordinate(47.397742, 8.545594, 0.0);
    TakeoffMissionItem* takeoffItem = qobject_cast<TakeoffMissionItem*>(_missionController->insertTakeoffItem(takeoffCoordinate, 1));
    QVERIFY(takeoffItem);
    takeoffItem->setLaunchCoordinate(takeoffCoordinate);
    QVERIFY(!_missionController->takeoffToFirstWaypointDistanceAvailable());

    QGeoCoordinate waypointCoordinate = takeoffCoordinate.atDistanceAndAzimuth(500.0, 73.0);
    SimpleMissionItem* waypoint = qobject_cast<SimpleMissionItem*>(_missionController->insertSimpleMissionItem(waypointCoordinate, 2));
    QVERIFY(waypoint);
    waypoint->altitude()->setRawValue(123.0);

    QTRY_VERIFY(_missionController->takeoffToFirstWaypointDistanceAvailable());
    QVERIFY(qAbs(_missionController->takeoffToFirstWaypointDistance()->rawValue().toDouble() - 500.0) < 0.2);

    const double originalAzimuth = takeoffCoordinate.azimuthTo(waypoint->coordinate());
    const double originalAltitude = waypoint->altitude()->rawValue().toDouble();
    _missionController->setDirty(false);
    _missionController->takeoffToFirstWaypointDistance()->setRawValue(800.0);

    QTRY_VERIFY(qAbs(takeoffCoordinate.distanceTo(waypoint->coordinate()) - 800.0) < 0.2);
    QVERIFY(qAbs(takeoffCoordinate.azimuthTo(waypoint->coordinate()) - originalAzimuth) < 0.01);
    QCOMPARE(waypoint->altitude()->rawValue().toDouble(), originalAltitude);
    QVERIFY(_missionController->dirty());

    // Moving the waypoint changes only its bearing. The configured distance remains fixed.
    QGeoCoordinate draggedCoordinate = takeoffCoordinate.atDistanceAndAzimuth(650.0, 120.0);
    waypoint->setCoordinate(draggedCoordinate);
    QTRY_VERIFY(qAbs(takeoffCoordinate.distanceTo(waypoint->coordinate()) - 800.0) < 0.2);
    QVERIFY(qAbs(_missionController->takeoffToFirstWaypointDistance()->rawValue().toDouble() - 800.0) < 0.2);
    QVERIFY(qAbs(takeoffCoordinate.azimuthTo(waypoint->coordinate()) - 120.0) < 0.01);
    QCOMPARE(waypoint->altitude()->rawValue().toDouble(), originalAltitude);

    // Non-coordinate commands between takeoff and the first waypoint are skipped.
    SimpleMissionItem* changeSpeed = qobject_cast<SimpleMissionItem*>(
        _missionController->insertSimpleMissionItem(takeoffCoordinate.atDistanceAndAzimuth(100.0, 10.0), 2));
    QVERIFY(changeSpeed);
    changeSpeed->setCommand(MAV_CMD_DO_CHANGE_SPEED);
    QTRY_VERIFY(_missionController->takeoffToFirstWaypointDistanceAvailable());

    // A loiter command is a supported first route point. Changing its distance or dragging its
    // center must preserve loiter-specific parameters and altitude.
    SimpleMissionItem* loiter = qobject_cast<SimpleMissionItem*>(
        _missionController->insertSimpleMissionItem(takeoffCoordinate.atDistanceAndAzimuth(200.0, 20.0), 2));
    QVERIFY(loiter);
    loiter->setCommand(MAV_CMD_NAV_LOITER_TIME);
    loiter->missionItem().setParam1(45.0);
    loiter->setRadius(-90.0);
    loiter->altitude()->setRawValue(88.0);

    QTRY_VERIFY(_missionController->takeoffToFirstWaypointDistanceAvailable());
    QTRY_VERIFY(qAbs(_missionController->takeoffToFirstWaypointDistance()->rawValue().toDouble() - 200.0) < 0.2);

    _missionController->takeoffToFirstWaypointDistance()->setRawValue(300.0);
    QTRY_VERIFY(qAbs(takeoffCoordinate.distanceTo(loiter->coordinate()) - 300.0) < 0.2);
    QVERIFY(qAbs(takeoffCoordinate.azimuthTo(loiter->coordinate()) - 20.0) < 0.01);
    QCOMPARE(loiter->missionItem().param1(), 45.0);
    QCOMPARE(loiter->loiterRadius(), -90.0);
    QCOMPARE(loiter->altitude()->rawValue().toDouble(), 88.0);

    loiter->setCoordinate(takeoffCoordinate.atDistanceAndAzimuth(450.0, 140.0));
    QTRY_VERIFY(qAbs(takeoffCoordinate.distanceTo(loiter->coordinate()) - 300.0) < 0.2);
    QVERIFY(qAbs(takeoffCoordinate.azimuthTo(loiter->coordinate()) - 140.0) < 0.01);
    QVERIFY(qAbs(_missionController->takeoffToFirstWaypointDistance()->rawValue().toDouble() - 300.0) < 0.2);
    QCOMPARE(loiter->missionItem().param1(), 45.0);
    QCOMPARE(loiter->loiterRadius(), -90.0);
    QCOMPARE(loiter->altitude()->rawValue().toDouble(), 88.0);

    // An unsupported flight-path coordinate before the loiter point blocks this setting.
    SimpleMissionItem* spline = qobject_cast<SimpleMissionItem*>(
        _missionController->insertSimpleMissionItem(takeoffCoordinate.atDistanceAndAzimuth(150.0, 30.0), 2));
    QVERIFY(spline);
    spline->setCommand(MAV_CMD_NAV_SPLINE_WAYPOINT);
    QTRY_VERIFY(!_missionController->takeoffToFirstWaypointDistanceAvailable());

    _missionController->removeVisualItem(_missionController->visualItems()->indexOf(spline));
    QTRY_VERIFY(_missionController->takeoffToFirstWaypointDistanceAvailable());
    QTRY_VERIFY(qAbs(_missionController->takeoffToFirstWaypointDistance()->rawValue().toDouble() - 300.0) < 0.2);

    _missionController->removeVisualItem(_missionController->visualItems()->indexOf(loiter));
    QTRY_VERIFY(_missionController->takeoffToFirstWaypointDistanceAvailable());
    QTRY_VERIFY(qAbs(_missionController->takeoffToFirstWaypointDistance()->rawValue().toDouble() - 800.0) < 0.2);
    _missionController->removeVisualItem(_missionController->visualItems()->indexOf(waypoint));
    QTRY_VERIFY(!_missionController->takeoffToFirstWaypointDistanceAvailable());
}

void MissionControllerTest::_testLoadJsonSectionAvailable(void)
{
    _initForFirmwareType(MAV_AUTOPILOT_PX4);
    _masterController->loadFromFile(":/unittest/SectionTest.plan");

    QmlObjectListModel* visualItems = _missionController->visualItems();
    QVERIFY(visualItems);
    QCOMPARE(visualItems->count(), 5);

    // Check that only waypoint items have camera and speed sections
    for (int i=1; i<visualItems->count(); i++) {
        SimpleMissionItem* item = visualItems->value<SimpleMissionItem*>(i);
        QVERIFY(item);
        if ((int)item->command() == MAV_CMD_NAV_WAYPOINT) {
            QCOMPARE(item->cameraSection()->available(), true);
            QCOMPARE(item->speedSection()->available(), true);
        } else {
            QCOMPARE(item->cameraSection()->available(), false);
            QCOMPARE(item->speedSection()->available(), false);
        }

    }
}

void MissionControllerTest::_testGlobalAltMode(void)
{
    _initForFirmwareType(MAV_AUTOPILOT_PX4);

    struct  _globalAltMode_s {
        QGroundControlQmlGlobal::AltMode   altMode;
        MAV_FRAME                               expectedMavFrame;
    } altModeTestCases[] = {
        { QGroundControlQmlGlobal::AltitudeModeRelative,            MAV_FRAME_GLOBAL_RELATIVE_ALT },
        { QGroundControlQmlGlobal::AltitudeModeAbsolute,            MAV_FRAME_GLOBAL },
        { QGroundControlQmlGlobal::AltitudeModeCalcAboveTerrain,    MAV_FRAME_GLOBAL },
        { QGroundControlQmlGlobal::AltitudeModeTerrainFrame,        MAV_FRAME_GLOBAL_TERRAIN_ALT },
    };

    for (const _globalAltMode_s& testCase: altModeTestCases) {
        _missionController->removeAll();
        _missionController->setGlobalAltitudeMode(testCase.altMode);

        _missionController->insertTakeoffItem(QGeoCoordinate(0, 0), 1);
        _missionController->insertSimpleMissionItem(QGeoCoordinate(0, 0), 2);
        _missionController->insertSimpleMissionItem(QGeoCoordinate(0, 0), 3);
        _missionController->insertSimpleMissionItem(QGeoCoordinate(0, 0), 4);

        SimpleMissionItem* si = qobject_cast<SimpleMissionItem*>(_missionController->visualItems()->value<VisualMissionItem*>(1));
        QCOMPARE(si->altitudeMode(), QGroundControlQmlGlobal::AltitudeModeRelative);
        QCOMPARE(si->missionItem().frame(), MAV_FRAME_GLOBAL_RELATIVE_ALT);

        for (int i=2; i<_missionController->visualItems()->count(); i++) {
            qDebug() << i;
            SimpleMissionItem* si = qobject_cast<SimpleMissionItem*>(_missionController->visualItems()->value<VisualMissionItem*>(i));
            QCOMPARE(si->altitudeMode(), testCase.altMode);
            QCOMPARE(si->missionItem().frame(), testCase.expectedMavFrame);
        }
    }
}
