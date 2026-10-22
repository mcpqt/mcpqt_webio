// SPDX-License-Identifier: AGPL-3.0-only
//
// mcpqt_example_plugin: a starting point for an independent plugin.
//
// Copy this repository, rename the class/file/target, and replace the example
// public slots with your own typed functions. Keep the metadata JSON's
// signature keys in lockstep with the C++ declarations.
//
// Build and run (against an installed host, or with
// -DMCPQT_HOST_SOURCE_DIR=/path/to/mcpqt_host):
//
//   cmake -S . -B build && cmake --build build
//   /path/to/mcp_stdio_server ./build/mcpqt_example_plugin.so

#include "plugin.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVariant>

namespace
{

  class ExamplePlugin : public mcp::PluginInterface
  {
    Q_OBJECT

    // Metadata read by the host. "id" is required and should be unique among
    // loaded plugins; "version"/"description" are optional.
    Q_CLASSINFO("id", "example")
    Q_CLASSINFO("version", "0.1.0")
    Q_CLASSINFO("description", "Example plugin — replace with your own.")
    Q_CLASSINFO("license", "AGPL-3.0-only")

    // The IID must match MCP_PLUGIN_IID (moc does not expand macros here).
    Q_PLUGIN_METADATA(IID MCP_PLUGIN_IID FILE "example_plugin.json")
    Q_INTERFACES(mcp::PluginInterface)

  public:
    explicit ExamplePlugin(QObject* parent = nullptr)
        : mcp::PluginInterface(parent)
    {
    }

    void initialize(const mcp::HostInterface& host) override
    {
      m_host = host;
      registerPublicSlots();
    }

    void shutdown(void) override {}

  public slots:
    // Tool: "QString ExamplePlugin::hello(QString)".
    QString hello(QString name)
    {
      if(name.isEmpty())
      {
        name = QStringLiteral("world");
      }
      return QStringLiteral("Hello, %1!").arg(name);
    }

    // Tool: "double ExamplePlugin::add(double,double)".
    double add(double a, double b) { return a + b; }

    // Tool returning structured JSON (the host maps a JSON object to
    // structuredContent and a text dump): "QVariant ExamplePlugin::info()".
    QVariant info(void)
    {
      return QJsonObject{
        { "plugin", "example" },
        { "version", "0.1.0" },
        { "ready", true },
      };
    }

    // Resource reader for "example://info". A resource reader takes an
    // optional single QString (the requested URI) and returns its text.
    QString readInfo(QString uri)
    {
      (void)uri;
      const QJsonObject doc{
        { "plugin", "example" },
        { "version", "0.1.0" },
        { "notes", "Replace this example with real content." },
      };
      return QString::fromStdString(
          QJsonDocument(doc).toJson(QJsonDocument::Compact).toStdString());
    }
  };

} // namespace

#include "example_plugin.moc"
