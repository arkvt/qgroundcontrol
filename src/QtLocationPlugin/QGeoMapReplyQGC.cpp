/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "QGeoMapReplyQGC.h"

#include "ElevationMapProvider.h"
#include "Fact.h"
#include "FlightMapSettings.h"
#include "MapProvider.h"
#include "QGCMapEngine.h"
#include "QGCMapUrlEngine.h"
#include "QGeoFileTileCacheQGC.h"
#include "SettingsManager.h"

#include <DeviceInfo.h>
#include <QGCFileDownload.h>
#include <QGCLoggingCategory.h>

#include <QtCore/QBuffer>
#include <QtCore/QFile>
#include <QtGui/QImage>
#include <QtLocation/private/qgeotilespec_p.h>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QSslError>

QGC_LOGGING_CATEGORY(QGeoTiledMapReplyQGCLog, "qgc.qtlocationplugin.qgeomapreplyqgc")

QByteArray QGeoTiledMapReplyQGC::_bingNoTileImage;
QByteArray QGeoTiledMapReplyQGC::_badTile;

QByteArray QGeoTiledMapReplyQGC::_applyMapSaturation(const QByteArray &imageData, const QString &format) const
{
    const SharedMapProvider mapProvider = UrlFactory::getMapProviderFromQtMapId(tileSpec().mapId());
    if (!mapProvider || mapProvider->isElevationProvider()) {
        return imageData;
    }

    const Fact *const saturationFact = SettingsManager::instance()->flightMapSettings()->mapSaturation();
    const int saturationPercent = qBound(0, saturationFact->rawValue().toInt(), 150);
    if (saturationPercent == 100) {
        return imageData;
    }

    QImage image;
    if (!image.loadFromData(imageData)) {
        return imageData;
    }

    image = image.convertToFormat(QImage::Format_ARGB32);
    const double saturation = static_cast<double>(saturationPercent) / 100.0;
    for (int y = 0; y < image.height(); ++y) {
        QRgb *const scanLine = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const QRgb pixel = scanLine[x];
            const double luminance = (0.2126 * qRed(pixel)) +
                                     (0.7152 * qGreen(pixel)) +
                                     (0.0722 * qBlue(pixel));
            const int red = qBound(0, qRound(luminance + ((qRed(pixel) - luminance) * saturation)), 255);
            const int green = qBound(0, qRound(luminance + ((qGreen(pixel) - luminance) * saturation)), 255);
            const int blue = qBound(0, qRound(luminance + ((qBlue(pixel) - luminance) * saturation)), 255);
            scanLine[x] = qRgba(red, green, blue, qAlpha(pixel));
        }
    }

    QByteArray adjustedData;
    QBuffer buffer(&adjustedData);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, format.toLatin1().constData())) {
        return imageData;
    }
    return adjustedData;
}

QGeoTiledMapReplyQGC::QGeoTiledMapReplyQGC(QNetworkAccessManager *networkManager, const QNetworkRequest &request, const QGeoTileSpec &spec, QObject *parent)
    : QGeoTiledMapReply(spec, parent)
    , _networkManager(networkManager)
    , _request(request)
{
    // qCDebug(QGeoTiledMapReplyQGCLog) << Q_FUNC_INFO << this;

    _initDataFromResources();

    (void) connect(this, &QGeoTiledMapReplyQGC::errorOccurred, this, [this](QGeoTiledMapReply::Error error, const QString &errorString) {
        qCWarning(QGeoTiledMapReplyQGCLog) << error << errorString;
        setMapImageData(_badTile);
        setMapImageFormat("png");
        setCached(false);
    }, Qt::AutoConnection);

    QGCFetchTileTask* const task = QGeoFileTileCacheQGC::createFetchTileTask(UrlFactory::getProviderTypeFromQtMapId(spec.mapId()), spec.x(), spec.y(), spec.zoom());
    (void) connect(task, &QGCFetchTileTask::tileFetched, this, &QGeoTiledMapReplyQGC::_cacheReply);
    (void) connect(task, &QGCMapTask::error, this, &QGeoTiledMapReplyQGC::_cacheError);
    getQGCMapEngine()->addTask(task);
}

QGeoTiledMapReplyQGC::~QGeoTiledMapReplyQGC()
{
    // qCDebug(QGeoTiledMapReplyQGCLog) << Q_FUNC_INFO << this;
}

void QGeoTiledMapReplyQGC::_initDataFromResources()
{
    if (_bingNoTileImage.isEmpty()) {
        QFile file(":/res/BingNoTileBytes.dat");
        if (file.open(QFile::ReadOnly)) {
            _bingNoTileImage = file.readAll();
            file.close();
        }
    }

    if (_badTile.isEmpty()) {
        QFile file(":/res/images/notile.png");
        if (file.open(QFile::ReadOnly)) {
            _badTile = file.readAll();
            file.close();
        }
    }
}

