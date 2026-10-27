// SPDX-License-Identifier: AGPL-3.0-only
//
// Generic outbound HTTP client backed by QNetworkAccessManager. It performs
// raw GET and form POST requests and returns the response without interpreting
// the body.

#pragma once

#include "http_response.h"
#include "tls_verify.h"

#include <QFuture>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QPromise>
#include <QString>
#include <QVariant>

#include <atomic>
#include <cstdint>
#include <memory>

namespace webio
{
  class HttpClient : public QObject
  {
    Q_OBJECT
  public:
    explicit HttpClient(TlsVerify verify);
    ~HttpClient() override;

    /// GET without following redirects.
    QFuture<QVariant> get(const QString& url,
                          const QMap<QString, QString>& headers = {},
                          uint32_t timeout_ms = 30000);

    /// application/x-www-form-urlencoded POST.
    QFuture<QVariant> post_form(const QString& url,
                                const QMap<QString, QString>& form,
                                const QMap<QString, QString>& headers = {},
                                uint32_t timeout_ms = 30000);

  private slots:
    void finish(QNetworkReply* reply);

  private:
    QFuture<QVariant> make_promise(QNetworkReply* reply);

    TlsVerify m_verify;
    QNetworkAccessManager m_netman;
    QMap<QNetworkReply*, std::shared_ptr<QPromise<QVariant>>> m_promises;
  };
} // namespace webio
