// SPDX-License-Identifier: AGPL-3.0-only
//
// mcpqt_webio: a generic HTTP access service for other plugins. It exposes raw
// GET and form POST requests through the host registry so downstream plugins do
// not need to carry their own network stack. It does not interpret response
// bodies or know about any particular website.

#include "plugin.h"

#include "errors.h"
#include "http_client.hpp"
#include "http_response.hpp"
#include "net_utils.hpp"
#include "tls_verify.hpp"

#include <QFuture>
#include <QObject>
#include <QPromise>
#include <QString>
#include <QVariant>
#include <QVariantMap>

#include <cstdlib>
#include <memory>
#include <optional>
#include <string>



static std::optional<std::string> env_str(const char* name)
{
  const std::string webio_key = std::string("WEBIO_") + name;
  const char* v = std::getenv(webio_key.c_str());
  if(!v || !*v)
  {
    return std::nullopt;
  }
  return std::string(v);
}

static QMap<QString, QString> to_string_map(const QVariant& value)
{
  QMap<QString, QString> result;
  if(!value.canConvert<QVariantMap>())
  {
    return result;
  }
  const QVariantMap map = value.toMap();
  for(auto it = map.begin(); it != map.end(); ++it)
  {
    result.insert(it.key(), it.value().toString());
  }
  return result;
}

static QVariant error_result(const QString& message)
{
  QVariantMap result;
  result.insert(QStringLiteral("ok"), false);
  result.insert(QStringLiteral("error"), message);
  return result;
}

class WebioPlugin : public mcp::PluginInterface
{
  Q_OBJECT
  Q_CLASSINFO("id", "webio")
  Q_CLASSINFO("version", "0.3.0")
  Q_CLASSINFO("description",
              "Generic HTTP GET/POST access service for other plugins.")
  Q_CLASSINFO("license", "AGPL-3.0-only")
  Q_PLUGIN_METADATA(IID MCP_PLUGIN_IID FILE "webio_plugin.json")
  Q_INTERFACES(mcp::PluginInterface)

public:
  explicit WebioPlugin(QObject* parent = nullptr)
      : mcp::PluginInterface(parent)
  {
  }

  void initialize(const mcp::HostInterface& host) override
  {
    m_host = host;

    webio::TlsVerify tls;
    if(const auto v = env_str("SSL_VERIFY"))
    {
      const std::string s = webio::to_lower(webio::trim(*v));
      tls.enabled = !(s == "0" || s == "false" || s == "no" || s == "off");
    }
    if(const auto v = env_str("CA_CERTS"))
    {
      tls.ca_bundle = webio::trim(*v);
    }

    m_http = std::make_unique<webio::HttpClient>(tls);

    registerPublicSlots();
  }

  void shutdown(void) override
  {
    // HttpClient owns its network thread. Stopping it fails outstanding
    // requests and resolves their futures; no thread-pool draining is needed.
    m_http.reset();
  }

public slots:
  // Asynchronous plugin-to-plugin API. These return immediately with a
  // QFuture<QVariant> that resolves to the same response object as the
  // synchronous tools below. Callers can attach continuations or wait.
  QFuture<QVariant> httpGet(QString url, QVariant headers, uint32_t timeout_ms = 30000)
  {
    const uint32_t effective_timeout = !timeout_ms ? 30000 : timeout_ms;
    return complete(m_http->get(url, to_string_map(headers),
                                effective_timeout));
  }

  QFuture<QVariant> httpPost(QString url, QVariant form, QVariant headers, uint32_t timeout_ms = 30000)
  {
    const uint32_t effective_timeout = !timeout_ms ? 30000 : timeout_ms;
    return complete(m_http->post_form(url, to_string_map(form),
                                      to_string_map(headers), effective_timeout));
  }


private:
  QFuture<QVariant> complete(QFuture<QVariant> future)
  {    
    return future.then(
        [](QFuture<QVariant> finished) -> QVariant
        {
          if(finished.isCanceled())
            return error_result(QStringLiteral("request canceled"));
          const QVariant raw = finished.result();
          if(!raw.isValid())
            return error_result(QStringLiteral("no response"));
          QVariantMap result = raw.toMap();
          result.insert(QStringLiteral("ok"), true);
          return result;
        });
  }

  std::unique_ptr<webio::HttpClient> m_http;
};

#include "webio_plugin.moc"