void QGeoTiledMapReplyQGC::_networkReplyFinished()
{
    QNetworkReply* const reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        setError(QGeoTiledMapReply::UnknownError, tr("Unexpected Error"));
        return;
    }
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        return;
    }

    if (!reply->isOpen()) {
        setError(QGeoTiledMapReply::ParseError, tr("Empty Reply"));
        return;
    }

    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if ((statusCode < HTTP_Response::SUCCESS_OK) || (statusCode >= HTTP_Response::REDIRECTION_MULTIPLE_CHOICES)) {
        setError(QGeoTiledMapReply::CommunicationError, reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString());
        return;
    }

    QByteArray image = reply->readAll();
    if (image.isEmpty()) {
        setError(QGeoTiledMapReply::ParseError, tr("Image is Empty"));
        return;
    }

    const SharedMapProvider mapProvider = UrlFactory::getMapProviderFromQtMapId(tileSpec().mapId());
    Q_CHECK_PTR(mapProvider);

    if (mapProvider->isBingProvider() && (image == _bingNoTileImage)) {
        setError(QGeoTiledMapReply::CommunicationError, tr("Bing Tile Above Zoom Level"));
        return;
    }

    if (mapProvider->isElevationProvider()) {
        const SharedElevationProvider elevationProvider = std::dynamic_pointer_cast<const ElevationProvider>(mapProvider);
        image = elevationProvider->serialize(image);
        if (image.isEmpty()) {
            setError(QGeoTiledMapReply::ParseError, tr("Failed to Serialize Terrain Tile"));
            return;
        }
    }
    const QString format = mapProvider->getImageFormat(image);
    if (format.isEmpty()) {
        setError(QGeoTiledMapReply::ParseError, tr("Unknown Format"));
        return;
    }

    QGeoFileTileCacheQGC::cacheTile(mapProvider->getMapName(), tileSpec().x(), tileSpec().y(), tileSpec().zoom(), image, format);

    setMapImageData(_applyMapSaturation(image, format));
    setMapImageFormat(format);
    setFinished(true);
}

void QGeoTiledMapReplyQGC::_networkReplyError(QNetworkReply::NetworkError error)
{
    if (error != QNetworkReply::OperationCanceledError) {
        const QNetworkReply* const reply = qobject_cast<const QNetworkReply*>(sender());
        if (!reply) {
            setError(QGeoTiledMapReply::CommunicationError, tr("Invalid Reply"));
        } else {
            setError(QGeoTiledMapReply::CommunicationError, reply->errorString());
        }
    } else {
        setFinished(true);
    }
}

void QGeoTiledMapReplyQGC::_networkReplySslErrors(const QList<QSslError> &errors)
{
    QString errorString;
    for (const QSslError &error : errors) {
        if (!errorString.isEmpty()) {
            (void) errorString.append('\n');
        }
        (void) errorString.append(error.errorString());
    }

    if (!errorString.isEmpty()) {
        setError(QGeoTiledMapReply::CommunicationError, errorString);
    }
}

void QGeoTiledMapReplyQGC::_cacheReply(QGCCacheTile *tile)
{
    if (tile) {
        setMapImageData(_applyMapSaturation(tile->img(), tile->format()));
        setMapImageFormat(tile->format());
        setCached(true);
        setFinished(true);
        delete tile;
    } else {
        setError(QGeoTiledMapReply::UnknownError, tr("Invalid Cache Tile"));
    }
}

void QGeoTiledMapReplyQGC::_cacheError(QGCMapTask::TaskType type, QStringView errorString)
{
    Q_UNUSED(errorString);

    Q_ASSERT(type == QGCMapTask::taskFetchTile);

    if (!QGCDeviceInfo::isInternetAvailable()) {
        setError(QGeoTiledMapReply::CommunicationError, tr("Network Not Available"));
        return;
    }

    _request.setOriginatingObject(this);

    QNetworkReply* const reply = _networkManager->get(_request);
    reply->setParent(this);
    QGCFileDownload::setIgnoreSSLErrorsIfNeeded(*reply);

    (void) connect(reply, &QNetworkReply::finished, this, &QGeoTiledMapReplyQGC::_networkReplyFinished);
    (void) connect(reply, &QNetworkReply::errorOccurred, this, &QGeoTiledMapReplyQGC::_networkReplyError);
    (void) connect(reply, &QNetworkReply::sslErrors, this, &QGeoTiledMapReplyQGC::_networkReplySslErrors);
    (void) connect(this, &QGeoTiledMapReplyQGC::aborted, reply, &QNetworkReply::abort);
}

void QGeoTiledMapReplyQGC::abort()
{
    QGeoTiledMapReply::abort();
}
