# mcpqt Plugin HOWTO

This document explains how to build a plugin against the mcpqt host ABI, how to
invoke host functions and other plugins, and how to emit diagnostics through the
host's logger. It is derived from the host sources in `mcpqt_host/src`, the
`tool_search` plugin, and the `websearch` plugin.

## 1. The plugin ABI

Every plugin derives from `mcp::PluginInterface` (see `plugin.h`) and overrides
`initialize()`/`shutdown()`. The host hands the plugin a `mcp::HostInterface` at
initialization:

```cpp
struct HostInterface
{
  std::function<QStringList()> getFuncs;                     // list registered signatures
  std::function<QVariant(const QString& signature,
                         const QVariantList& args)> callFunc; // invoke a function
  std::function<bool(const QString& signature)> regFunc;     // publish one slot
  std::function<void(const QString& reason)> unloadSelf;     // ask the host to unload us
  std::function<void(int level, const QString& message)> log; // diagnostic logging
};
```

All cross-plugin and host-service calls go through this object. There is no
separate linking or service locator: the host's `PluginManager` owns the
process-wide function registry and routes `callFunc` into it.

## 2. Publishing your functions

A plugin's functions are ordinary Qt public slots. Declare them under
`public slots:` and either call `registerPublicSlots()` in `initialize()` or
declare them in the plugin's JSON metadata (the host auto-publishes tools listed
in metadata even if you did not call `registerPublicSlots()`).

The signature format is fully qualified and must match what the host builds:

```
<return_type> <ClassName>::<methodSignature>
```

Examples:

```
QString ExamplePlugin::hello(QString)
double ExamplePlugin::add(double,double)
QVariant ExamplePlugin::info()
```

`registerPublicSlots()` iterates the meta-object and registers every **public**
slot using that exact format. Tool metadata lives in the plugin's
`Q_PLUGIN_METADATA` JSON, keyed by signature:

```json
{
  "Tools": {
    "QString ExamplePlugin::hello(QString)": {
      "name": "example/hello",
      "description": "Return a friendly greeting.",
      "inputSchema": {
        "type": "object",
        "properties": { "name": { "type": "string" } }
      }
    }
  }
}
```

Resources are declared under `"Resources"`, keyed by URI, with a `"signature"`
pointing at a slot that renders the resource:

```json
{
  "Resources": {
    "example://info": {
      "name": "example-info",
      "mimeType": "application/json",
      "signature": "QString ExamplePlugin::readInfo(QString)"
    }
  }
}
```

## 3. Calling host functions

The host installs a small set of built-in services into the same registry. Use
`m_host.callFunc(signature, args)` with one `QVariant` per declared parameter.
The built-in host signatures are:

| Signature | Purpose |
| --- | --- |
| `QVariant mcp::Host::toolList()` | List tool names registered on the active server. |
| `QVariant mcp::Host::toolDetails(QString)` | Fetch name/description/source for one tool. |
| `QVariant mcp::Host::elicit(QVariant)` | Route an `ElicitRequestParams` JSON object to the active session. |
| `bool mcp::Host::registerLogger(QString)` | Claim the process-wide log sink (see §5). |

Example from `mcpqt_tool_search/src/tool_search_plugin.cpp`:

```cpp
const QVariant raw = m_host.callFunc(
    QStringLiteral("QVariant mcp::Host::toolList()"), {});
const QStringList names = raw.toStringList();

const QVariant details_raw = m_host.callFunc(
    QStringLiteral("QVariant mcp::Host::toolDetails(QString)"),
    { QVariant(tool_name) });
const QVariantMap details = details_raw.toMap();
```

Notes:

- The return value is a `QVariant`. For void functions it is invalid.
- On unknown signatures, arity/type mismatches, or exceptions escaping the slot,
  `callFunc` throws `mcp::Error`. Wrap calls in `try`/`catch` when the service is
  optional (as `tool_search` does).
- `m_host.getFuncs()` returns every currently registered signature; this is a
  convenient fallback when a host service is absent.
- `callFunc` is not exposed to MCP clients directly — it is the in-process ABI
  used by plugins. The host-only `plugin` control tool is deliberately excluded
  from the registry so plugins cannot call or override it.

## 4. Calling other plugins

There is no difference between calling a host function and calling another
plugin's slot: use `callFunc` and the other plugin's fully-qualified signature.
For example, to call the example plugin's greeting from your plugin:

```cpp
const QVariant reply = m_host.callFunc(
    QStringLiteral("QString ExamplePlugin::hello(QString)"),
    { QVariant(QStringLiteral("plugin A")) });
const QString text = reply.toString();
```

Because the registry stores weak references to loaded plugins, a call to an
unloaded plugin fails cleanly with an error instead of touching detached code.
To discover what is available, iterate `m_host.getFuncs()` or the
`QVariant mcp::Host::toolList()` service.

### Dependencies between plugins

Declare dependencies in the plugin metadata so the host loads you only after the
required functions/plugin ids exist:

```json
{
  "dependencies": [
    "QString ExamplePlugin::hello(QString)",
    "tool-search"
  ]
}
```

`load_directory()` retries plugins whose dependencies are not yet satisfied, so
directory scan order does not matter. A failed `load_file()` means dependencies
were missing or initialization threw. Unloading a plugin is refused while
another loaded plugin depends on it.

## 5. Logging

### 5.1 Emitting log messages

`HostInterface::log` is the plugin-facing diagnostic channel. The host routes it
through its global sink. Level is an `mcp::LogLevel` integer:

| Value | Level |
| --- | --- |
| 0 | trace |
| 1 | debug |
| 2 | info |
| 3 | warn |
| 4 | error |
| 5 | off |

