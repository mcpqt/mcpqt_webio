// SPDX-License-Identifier: AGPL-3.0-only
//
// mcpqt_webio: a shared internet access service for other plugins. It wraps the
// ddg_core HTTP client and content fetcher and publishes them as host-callable
// slots so other plugins do not need their own network interface.

#include "plugin.h"

#include "constants.hpp"
#include "context.hpp"
#include "fetcher.hpp"
#include "http_client.hpp"
#include "http_response.hpp"
#include "link_registry.hpp"
#include "safe_search_mode.hpp"
#include "searcher.hpp"
#include "tls_verify.hpp"
#include "utils.hpp"

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
    const std::string ddg_key = std::string("DDG_") + name;
    const char* v = std::getenv(webio_key.c_str());
    if(!v || !*v)
    {
      v = std::getenv(ddg_key.c_str());
    }
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
    const std::string s = ddg::to_lower(ddg::trim(*v));
    return s == "1" || s == "true" || s == "yes" || s == "on";
  }

  std::optional<int> env_int(const char* name, int minimum)
  {
    const auto v = env_str(name);
    if(!v)
    {
      return std::nullopt;
    }
    try
    {
      const int value = std::stoi(ddg::trim(*v));
      if(value < minimum)
      {
        return std::nullopt;
      }
      return value;
    }
    catch(...)
    {
      return std::nullopt;
    }
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

  QVariant response_to_variant(const ddg::HttpResponse& response)
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
    Q_CLASSINFO("version", "0.1.0")
    Q_CLASSINFO(
        "description",
        "Shared internet access service for other plugins.")
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
      m_context = ddg::Context(
          [this](int level, const std::string& message)
          {
            if(m_host.log)
            {
              m_host.log(level, QString::fromStdString(message));
            }
          });

      ddg::TlsVerify tls;
      if(const auto v = env_str("SSL_VERIFY"))
      {
        const std::string s = ddg::to_lower(ddg::trim(*v));
        tls.enabled = !(s == "0" || s == "false" || s == "no" || s == "off");
      }
      if(const auto v = env_str("CA_CERTS"))
      {
        tls.ca_bundle = ddg::trim(*v);
      }

      std::string backend = ddg::kCompiledFetchBackend;
      if(const auto v = env_str("FETCH_BACKEND"))
      {
        backend = ddg::to_lower(ddg::trim(*v));
      }

      const int fetch_rpm = env_int("FETCH_RPM", 1).value_or(20);
      const int fetch_host_rpm = env_int("FETCH_HOST_RPM", 0).value_or(0);
      const int cache_ttl = env_int("CACHE_TTL", 0).value_or(300);
      const int cache_max_entries
          = env_int("CACHE_MAX_ENTRIES", 0).value_or(64);

      std::string rate_strategy = "sliding";
      if(const auto v = env_str("RATE_LIMIT_STRATEGY"))
      {
        rate_strategy = ddg::to_lower(ddg::trim(*v));
      }

      std::string parse_mode = "text";
      if(const auto v = env_str("PARSE_MODE"))
      {
        parse_mode = ddg::to_lower(ddg::trim(*v));
      }

      m_links = std::make_shared<ddg::LinkRegistry>();
      m_allow_private_urls = env_flag("ALLOW_PRIVATE_URLS");

      std::string safe_search_name = "MODERATE";
      if(const auto v = env_str("SAFE_SEARCH"))
      {
        safe_search_name = ddg::to_lower(ddg::trim(*v));
      }

      m_region = "wt-wt";
      if(const auto v = env_str("REGION"))
      {
        m_region = ddg::trim(*v);
      }

      const int search_rpm = env_int("SEARCH_RPM", 1).value_or(30);
      const int ref_threshold
          = env_int("REF_URL_THRESHOLD", 0).value_or(
              ddg::kDefaultRefUrlThreshold);

      const ddg::SafeSearchMode mode
          = ddg::safe_search_from_name(safe_search_name)
                .value_or(ddg::SafeSearchMode::Moderate);

      ddg::DuckDuckGoSearcher::Config search_cfg;
      search_cfg.safe_search = mode;
      search_cfg.default_region = m_region;
      search_cfg.backend = backend;
      search_cfg.ssl_verify = tls;
      search_cfg.requests_per_minute = search_rpm;
      search_cfg.rate_limit_strategy = rate_strategy;
      search_cfg.ref_url_threshold = ref_threshold;
      m_searcher
          = std::make_unique<ddg::DuckDuckGoSearcher>(search_cfg, m_links);

      ddg::WebContentFetcher::Config fetch_cfg;
      fetch_cfg.backend = backend;
      fetch_cfg.allow_private_urls = m_allow_private_urls;
      fetch_cfg.ssl_verify = tls;
      fetch_cfg.requests_per_minute = fetch_rpm;
      fetch_cfg.host_requests_per_minute = fetch_host_rpm;
      fetch_cfg.rate_limit_strategy = rate_strategy;
      fetch_cfg.cache_ttl = static_cast<double>(cache_ttl);
      fetch_cfg.cache_max_entries = cache_max_entries;
      fetch_cfg.parse_mode = parse_mode;
      m_fetcher
          = std::make_unique<ddg::WebContentFetcher>(fetch_cfg, m_links);
      m_http = std::make_unique<ddg::HttpClient>(tls);

      registerPublicSlots();
    }

    void shutdown(void) override
    {
      m_http.reset();
      m_fetcher.reset();
      m_searcher.reset();
      m_links.reset();
    }

  public slots:
    QString search(QString query, int max_results, QString region,
                   QString timelimit)
    {
      try
      {
        const auto results = m_searcher->search(
            query.toStdString(), m_context,
            max_results <= 0 ? 10 : max_results, region.toStdString(),
            timelimit.toStdString());
        return QString::fromStdString(
            m_searcher->format_results_for_llm(results));
      }
      catch(const std::exception& e)
      {
        return QStringLiteral("An error occurred while searching: ")
               + QString::fromStdString(e.what());
      }
    }

    QString fetchContent(QString url, int start_index, int max_length,
                         QString backend, QString parse_mode)
    {
      try
      {
        const std::optional<std::string> backend_opt
            = backend.isEmpty()
                  ? std::nullopt
                  : std::optional<std::string>(backend.toStdString());
        const std::optional<std::string> parse_opt
            = parse_mode.isEmpty()
                  ? std::nullopt
                  : std::optional<std::string>(parse_mode.toStdString());
        return QString::fromStdString(m_fetcher->fetch_and_parse(
            url.toStdString(), m_context, start_index,
            max_length <= 0 ? 8000 : max_length, backend_opt, parse_opt));
      }
      catch(const std::exception& e)
      {
        return QStringLiteral("An error occurred while fetching: ")
               + QString::fromStdString(e.what());
      }
    }

    QString expandLink(QString token)
    {
      if(const auto url = m_links->resolve(token.toStdString()))
      {
        return QString::fromStdString(*url);
      }
      return QString::fromStdString(
          ddg::unknown_ref_error(token.toStdString()));
    }

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
          ddg::validate_public_url(target);
        }

        const int effective_timeout = timeout_ms <= 0 ? 30000 : timeout_ms;
        const ddg::HttpResponse response
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

    ddg::Context m_context;
    std::shared_ptr<ddg::LinkRegistry> m_links;
    std::unique_ptr<ddg::DuckDuckGoSearcher> m_searcher;
    std::unique_ptr<ddg::WebContentFetcher> m_fetcher;
    std::unique_ptr<ddg::HttpClient> m_http;
    std::string m_region;
    bool m_allow_private_urls = false;
  };

} // namespace

#include "webio_plugin.moc"
