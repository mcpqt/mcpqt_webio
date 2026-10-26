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

Both functions are public Qt slots and are also published as MCP tools through
`src/webio_plugin.json`. They return a `QFuture<QVariant>` immediately; the
request completes asynchronously.

| Signature | Tool name | Purpose |
| --- | --- | --- |
| `QFuture<QVariant> WebioPlugin::httpGet(QString,QVariant,uint32_t)` | `webio/http_get` | Asynchronous raw HTTP GET; resolves to status, headers, body, and redirect location. |
| `QFuture<QVariant> WebioPlugin::httpPost(QString,QVariant,QVariant,uint32_t)` | `webio/http_post_form` | Asynchronous URL-encoded form POST; same response shape as `http_get`. |

The resolved `QVariant` is a `QVariantMap` with `ok`, `status`, `headers`,
`body`, and `location` (plus `error` when `ok` is false). The response body is
not parsed.

## Using it from another plugin

Declare a dependency on `webio` (or directly on the signatures you call) in the
calling plugin's metadata:

```json
{
  "dependencies": ["webio"]
}
```

Then invoke the service through the host interface. Both functions return a
`QFuture<QVariant>` immediately, so the caller can keep working until it needs
the response:

```cpp
// Generic GET.
const QVariant get_handle = m_host.callFunc(
    QStringLiteral(
        "QFuture<QVariant> WebioPlugin::httpGet(QString,QVariant,uint32_t)"),
    { QVariant(QStringLiteral("https://example.com/")),
      QVariant(QVariantMap()), QVariant(30000) });

const QFuture<QVariant> get_future = get_handle.value<QFuture<QVariant>>();
// ... do other work ...
get_future.waitForFinished();
const QVariantMap get_response = get_future.result().toMap();

// Generic form POST.
const QVariant post_handle = m_host.callFunc(
    QStringLiteral(
        "QFuture<QVariant> WebioPlugin::httpPost(QString,QVariant,QVariant,uint32_t)"),
    { QVariant(QStringLiteral("https://example.com/api")),
      QVariant(QVariantMap{{QStringLiteral("q"), QStringLiteral("term")}}),
      QVariant(QVariantMap()), QVariant(30000) });

const QFuture<QVariant> post_future = post_handle.value<QFuture<QVariant>>();
post_future.waitForFinished();
const QVariantMap post_response = post_future.result().toMap();
```

`QFuture` is a registered Qt metatype, so the handle can be carried through the
host's `QVariant`-based registry. Callers must wait on the future (or attach a
continuation) to obtain the result.

A higher-level plugin (for example a search plugin) should use these raw calls
to reach its upstream service and handle any parsing or result formatting
itself.

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

## Repository layout

| Path | Purpose |
| --- | --- |
| `src/webio_plugin.cpp` | Plugin class, metadata, and typed public slots. |
| `src/webio_plugin.json` | MCP `Tools` declarations keyed by function signature. |
| `src/net/` | Generic HTTP client, response type, TLS settings, and string helpers. |
| `src/plugin.h`, `src/plugin.cpp` | Vendored host plugin ABI. |
| `CMakeLists.txt` | Standalone build. |

## Notes

- Build the plugin as a `MODULE` library with an empty `PREFIX` so the artifact
  is named `mcpqt_webio.so`.
- `initialize()` runs once at load; `shutdown()` releases the HTTP client before
  the library is detached.
- The tool functions return a `QFuture<QVariant>` immediately and complete
  asynchronously on the Qt event loop. The HTTP client is stopped and any
  outstanding requests are failed during `shutdown()`.
- Redirects are not followed automatically; inspect `status` and `location` in
  the response.

## License

GNU Affero General Public License v3.0 (`AGPL-3.0-only`). See `LICENSE`.