Example from `mcpqt_websearch/src/websearch_plugin.cpp`:

```cpp
if(m_host.log)
{
  m_host.log(level, QString::fromStdString(message));
}
```

Always guard with `if(m_host.log)` for compatibility with hosts that do not
provide the member. Logs must never go to stdout when using the stdio transport;
stdout is owned by the protocol. The host's default sink writes timestamped lines
to stderr.

### 5.2 Claiming the global log sink

A plugin may take exclusive ownership of process-wide logging by calling the
host function `bool mcp::Host::registerLogger(QString)` with the signature of its
own `void <Class>::log(int, QString)` slot. Requirements:

- The slot must have exactly two parameters (`int`, `QString`) and return `void`.
- The slot must already be published in the registry (call
  `registerPublicSlots()` first, or declare a tool for it).
- The first successful claim wins; later claims and `set_log_sink()` calls fail.
- The claiming plugin is **pinned**: `unload_all()` leaves it loaded because its
  sink executes code inside the plugin's library.

Example:

```cpp
void initialize(const mcp::HostInterface& host) override
{
  m_host = host;
  registerPublicSlots();
  try
  {
    m_host.callFunc(QStringLiteral("bool mcp::Host::registerLogger(QString)"),
                    { QVariant(QStringLiteral("void MyPlugin::log(int,QString)")) });
  }
  catch(const std::exception& e)
  {
    // Claim failed (already owned, or host lacks the service).
  }
}

public slots:
  void log(int level, QString message)
  {
    // Write `message` to your sink. Avoid re-entering host logging here.
  }
```

The host's own SDK macros (`MCP_LOG_TRACE` .. `MCP_LOG_ERROR` in `log.h`) are
for internal host code; plugins should use `m_host.log` instead.

## 6. Lifecycle and unloading

- `m_host.unloadSelf(reason)` asks the host to unload your plugin. The host
  refuses if another plugin depends on you or you own the log sink.
- `shutdown()` is called before your library is detached. Release resources
  there (the websearch plugin resets its searcher/fetcher).
- Do not call plugin functions while a load/unload is in flight.

## 7. Complete example

```cpp
#include "plugin.h"

#include <QObject>
#include <QString>
#include <QVariant>

namespace
{
  class GreeterPlugin : public mcp::PluginInterface
  {
    Q_OBJECT
    Q_CLASSINFO("id", "greeter")
    Q_CLASSINFO("version", "0.1.0")
    Q_CLASSINFO("description", "Calls the example plugin and logs.")
    Q_CLASSINFO("license", "AGPL-3.0-only")
    Q_PLUGIN_METADATA(IID MCP_PLUGIN_IID FILE "greeter_plugin.json")
    Q_INTERFACES(mcp::PluginInterface)

  public:
    explicit GreeterPlugin(QObject* parent = nullptr)
        : mcp::PluginInterface(parent) {}

    void initialize(const mcp::HostInterface& host) override
    {
      m_host = host;
      registerPublicSlots();
      if(m_host.log)
        m_host.log(2, QStringLiteral("greeter: initialized"));
    }

  public slots:
    QString greet(QString name)
    {
      try
      {
        const QVariant reply = m_host.callFunc(
            QStringLiteral("QString ExamplePlugin::hello(QString)"),
            { QVariant(name) });
        if(m_host.log)
          m_host.log(1, QStringLiteral("greeter: delegated to example"));
        return reply.toString();
      }
      catch(const std::exception& e)
      {
        if(m_host.log)
          m_host.log(4, QStringLiteral("greeter: call failed: %1")
                           .arg(QString::fromUtf8(e.what())));
        return QStringLiteral("example plugin unavailable");
      }
    }
  };
} // namespace

#include "greeter_plugin.moc"
```

Metadata (`greeter_plugin.json`):

```json
{
  "dependencies": ["QString ExamplePlugin::hello(QString)"],
  "Tools": {
    "QString GreeterPlugin::greet(QString)": {
      "name": "greeter/greet",
      "description": "Greet via the example plugin.",
      "inputSchema": {
        "type": "object",
        "properties": { "name": { "type": "string" } },
        "required": ["name"]
      }
    }
  }
}
```

## 8. Build and load

The template `CMakeLists.txt` builds a Qt plugin module. Add your sources to the
`add_library(... MODULE ...)` call and keep `CMAKE_AUTOMOC ON` so `Q_OBJECT` and
`Q_PLUGIN_METADATA` are processed. After building, load the `.so` through the
host's `plugin` control tool (`load <path>`) or by placing it in a directory
passed to `PluginManager::load_directory()`.

```bash
cmake -S . -B build && cmake --build build
# then, from the host's plugin control tool:
#   plugin load /path/to/build/libgreeter_plugin.so
```

## 9. Quick reference

| Task | Mechanism |
| --- | --- |
| Publish a function | `public slots:` + `registerPublicSlots()` or metadata `"Tools"` |
| Call a host service | `m_host.callFunc("QVariant mcp::Host::toolList()", {})` |
| Call another plugin | `m_host.callFunc("QString OtherPlugin::fn(QString)", { QVariant(x) })` |
| List available functions | `m_host.getFuncs()` |
| Emit a log line | `m_host.log(level, message)` with level 0–5 |
| Own the global log sink | `callFunc("bool mcp::Host::registerLogger(QString)", { "void C::log(int,QString)" })` |
| Request elicitation | `callFunc("QVariant mcp::Host::elicit(QVariant)", { json })` |
| Unload yourself | `m_host.unloadSelf("reason")` |
| Declare dependencies | `"dependencies": ["<signature>", "<plugin-id>"]` in metadata |
