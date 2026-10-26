# mcpqt_webio

A generic internet-access plugin for [`mcpqt_host`](../mcpqt_host). It gives
other plugins a single place to perform raw HTTP GET and form POST requests
without each plugin carrying its own network stack. It does not know about or
interpret any particular website: response bodies are returned verbatim.

`mcpqt_webio` is an ordinary Qt plugin (`QObject` + `mcp::PluginInterface`)
built as a `MODULE` library. The host loads it at runtime with `QPluginLoader`;
the plugin never links against the server executable. The transport layer lives
under `src/net/` and is website-agnostic.

## Exposed functions

The synchronous functions are public Qt slots and are also published as MCP
tools through `src/webio_plugin.json`. The asynchronous functions are public Qt
slots for plugin-to-plugin use and are not published as MCP tools.

| Signature | Tool name | Purpose |
| --- | --- | --- |
| `QVariant WebioPlugin::httpGet(QString,QVariant,int)` | `webio/http_get` | Raw HTTP GET; returns status, headers, body, and redirect location. |
| `QVariant WebioPlugin::httpPostForm(QString,QVariant,QVariant,int)` | `webio/http_post_form` | URL-encoded form POST; same response shape as `http_get`. |
| `QFuture<QVariant> WebioPlugin::httpGetAsync(QString,QVariant,int)` | — | Asynchronous GET; returns a `QFuture` that resolves to the same response shape. |
| `QFuture<QVariant> WebioPlugin::httpPostFormAsync(QString,QVariant,QVariant,int)` | — | Asynchronous form POST; returns a `QFuture` that resolves to the same response shape. |

All four return a `QVariantMap` with `ok`, `status`, `headers`, `body`, and
`location` (plus `error` when `ok` is false). The response body is not parsed.

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
// Generic GET.
const QVariant get_response = m_host.callFunc(
    QStringLiteral("QVariant WebioPlugin::httpGet(QString,QVariant,int)"),
    { QVariant(QStringLiteral("https://example.com/")),
      QVariant(QVariantMap()), QVariant(30000) });

// Generic form POST.
const QVariant post_response = m_host.callFunc(
    QStringLiteral(
        "QVariant WebioPlugin::httpPostForm(QString,QVariant,QVariant,int)"),
    { QVariant(QStringLiteral("https://example.com/api")),
      QVariant(QVariantMap{{QStringLiteral("q"), QStringLiteral("term")}}),
      QVariant(QVariantMap()), QVariant(30000) });
```

A higher-level plugin (for example a search plugin) should use these raw calls
to reach its upstream service and handle any parsing or result formatting
itself.

### Asynchronous plugin-to-plugin calls

The `*Async` functions return a `QFuture<QVariant>` immediately. The request is
started on the HTTP client's dedicated network thread, so the calling plugin can
keep working until it needs the response:

```cpp
const QVariant handle = m_host.callFunc(
    QStringLiteral(
        "QFuture<QVariant> WebioPlugin::httpGetAsync(QString,QVariant,int)"),
    { QVariant(QStringLiteral("https://example.com/")),
      QVariant(QVariantMap()), QVariant(30000) });

const QFuture<QVariant> future = handle.value<QFuture<QVariant>>();
// ... do other work ...
future.waitForFinished();
const QVariantMap response = future.result().toMap();
```

`QFuture` is a registered Qt metatype, so the handle can be carried through the
host's `QVariant`-based registry. Callers that use the synchronous functions
above do not need to wait manually.

## Building

Requires C++20 and Qt 6.4+ (Core and Network).

```sh
cmake -S . -B build
cmake --build build
```

This produces `build/mcpqt_webio.so` (`.dylib` / `.dll` on other platforms).
There is no external source-tree dependency.

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

| Variable | Default | Purpose |
| --- | --- | --- |
| `WEBIO_SSL_VERIFY` | enabled | Set to `0`/`false` to disable TLS verification. |
| `WEBIO_CA_CERTS` | system default | Path to a CA bundle. |
| `WEBIO_ALLOW_PRIVATE_URLS` | disabled | Allow requests to private/internal addresses. |

> Private/internal address blocking is on by default to prevent SSRF. Enable
> `WEBIO_ALLOW_PRIVATE_URLS` only in trusted deployments.

## Repository layout

| Path | Purpose |
| --- | --- |
| `src/webio_plugin.cpp` | Plugin class, metadata, and typed public slots. |
| `src/webio_plugin.json` | MCP `Tools` declarations keyed by function signature. |
| `src/net/` | Generic HTTP client, response type, TLS settings, and URL/SSRF helpers. |
| `src/plugin.h`, `src/plugin.cpp` | Vendored host plugin ABI. |
| `CMakeLists.txt` | Standalone build. |

## Notes

- Build the plugin as a `MODULE` library with an empty `PREFIX` so the artifact
  is named `mcpqt_webio.so`.
- `initialize()` runs once at load; `shutdown()` releases the HTTP client before
  the library is detached.
- The synchronous tool functions block the dispatching thread until the HTTP
  request completes. The asynchronous functions return a `QFuture` immediately
  and run the request on the HTTP client's dedicated network thread. The
  network thread is stopped and any outstanding requests are failed during
  `shutdown()`.
- Redirects are not followed automatically; inspect `status` and `location` in
  the response.

## License

GNU Affero General Public License v3.0 (`AGPL-3.0-only`). See `LICENSE`.
