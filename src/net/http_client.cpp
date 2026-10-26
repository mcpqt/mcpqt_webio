// SPDX-License-Identifier: AGPL-3.0-only

#include "http_client.hpp"

#include "net_utils.hpp"
#include "errors.h"

#include <QMetaObject>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QUrl>
#include <QVariantMap>

#include <exception>
#include <memory>

namespace webio
{
  static void configure_ssl(QNetworkRequest& req, const TlsVerify& verify)
  {
    QSslConfiguration cfg = req.sslConfiguration();
    if (!verify.enabled) {
      cfg.setPeerVerifyMode(QSslSocket::VerifyNone);
    } else if (!verify.ca_bundle.empty()) {
      const auto certs = QSslCertificate::fromPath(QString::fromStdString(verify.ca_bundle.string()));
      cfg.setCaCertificates(certs);
    }
    req.setSslConfiguration(cfg);
  }

  static QNetworkRequest make_request(const QString&                url,
                                      const QMap<QString, QString>& headers,
                                      int                           timeout_ms,
                                      const TlsVerify& verify)
  {
    QNetworkRequest req (url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::ManualRedirectPolicy);
    req.setTransferTimeout(timeout_ms);
    for (const auto& [k, v] : headers.asKeyValueRange()) {
      req.setRawHeader(k.toLatin1(), v.toLatin1());
    }
    configure_ssl(req, verify);
    return req;
  }

  static QVariant response_to_variant(const HttpResponse& response)
  {
    QVariantMap headers;
    for (auto it = response.headers.cbegin(); it != response.headers.cend(); ++it)
      headers.insert(it.key(), it.value());

    QVariantMap result;
    result.insert(QStringLiteral("status"), response.status);
    result.insert(QStringLiteral("body"), QString::fromUtf8(response.body));
    result.insert(QStringLiteral("headers"), headers);
    result.insert(QStringLiteral("location"), response.location);
    return result;
  }

  HttpClient::HttpClient(TlsVerify verify)
      : QObject(nullptr),
        m_verify(std::move(verify))
  {
    connect(&m_netman, &QNetworkAccessManager::finished, this, &HttpClient::finish);
  }

  // Abort requests still in flight and fail their futures.
  HttpClient::~HttpClient()
  {
    // Move the map out first: abort() may emit finished synchronously, and
    // finish() would otherwise erase from the container we are iterating.
    auto pending = m_promises;
    m_promises.clear();
    for (auto it = pending.begin(); it != pending.end(); ++it)
    {
      it.key()->abort();
      it.value()->setException(std::make_exception_ptr(HttpError("HTTP client shutting down")));
      it.value()->finish();
    }
  }

  QFuture<QVariant> HttpClient::make_promise(QNetworkReply* reply)
  {
    auto promise = std::make_shared<QPromise<QVariant>>();
    m_promises.insert(reply, promise);
    promise->start();
    return promise->future();
  }

  QFuture<QVariant> HttpClient::get(const QString&                url,
                                    const QMap<QString, QString>& headers,
                                    int                           timeout_ms)
  {
    return make_promise(m_netman.get(make_request(url, headers, timeout_ms, m_verify)));
  }

  /// application/x-www-form-urlencoded encoding of a key/value map.
  static inline QByteArray url_form_encode(const QMap<QString, QString>& form)
  {
    QByteArray out = "";
    for (const auto& [k, v] : form.asKeyValueRange())
    {
      if (!out.isEmpty())
        out += '&';
      out += QUrl::toPercentEncoding(k) + '=' + QUrl::toPercentEncoding(v);
    }
    return out;
  }

  QFuture<QVariant> HttpClient::post_form(const QString&                url,
                                          const QMap<QString, QString>& form,
                                          const QMap<QString, QString>& headers,
                                          int                           timeout_ms)
  {
    QNetworkRequest req = make_request(url, headers, timeout_ms, m_verify);
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/x-www-form-urlencoded"));
    return make_promise(m_netman.post(req, url_form_encode(form)));
  }

  void build_error(std::shared_ptr<QPromise<QVariant>> promise, QString message, QString default_message)
  {
    QVariantMap result;
    result.insert(QStringLiteral("ok"), false);
    if(message.isEmpty())
      result.insert(QStringLiteral("error"), default_message);
    result.insert(QStringLiteral("error"), message);
    promise->addResult(result);
  }

  void HttpClient::finish(QNetworkReply* reply)
  {
    auto promise_iter = m_promises.find(reply);
    if (promise_iter != m_promises.end())
    {
      const auto promise = promise_iter.value();
      m_promises.erase(promise_iter);

      HttpResponse res;
      res.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                       .value<QNetworkReply::NetworkError>();
      for (const auto& [k, v] : reply->rawHeaderPairs())
        res.headers.insert(QString::fromLatin1(k).toLower(),
                           QString::fromLatin1(v));
      res.location = res.headers.value(QStringLiteral("location"));
      res.body = reply->readAll();

      const QNetworkReply::NetworkError err = reply->error();
      if (err == QNetworkReply::TimeoutError ||
          err == QNetworkReply::OperationCanceledError)
      {
        build_error(promise, "request timed out: " + reply->errorString(), "request timed out");
      }
      else if (
          err == QNetworkReply::ConnectionRefusedError ||
          err == QNetworkReply::RemoteHostClosedError ||
          err == QNetworkReply::HostNotFoundError ||
          err == QNetworkReply::NetworkSessionFailedError ||
          err == QNetworkReply::TemporaryNetworkFailureError ||
          err == QNetworkReply::UnknownNetworkError)
      {
        build_error(promise, reply->errorString(), "connection failed");
      }
      else if (err != QNetworkReply::NoError)
      {
        build_error(promise, reply->errorString(), "network error");
      }
      else
        promise->addResult(response_to_variant(res));
      promise->finish();
    }
    reply->deleteLater();
  }

}  // namespace webio
