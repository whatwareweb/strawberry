/*
 * Strawberry Music Player
 * Copyright 2018-2021, Jonas Kvinge <jonas@jkvinge.net>
 *
 * Strawberry is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Strawberry is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Strawberry.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include <algorithm>

#include <QObject>
#include <QLocale>
#include <QList>
#include <QPair>
#include <QByteArray>
#include <QString>
#include <QUrl>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonValue>
#include <QJsonObject>
#include <QJsonArray>

#include "core/shared_ptr.h"
#include "core/application.h"
#include "core/networkaccessmanager.h"
#include "core/logging.h"

#include "jsoncoverprovider.h"
#include "albumcoverfetcher.h"
#include "lastfmcoverprovider.h"

namespace {
constexpr char kUrl[] = "https://ws.audioscrobbler.com/2.0/";
constexpr char kApiKey[] = "211990b4c96782c05d1536e7219eb56e";
constexpr char kSecret[] = "80fd738f49596e9709b1bf9319c444a8";
}  // namespace

LastFmCoverProvider::LastFmCoverProvider(Application *app, SharedPtr<NetworkAccessManager> network, QObject *parent)
    : JsonCoverProvider(QStringLiteral("Last.fm"), true, false, 1.0, true, false, app, network, parent) {}

LastFmCoverProvider::~LastFmCoverProvider() {

  while (!replies_.isEmpty()) {
    QNetworkReply *reply = replies_.takeFirst();
    QObject::disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
  }

}

bool LastFmCoverProvider::StartSearch(const QString &artist, const QString &album, const QString &title, const int id) {

  if (artist.isEmpty() && album.isEmpty() && title.isEmpty()) return false;

  QString method;
  QString type;
  QString query = artist;
  if (album.isEmpty() && !title.isEmpty()) {
    method = QStringLiteral("track.search");
    type = QStringLiteral("track");
    if (!query.isEmpty()) query.append(QLatin1Char(' '));
    query.append(title);
  }
  else {
    method = QStringLiteral("album.search");
    type = QStringLiteral("album");
    if (!album.isEmpty()) {
      if (!query.isEmpty()) query.append(QLatin1Char(' '));
      query.append(album);
    }
  }

  ParamList params = ParamList() << Param(QStringLiteral("api_key"), QLatin1String(kApiKey))
                                 << Param(QStringLiteral("lang"), QLocale().name().left(2).toLower())
                                 << Param(QStringLiteral("method"), method)
                                 << Param(type, query);

  std::sort(params.begin(), params.end());

  QUrlQuery url_query;
  QString data_to_sign;
  for (const Param &param : params) {
    url_query.addQueryItem(QString::fromLatin1(QUrl::toPercentEncoding(param.first)), QString::fromLatin1(QUrl::toPercentEncoding(param.second)));
    data_to_sign += param.first + param.second;
  }
  data_to_sign += QLatin1String(kSecret);

  QByteArray const digest = QCryptographicHash::hash(data_to_sign.toUtf8(), QCryptographicHash::Md5);
  QString signature = QString::fromLatin1(digest.toHex()).rightJustified(32, QLatin1Char('0')).toLower();

  url_query.addQueryItem(QString::fromLatin1(QUrl::toPercentEncoding(QStringLiteral("api_sig"))), QString::fromLatin1(QUrl::toPercentEncoding(signature)));
  url_query.addQueryItem(QString::fromLatin1(QUrl::toPercentEncoding(QStringLiteral("format"))), QString::fromLatin1(QUrl::toPercentEncoding(QStringLiteral("json"))));

  QUrl url(QString::fromLatin1(kUrl));
  QNetworkRequest req(url);
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
  req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
  QNetworkReply *reply = network_->post(req, url_query.toString(QUrl::FullyEncoded).toUtf8());
  replies_ << reply;
  QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, id, type]() { QueryFinished(reply, id, type); });

  return true;

}

void LastFmCoverProvider::QueryFinished(QNetworkReply *reply, const int id, const QString &type) {

  if (!replies_.contains(reply)) return;
  replies_.removeAll(reply);
  QObject::disconnect(reply, nullptr, this, nullptr);
  reply->deleteLater();

  CoverProviderSearchResults results;

  QByteArray data = GetReplyData(reply);
  if (data.isEmpty()) {
    emit SearchFinished(id, results);
    return;
  }

  QJsonObject json_obj = ExtractJsonObj(data);
  if (json_obj.isEmpty()) {
    emit SearchFinished(id, results);
    return;
  }

  QJsonValue value_results;
  if (json_obj.contains(QStringLiteral("results"))) {
    value_results = json_obj[QStringLiteral("results")];
  }
  else if (json_obj.contains(QStringLiteral("error")) && json_obj.contains(QStringLiteral("message"))) {
    int error = json_obj[QStringLiteral("error")].toInt();
    QString message = json_obj[QStringLiteral("message")].toString();
    Error(QStringLiteral("Error: %1: %2").arg(QString::number(error), message));
    emit SearchFinished(id, results);
    return;
  }
  else {
    Error(QStringLiteral("Json reply is missing results."), json_obj);
    emit SearchFinished(id, results);
    return;
  }

  if (!value_results.isObject()) {
    Error(QStringLiteral("Json results is not a object."), value_results);
    emit SearchFinished(id, results);
    return;
  }

  QJsonObject obj_results = value_results.toObject();
  if (obj_results.isEmpty()) {
    Error(QStringLiteral("Json results object is empty."), value_results);
    emit SearchFinished(id, results);
    return;
  }

  QJsonValue value_matches;

  if (type == QStringLiteral("album")) {
    if (obj_results.contains(QStringLiteral("albummatches"))) {
      value_matches = obj_results[QStringLiteral("albummatches")];
    }
    else {
      Error(QStringLiteral("Json results object is missing albummatches."), obj_results);
      emit SearchFinished(id, results);
      return;
    }
  }
  else if (type == QStringLiteral("track")) {
    if (obj_results.contains(QStringLiteral("trackmatches"))) {
      value_matches = obj_results[QStringLiteral("trackmatches")];
    }
    else {
      Error(QStringLiteral("Json results object is missing trackmatches."), obj_results);
      emit SearchFinished(id, results);
      return;
    }
  }

  if (!value_matches.isObject()) {
    Error(QStringLiteral("Json albummatches or trackmatches is not an object."), value_matches);
    emit SearchFinished(id, results);
    return;
  }

  QJsonObject obj_matches = value_matches.toObject();
  if (obj_matches.isEmpty()) {
    Error(QStringLiteral("Json albummatches or trackmatches object is empty."), value_matches);
    emit SearchFinished(id, results);
    return;
  }

  QJsonValue value_type;
  if (!obj_matches.contains(type)) {
    Error(QStringLiteral("Json object is missing %1.").arg(type), obj_matches);
    emit SearchFinished(id, results);
    return;
  }
  value_type = obj_matches[type];

  if (!value_type.isArray()) {
    Error(QStringLiteral("Json album value in albummatches object is not an array."), value_type);
    emit SearchFinished(id, results);
    return;
  }
  QJsonArray array_type = value_type.toArray();

  for (const QJsonValueRef value : array_type) {

    if (!value.isObject()) {
      Error(QStringLiteral("Invalid Json reply, value in albummatches/trackmatches array is not a object."));
      continue;
    }
    QJsonObject obj = value.toObject();
    if (!obj.contains(QStringLiteral("artist")) || !obj.contains(QStringLiteral("image")) || !obj.contains(QStringLiteral("name"))) {
      Error(QStringLiteral("Invalid Json reply, album is missing artist, image or name."), obj);
      continue;
    }
    QString artist = obj[QStringLiteral("artist")].toString();
    QString album;
    if (type == QStringLiteral("album")) {
      album = obj[QStringLiteral("name")].toString();
    }

    QJsonValue json_image = obj[QStringLiteral("image")];
    if (!json_image.isArray()) {
      Error(QStringLiteral("Invalid Json reply, album image is not a array."), json_image);
      continue;
    }
    QJsonArray array_image = json_image.toArray();
    QString image_url_use;
    LastFmImageSize image_size_use(LastFmImageSize::Unknown);
    for (const QJsonValueRef value_image : array_image) {
      if (!value_image.isObject()) {
        Error(QStringLiteral("Invalid Json reply, album image value is not an object."));
        continue;
      }
      QJsonObject obj_image = value_image.toObject();
      if (!obj_image.contains(QStringLiteral("#text")) || !obj_image.contains(QStringLiteral("size"))) {
        Error(QStringLiteral("Invalid Json reply, album image value is missing #text or size."), obj_image);
        continue;
      }
      QString image_url = obj_image[QStringLiteral("#text")].toString();
      if (image_url.isEmpty()) continue;
      LastFmImageSize image_size = ImageSizeFromString(obj_image[QStringLiteral("size")].toString().toLower());
      if (image_url_use.isEmpty() || image_size > image_size_use) {
        image_url_use = image_url;
        image_size_use = image_size;
      }
    }

    if (image_url_use.isEmpty()) continue;

    // Workaround for API limiting to 300x300 images.
    if (image_url_use.contains(QLatin1String("/300x300/"))) {
      image_url_use = image_url_use.replace(QLatin1String("/300x300/"), QLatin1String("/740x0/"));
    }
    QUrl url(image_url_use);
    if (!url.isValid()) continue;

    CoverProviderSearchResult cover_result;
    cover_result.artist = artist;
    cover_result.album = album;
    cover_result.image_url = url;
    cover_result.image_size = QSize(300, 300);
    results << cover_result;
  }
  emit SearchFinished(id, results);

}

QByteArray LastFmCoverProvider::GetReplyData(QNetworkReply *reply) {

  QByteArray data;

  if (reply->error() == QNetworkReply::NoError && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200) {
    data = reply->readAll();
  }
  else {
    if (reply->error() != QNetworkReply::NoError && reply->error() < 200) {
      // This is a network error, there is nothing more to do.
      Error(QStringLiteral("%1 (%2)").arg(reply->errorString()).arg(reply->error()));
    }
    else {
      // See if there is Json data containing "error" and "message" - then use that instead.
      data = reply->readAll();
      QString error;
      QJsonParseError json_error;
      QJsonDocument json_doc = QJsonDocument::fromJson(data, &json_error);
      if (json_error.error == QJsonParseError::NoError && !json_doc.isEmpty() && json_doc.isObject()) {
        QJsonObject json_obj = json_doc.object();
        if (json_obj.contains(QStringLiteral("error")) && json_obj.contains(QStringLiteral("message"))) {
          int code = json_obj[QStringLiteral("error")].toInt();
          QString message = json_obj[QStringLiteral("message")].toString();
          error = QStringLiteral("Error: ") + QString::number(code) + QStringLiteral(": ") + message;
        }
      }
      if (error.isEmpty()) {
        if (reply->error() != QNetworkReply::NoError) {
          error = QStringLiteral("%1 (%2)").arg(reply->errorString()).arg(reply->error());
        }
        else {
          error = QStringLiteral("Received HTTP code %1").arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt());
        }
      }
      Error(error);
    }
    return QByteArray();
  }

  return data;

}

void LastFmCoverProvider::Error(const QString &error, const QVariant &debug) {

  qLog(Error) << "Last.fm:" << error;
  if (debug.isValid()) qLog(Debug) << debug;

}

LastFmCoverProvider::LastFmImageSize LastFmCoverProvider::ImageSizeFromString(const QString &size) {

  if (size == QStringLiteral("small")) return LastFmImageSize::Small;
  else if (size == QStringLiteral("medium")) return LastFmImageSize::Medium;
  else if (size == QStringLiteral("large")) return LastFmImageSize::Large;
  else if (size == QStringLiteral("extralarge")) return LastFmImageSize::ExtraLarge;
  else return LastFmImageSize::Unknown;

}
