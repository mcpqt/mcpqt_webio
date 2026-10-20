# mcpqt_template_plugin

An independent, out-of-tree plugin template for
[`mcpqt_host`](../mcpqt_host). Copy this repository, rename the plugin, and
replace the example functions with your own.

The plugin is an ordinary Qt plugin (`QObject` + `mcp::PluginInterface`) built
as a `MODULE` library. `mcpqt_host`'s servers load it at runtime with
`QPluginLoader`; the plugin never links against the server executable.

## Contents

| File | Purpose |
| --- | --- |
| `src/example_plugin.cpp` | Plugin class, metadata (`Q_CLASSINFO`), typed public slots. |
| `src/example_plugin.json` | MCP `Tools` and `Resources` declarations, keyed by function signature. |
| `src/plugin.h`, `src/plugin.cpp` | Vendored copies of the host's plugin ABI, so this repository is self-contained. |
| `CMakeLists.txt` | Standalone build; links `mcp::mcp`. |

All C++ sources and the plugin metadata JSON live in `src/`.

> `src/plugin.h`/`src/plugin.cpp` are kept in this repository so the template does not
> depend on the host's source tree. They mirror `mcpqt_host/src/plugin.{h,cpp}`;
> if the plugin ABI changes, update these copies (their includes use the public
> `<mcp/...>` headers from the linked `mcp::mcp` library).

## Building

Requires C++20, Qt 6.4+, and either an installed `mcpqt_host` or its source
tree.

### Against an installed host

```sh
cmake --install /path/to/mcpqt_host/build --prefix /usr/local
cmake -S . -B build
cmake --build build
```

### Against an in-tree host (no install)

```sh
cmake -S . -B build -DMCPQT_HOST_SOURCE_DIR=/path/to/mcpqt_host
cmake --build build
```

This produces `build/mcpqt_example_plugin.so` (`.dylib` / `.dll` on other
platforms).

## Running

Point either server at the built module (a file or a directory):

```sh
# stdio
/path/to/mcp_stdio_server ./build/mcpqt_example_plugin.so

# Streamable HTTP
/path/to/mcp_http_server --port 8848 ./build/mcpqt_example_plugin.so
```

Or copy it into the host's plugin directory and load the directory.

Try it with the bundled client:

```sh
/path/to/mcp_tester /path/to/mcp_stdio_server ./build/mcpqt_example_plugin.so
```

then `list`, and `call` with `example/add` and `{"a":2,"b":40}`.

## Customizing

1. **Rename** the project/target (`mcpqt_example_plugin`), source file, class
   (`ExamplePlugin`), and JSON file.
2. **Update `Q_CLASSINFO`**: `id` is required and must be unique among loaded
   plugins; `version` and `description` are optional. The `id` is also used for
   dependency declarations.
3. **Keep the IID**: `Q_PLUGIN_METADATA(IID "ai.mcp.qt.mcp-plugin/2.0" ...)`
   must match `MCP_PLUGIN_IID`.
4. **Add typed public slots** and declare them in the JSON. A function is
   addressed by its **canonical signature**:

   ```
   <return_type> <ClassName>::<function_name>(<comma-separated param types>)
   ```

   For example `QString ExamplePlugin::hello(QString)`. The metadata key must
   match exactly (return type, class name, and parameter types).

   - Return type: `void` or any registered `QMetaType` (e.g. `QString`,
     `double`, `int`, `QVariant`).
   - 0–10 parameters, using normal `QMetaType`-registered types.
   - Parameter **names** must match the `inputSchema` property names; the host
     maps incoming JSON arguments by name.
   - Throw `std::runtime_error` (or `mcp::Error`) to report a tool error.

5. **Namespace tool names** (e.g. `mytool/do_something`) to avoid collisions
   with other plugins.
6. **Optional resources**: declare them under `Resources`, keyed by URI, with
   the signature of a reader function that takes an optional single `QString`
   (the URI) and returns text. Included in the template as `example://info`.
7. **Optional dependencies**: add `"dependencies": ["other-plugin-id"]` (or
   function signatures) to the metadata file; the host defers loading until
   they are available.

### Elicitation

If a tool needs to ask the user a question, use the host handle stored in
`initialize`:

```cpp
const mcp::ElicitResult answer = m_host.elicit(mcp::ElicitFormRequestParams{
    .message = "Proceed?",
    .requested_schema = QJsonObject{
        {"type", "object"},
        {"properties", QJsonObject{
            {"confirm", QJsonObject{{"type", "boolean"}}},
        }},
        {"required", QJsonArray{"confirm"}},
    },
});
if (answer.action != mcp::ElicitAction::accept) {
    throw std::runtime_error("cancelled by user");
}
```

The host routes elicitation to the session that invoked the tool (stdio or
HTTP).

## Notes

- Build the plugin as a `MODULE` library with an empty `PREFIX` so the artifact
  is named `mcpqt_example_plugin.so`.
- `initialize()` runs once at load; call `registerPublicSlots()` there after
  storing the host handle.
- `shutdown()` runs before the library is detached.
- Functions run synchronously on the thread dispatching the tool call; keep
  them thread-safe if the server may handle concurrent requests.
- A plugin that calls `m_host.claimLogging(...)` becomes pinned and is never
  unloaded (its sink runs code inside the plugin library).

## License

Apache-2.0. See `LICENSE`.
