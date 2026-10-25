// SPDX-License-Identifier: AGPL-3.0-only
//
// mcpqt_webio: a generic HTTP access service for other plugins. It exposes raw
// GET and form POST requests through the host registry so downstream plugins do
// not need to carry their own network stack. It does not interpret response
// bodies or know about any particular website.

#include "plugin.h"

#include "blocked_url_error.hpp"
#include "http_client.hpp"
#include "http_response.hpp"
#include "net_utils.hpp"
#include "tls_verify.hpp"

#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantMap>

#include <cstdlib>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace
{

  std::optional<std::string> env_str(const char* name)
  {
    const std::string webio_key = std::string("WEBIO_") + name;
    const char* v = std::getenv(webio_key.c_str());
    if(!v || !*v)
    {
      return std::nullopt;
    }
    return std::string(v);
  }

  bool env_flag(const char* name)
  {
    const auto v = env_str(name);
    if(!v)
    {
      return false;
    }
    const std::string s = webio::to_lower(webio::trim(*v));
    return s == "1" || s == "true" || s == "yes" || s == "on";
  }

  std::map<std::string, std::string> to_string_map(const QVariant& value)
  {
    std::map<std::string, std::string> result;
    if(!value.canConvert<QVariantMap>())
    {
      return result;
    }
    const QVariantMap map = value.toMap();
    for(auto it = map.begin(); it != map.end(); ++it)
    {
      result[it.key().toStdString()] = it.value().toString().toStdString();
    }
    return result;
  }

  QVariant response_to_variant(const webio::HttpResponse& response)
  {
    QVariantMap headers;
    for(const auto& [key, value] : response.headers)
    {
      headers.insert(QString::fromStdString(key),
                     QString::fromStdString(value));
    }

    QVariantMap result;
    result.insert(QStringLiteral("status"), response.status);
    result.insert(QStringLiteral("body"),
                  QString::fromStdString(response.body));
    result.insert(QStringLiteral("headers"), headers);
    result.insert(QStringLiteral("location"),
                  QString::fromStdString(response.location));
    return result;
  }

  class WebioPlugin : public mcp::PluginInterface
  {
    Q_OBJECT
    Q_CLASSINFO("id", "webio")
    Q_CLASSINFO("version", "0.2.0")
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

      m_allow_private_urls = env_flag("ALLOW_PRIVATE_URLS");
      m_http = std::make_unique<webio::HttpClient>(tls);

      registerPublicSlots();
    }

    void shutdown(void) override { m_http.reset(); }

  public slots:
    QVariant httpGet(QString url, QVariant headers, int timeout_ms)
    {
      return rawRequest(false, url, headers, QVariant(), timeout_ms);
    }

    QVariant httpPostForm(QString url, QVariant form, QVariant headers,
                          int timeout_ms)
    {
      return rawRequest(true, url, headers, form, timeout_ms);
    }

  private:
    QVariant rawRequest(bool post, const QString& url, const QVariant& headers,
                        const QVariant& form, int timeout_ms)
    {
      QVariantMap result;
      const std::string target = url.toStdString();
      try
      {
        if(!m_allow_private_urls)
        {
          webio::validate_public_url(target);
        }

        const int effective_timeout = timeout_ms <= 0 ? 30000 : timeout_ms;
        const webio::HttpResponse response
            = post ? m_http->post_form(target, to_string_map(form),
                                       to_string_map(headers), effective_timeout)
                   : m_http->get(target, to_string_map(headers),
                                 effective_timeout);

        result = response_to_variant(response).toMap();
        result.insert(QStringLiteral("ok"), true);
      }
      catch(const std::exception& e)
      {
        result.insert(QStringLiteral("ok"), false);
        result.insert(QStringLiteral("error"),
                      QString::fromStdString(e.what()));
      }
      return result;
    }

    std::unique_ptr<webio::HttpClient> m_http;
    bool m_allow_private_urls = false;
  };

} // namespace

#include "webio_plugin.moc"
