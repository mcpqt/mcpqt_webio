// SPDX-License-Identifier: AGPL-3.0-only
//
// A single outbound HTTP response.

#pragma once

#include <QByteArray>
#include <QString>
#include <QMap>
#include <QNetworkReply>

namespace webio
{
  struct HttpResponse
  {
    QNetworkReply::NetworkError status
        = QNetworkReply::NetworkError::NoError; // HTTP status code
    QByteArray body;
    QMap<QString, QString> headers; // lower-cased keys
    QString location;               // == headers["location"]

    [[nodiscard]] QString header(const QString& name) const
    {
      return headers.value(name);
    }
  };
} // namespace webio
