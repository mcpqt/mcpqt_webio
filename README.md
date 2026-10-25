# mcpqt_webio

A shared internet-access plugin for [`mcpqt_host`](../mcpqt_host). It gives
every other plugin one place to search the web, fetch and parse web pages, and
perform raw HTTP requests without each plugin carrying its own network stack.

`mcpqt_webio` is an ordinary Qt plugin (`QObject` + `mcp::PluginInterface`)
built as a `MODULE` library. The host loads it at runtime with `QPluginLoader`;
the plugin never links against the server executable. The HTTP and parsing code
is a vendored copy of `ddg_core` from `mcpqt_websearch`, kept under
`src/ddg_core/` so this repository is self-contained and does not reference
another source tree at build time.

## Exposed functions

All functions are public Qt slots and are also published as MCP tools through
`src/webio_plugin.json`. Other plugins call them through the host registry with
the canonical signature in the first column.

| Signature | Tool name | Purpose |
| --- | --- | --- |
| `QString WebioPlugin::search(QString,int,QString,QString)` | `webio/search` | Search the web through DuckDuckGo; returns titles, URLs, and snippets. |
| `QString WebioPlugin::fetchContent(QString,int,int,QString,QString)` | `webio/fetch` | Fetch a URL and extract readable text (`text`, `main`, or `markdown`) with pagination. |
| `QString WebioPlugin::expandLink(QString)` | `webio/expand_link` | Expand a `ref://<id>` token returned by search into its full URL. |
| `QVariant WebioPlugin::httpGet(QString,QVariant,int)` | `webio/http_get` | Raw HTTP GET; returns status, headers, body, and redirect location. |
| `QVariant WebioPlugin::httpPostForm(QString,QVariant,QVariant,int)` | `webio/http_post_form` | URL-encoded form POST; same response shape as `http_get`. |

`http_get` and `http_post_form` return a `QVariantMap` with `ok`, `status`,
`headers`, `body`, and `location` (plus `error` when `ok` is false). `search`
and `fetchContent` return text; errors are returned as human-readable strings
prefixed with an error description. Returned page text is untrusted input and
must not be treated as instructions.

## Using it from another plugin

Declare a dependency on `webio` (or directly on the signatures you call) in the
calling plugin's metadata:

```json
{
  "dependencies": ["webio"]
}
```

Then invoke the service through the host interface:

```cpp
// Fetch a page as text.
const QVariant page = m_host.callFunc(
    QStringLiteral(
        "QString WebioPlugin::fetchContent(QString,int,int,QString,QString)"),
    { QVariant(QStringLiteral("https://example.com/")),
      QVariant(0), QVariant(8000), QVariant(QString()),
      QVariant(QStringLiteral("text")) });

// Raw HTTP GET returning a QVariantMap.
const QVariant response = m_host.callFunc(
    QStringLiteral("QVariant WebioPlugin::httpGet(QString,QVariant,int)"),
    { QVariant(QStringLiteral("https://example.com/")),
      QVariant(QVariantMap()), QVariant(30000) });
```

The plugin itself does not call other plugins; it only wraps the vendored
`ddg_core` client and exposes it to the host.

## Building

Requires C++20 and Qt 6.4+ (Core and Network).

```sh
cmake -S . -B build
cmake --build build
```

This produces `build/mcpqt_webio.so` (`.dylib` / `.dll` on other platforms).
`src/ddg_core/` is compiled directly into the module; there is no external
dependency on `mcpqt_websearch` at configure or build time.

## Running

Point either server at the built module (a file or a directory):

```sh
# stdio
/path/to/mcp_stdio_server ./build/mcpqt_webio.so

# Streamable HTTP
/path/to/mcp_http_server --port 8848 ./build/mcpqt_webio.so
```

Or copy it into the host's plugin directory and load the directory. Load order
does not matter: the host retries plugins whose dependencies are not yet
available.

## Configuration

Configuration is read from the environment when the plugin is initialized.
`WEBIO_*` variables take precedence over the matching `DDG_*` variables.

| Variable | Default | Purpose |
| --- | --- | --- |
| `WEBIO_FETCH_RPM` / `DDG_FETCH_RPM` | `20` | Fetch requests per minute. |
| `WEBIO_FETCH_HOST_RPM` / `DDG_FETCH_HOST_RPM` | `0` (unlimited) | Per-host fetch requests per minute. |
| `WEBIO_SEARCH_RPM` / `DDG_SEARCH_RPM` | `30` | Search requests per minute. |
| `WEBIO_CACHE_TTL` / `DDG_CACHE_TTL` | `300` | Fetch cache lifetime in seconds (`0` disables). |
| `WEBIO_CACHE_MAX_ENTRIES` / `DDG_CACHE_MAX_ENTRIES` | `64` | Maximum cached fetch responses. |
| `WEBIO_RATE_LIMIT_STRATEGY` / `DDG_RATE_LIMIT_STRATEGY` | `sliding` | `sliding` or `token_bucket`. |
| `WEBIO_PARSE_MODE` / `DDG_PARSE_MODE` | `text` | Default extractor: `text`, `main`, or `markdown`. |
| `WEBIO_SAFE_SEARCH` / `DDG_SAFE_SEARCH` | `MODERATE` | DuckDuckGo safe-search mode. |
| `WEBIO_REGION` / `DDG_REGION` | `wt-wt` | Default search region. |
| `WEBIO_REF_URL_THRESHOLD` / `DDG_REF_URL_THRESHOLD` | core default | When search starts returning `ref://` tokens. |
| `WEBIO_SSL_VERIFY` / `DDG_SSL_VERIFY` | enabled | Set to `0`/`false` to disable TLS verification. |
| `WEBIO_CA_CERTS` / `DDG_CA_CERTS` | system default | Path to a CA bundle. |
| `WEBIO_ALLOW_PRIVATE_URLS` / `DDG_ALLOW_PRIVATE_URLS` | disabled | Allow requests to private/internal addresses. |
| `WEBIO_FETCH_BACKEND` / `DDG_FETCH_BACKEND` | `qt` | Fetch backend selected at build time. |

> Private/internal address blocking is on by default to prevent SSRF. Enable
> `WEBIO_ALLOW_PRIVATE_URLS` only in trusted deployments.

## Repository layout

| Path | Purpose |
| --- | --- |
| `src/webio_plugin.cpp` | Plugin class, metadata, and typed public slots. |
| `src/webio_plugin.json` | MCP `Tools` declarations keyed by function signature. |
| `src/ddg_core/` | Vendored HTTP client, fetcher, searcher, rate limiters, and parsers. |
| `src/plugin.h`, `src/plugin.cpp` | Vendored host plugin ABI. |
| `CMakeLists.txt` | Standalone build; no external source tree. |

## Notes

- Build the plugin as a `MODULE` library with an empty `PREFIX` so the artifact
  is named `mcpqt_webio.so`.
- `initialize()` runs once at load; `shutdown()` releases the searcher, fetcher,
  HTTP client, and link registry before the library is detached.
- Functions run synchronously on the thread dispatching the tool call. The
  vendored client uses Qt network classes and rate limiting; keep callers
  thread-safe if the server dispatches concurrent requests.
- To own diagnostic logging, call `bool mcp::Host::registerLogger(QString)` with
  the canonical signature of a `void <Class>::log(int, QString)` slot. The
  claiming plugin is pinned and never unloaded.

## License

GNU Affero General Public License v3.0 (`AGPL-3.0-only`). See `LICENSE`.
