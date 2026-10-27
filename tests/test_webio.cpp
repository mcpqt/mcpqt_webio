// SPDX-License-Identifier: AGPL-3.0-only
//
// Automated tests for the mcpqt_webio HTTP transport layer and plugin.

#include <QtTest>

#include "http_client.h"
#include "tls_verify.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFuture>
#include <QHostAddress>
#include <QMap>
#include <QNetworkReply>
#include <QPluginLoader>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QVariantMap>

#include "plugin.h"

namespace {

QVariant waitForFuture(QFuture<QVariant> future, int timeout_ms = 5000)
{
  QElapsedTimer timer;
  timer.start();
  while(!future.isFinished() && timer.elapsed() < timeout_ms)
  {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  }
  if(!future.isFinished())
  {
    return QVariant();
  }
  return future.result();
}

class LocalHttpServer : public QObject
{
  Q_OBJECT
public:
  struct Response
  {
    QByteArray statusLine = "HTTP/1.1 200 OK";
    QMap<QByteArray, QByteArray> headers;
    QByteArray body = "hello";
  };

  explicit LocalHttpServer(QObject* parent = nullptr) : QObject(parent)
  {
    m_response.headers.insert("Content-Type", "text/plain");
    connect(&m_server, &QTcpServer::newConnection, this,
            &LocalHttpServer::onNewConnection);
  }

  bool start() { return m_server.listen(QHostAddress::LocalHost, 0); }
  void close() { m_server.close(); }
  quint16 port() const { return m_server.serverPort(); }

  void setResponse(const Response& response) { m_response = response; }
  void setDelay(int ms) { m_delay = ms; }
  void setCloseWithoutResponse(bool close) { m_closeWithoutResponse = close; }

  QString lastMethod() const { return m_lastMethod; }
  QString lastPath() const { return m_lastPath; }
  QMap<QString, QString> lastHeaders() const { return m_lastHeaders; }
  QByteArray lastBody() const { return m_lastBody; }

private slots:
  void onNewConnection()
  {
    QTcpSocket* socket = m_server.nextPendingConnection();
    resetRequestState();
    connect(socket, &QTcpSocket::readyRead, this,
            [this, socket]() { onReadyRead(socket); });
    connect(socket, &QTcpSocket::disconnected, socket,
            &QTcpSocket::deleteLater);
  }

private:
  void resetRequestState()
  {
    m_lastRequest.clear();
    m_lastMethod.clear();
    m_lastPath.clear();
    m_lastHeaders.clear();
    m_lastBody.clear();
  }

  void onReadyRead(QTcpSocket* socket)
  {
    m_lastRequest += socket->readAll();
    const int headerEnd = m_lastRequest.indexOf("\r\n\r\n");
    if(headerEnd < 0)
    {
      return;
    }

    if(m_lastMethod.isEmpty())
    {
      const QByteArray headerBlock = m_lastRequest.left(headerEnd);
      const QList<QByteArray> lines = headerBlock.split('\n');
      if(!lines.isEmpty())
      {
        const QList<QByteArray> requestLine
            = lines.first().trimmed().split(' ');
        if(requestLine.size() >= 2)
        {
          m_lastMethod = QString::fromLatin1(requestLine[0]);
          m_lastPath = QString::fromLatin1(requestLine[1]);
        }
      }
      for(int i = 1; i < lines.size(); ++i)
      {
        const QByteArray line = lines[i].trimmed();
        const int colon = line.indexOf(':');
        if(colon > 0)
        {
          m_lastHeaders.insert(
              QString::fromLatin1(line.left(colon)).trimmed().toLower(),
              QString::fromLatin1(line.mid(colon + 1)).trimmed());
        }
      }
    }

    int contentLength = 0;
    if(m_lastHeaders.contains("content-length"))
    {
      contentLength
          = m_lastHeaders.value("content-length").toInt();
    }
    if(m_lastRequest.size() < headerEnd + 4 + contentLength)
    {
      return;
    }

    m_lastBody = m_lastRequest.mid(headerEnd + 4, contentLength);

    if(m_closeWithoutResponse)
    {
      socket->disconnectFromHost();
      return;
    }

    QByteArray response = m_response.statusLine + "\r\n";
    for(auto it = m_response.headers.cbegin();
        it != m_response.headers.cend(); ++it)
    {
      response += it.key() + ": " + it.value() + "\r\n";
    }
    if(!m_response.headers.contains("Content-Length"))
    {
      response += "Content-Length: "
                  + QByteArray::number(m_response.body.size()) + "\r\n";
    }
    response += "\r\n" + m_response.body;

    auto send = [socket, response]() {
      if(socket->state() == QAbstractSocket::ConnectedState)
      {
        socket->write(response);
        socket->flush();
        socket->disconnectFromHost();
      }
    };
    if(m_delay > 0)
    {
      QTimer::singleShot(m_delay, socket, send);
    }
    else
    {
      send();
    }
  }

  QTcpServer m_server;
  Response m_response;
  QByteArray m_lastRequest;
  QString m_lastMethod;
  QString m_lastPath;
  QMap<QString, QString> m_lastHeaders;
  QByteArray m_lastBody;
  int m_delay = 0;
  bool m_closeWithoutResponse = false;
};

}  // namespace

class WebioTest : public QObject
{
  Q_OBJECT
private slots:
  void initTestCase();
  void cleanupTestCase();

  void getReturnsStatusHeadersAndBody();
  void getSendsCustomHeaders();
  void getDoesNotFollowRedirects();
  void getConnectionRefusedReturnsError();
  void getTimeoutReturnsError();
  void postFormSendsEncodedBody();
  void postFormSendsCustomHeaders();

#ifdef WEBIO_PLUGIN_PATH
  void pluginLoadsAndExposesHttpGet();
#endif

private:
  LocalHttpServer m_server;
};

void WebioTest::initTestCase()
{
  QVERIFY2(m_server.start(), "failed to start local HTTP server");
}

void WebioTest::cleanupTestCase()
{
  m_server.close();
}

void WebioTest::getReturnsStatusHeadersAndBody()
{
  LocalHttpServer::Response response;
  response.statusLine = "HTTP/1.1 200 OK";
  response.headers.insert("Content-Type", "text/plain");
  response.body = "hello world";
  m_server.setResponse(response);
  m_server.setDelay(0);
  m_server.setCloseWithoutResponse(false);

  webio::HttpClient client(webio::TlsVerify{});
  const QUrl url(QString("http://127.0.0.1:%1/test")
                     .arg(m_server.port()));
  const QVariant result = waitForFuture(client.get(url.toString()));
  QVERIFY(result.isValid());

  const QVariantMap map = result.toMap();
  QCOMPARE(map.value("status").toInt(), 200);
  QCOMPARE(map.value("body").toString(),
           "hello world");
  const QVariantMap headers = map.value("headers").toMap();
  QCOMPARE(headers.value("content-type").toString(),
           "text/plain");
}

void WebioTest::getSendsCustomHeaders()
{
  LocalHttpServer::Response response;
  response.body = "ok";
  m_server.setResponse(response);

  webio::HttpClient client(webio::TlsVerify{});
  const QUrl url(QString("http://127.0.0.1:%1/headers")
                     .arg(m_server.port()));
  QMap<QString, QString> headers;
  headers.insert("X-Test", "value");
  const QVariant result = waitForFuture(client.get(url.toString(), headers));
  QVERIFY(result.isValid());
  QCOMPARE(m_server.lastHeaders().value("x-test"),
           "value");
}

void WebioTest::getDoesNotFollowRedirects()
{
  LocalHttpServer::Response response;
  response.statusLine = "HTTP/1.1 302 Found";
  response.headers.insert("Location", "http://example.com/next");
  response.body = "";
  m_server.setResponse(response);

  webio::HttpClient client(webio::TlsVerify{});
  const QUrl url(QString("http://127.0.0.1:%1/redirect")
                     .arg(m_server.port()));
  const QVariant result = waitForFuture(client.get(url.toString()));
  QVERIFY(result.isValid());
  const QVariantMap map = result.toMap();
  QCOMPARE(map.value("status").toInt(), 302);
  QCOMPARE(map.value("location").toString(),
           "http://example.com/next");
}

void WebioTest::getConnectionRefusedReturnsError()
{
  QTcpServer probe;
  QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
  const quint16 closedPort = probe.serverPort();
  probe.close();

  webio::HttpClient client(webio::TlsVerify{});
  const QUrl url(QString("http://127.0.0.1:%1/").arg(closedPort));
  const QVariant result = waitForFuture(client.get(url.toString(), {}, 1000));
  QVERIFY(result.isValid());
  const QVariantMap map = result.toMap();
  QCOMPARE(map.value("ok").toBool(), false);
  QVERIFY(map.contains("error"));
}

void WebioTest::getTimeoutReturnsError()
{
  LocalHttpServer::Response response;
  response.body = "too late";
  m_server.setResponse(response);
  m_server.setDelay(2000);

  webio::HttpClient client(webio::TlsVerify{});
  const QUrl url(
      QString("http://127.0.0.1:%1/slow").arg(m_server.port()));
  const QVariant result
      = waitForFuture(client.get(url.toString(), {}, 200), 3000);
  QVERIFY(result.isValid());
  const QVariantMap map = result.toMap();
  QCOMPARE(map.value("ok").toBool(), false);
  QVERIFY(map.value("error")
              .toString()
              .contains("timed out", Qt::CaseInsensitive));

  m_server.setDelay(0);
}

void WebioTest::postFormSendsEncodedBody()
{
  LocalHttpServer::Response response;
  response.body = "posted";
  m_server.setResponse(response);

  webio::HttpClient client(webio::TlsVerify{});
  const QUrl url(
      QString("http://127.0.0.1:%1/post").arg(m_server.port()));
  QMap<QString, QString> form;
  form.insert("name", "Jane Doe");
  form.insert("email",
              "jane+doe@example.com");
  const QVariant result
      = waitForFuture(client.post_form(url.toString(), form));
  QVERIFY(result.isValid());
  const QVariantMap map = result.toMap();
  QCOMPARE(map.value("status").toInt(), 200);
  QCOMPARE(map.value("body").toString(),
           "posted");
  QCOMPARE(m_server.lastMethod(), "POST");
  QCOMPARE(m_server.lastHeaders().value("content-type"),
           "application/x-www-form-urlencoded");
  const QByteArray body = m_server.lastBody();
  QVERIFY(body.contains("name=Jane+Doe") || body.contains("name=Jane%20Doe"));
  QVERIFY(body.contains("email=jane%2Bdoe%40example.com"));
}

void WebioTest::postFormSendsCustomHeaders()
{
  LocalHttpServer::Response response;
  response.body = "ok";
  m_server.setResponse(response);

  webio::HttpClient client(webio::TlsVerify{});
  const QUrl url(
      QString("http://127.0.0.1:%1/post").arg(m_server.port()));
  QMap<QString, QString> form;
  form.insert("q", "test");
  QMap<QString, QString> headers;
  headers.insert("X-Custom", "yes");
  const QVariant result
      = waitForFuture(client.post_form(url.toString(), form, headers));
  QVERIFY(result.isValid());
  QCOMPARE(m_server.lastHeaders().value("x-custom"),
           "yes");
}

#ifdef WEBIO_PLUGIN_PATH
void WebioTest::pluginLoadsAndExposesHttpGet()
{
  QPluginLoader loader(WEBIO_PLUGIN_PATH);
  QVERIFY2(loader.load(), qPrintable(loader.errorString()));

  QObject* instance = loader.instance();
  QVERIFY(instance != nullptr);

  auto* plugin = qobject_cast<mcp::PluginInterface*>(instance);
  QVERIFY(plugin != nullptr);

  mcp::HostInterface host;
  host.regFunc = [](const QString&) { return true; };
  host.getFuncs = []() { return QStringList{}; };
  host.callFunc = [](const QString&, const QVariantList&) {
    return QVariant{};
  };
  host.unloadSelf = [](const QString&) {};
  host.log = [](int, const QString&) {};

  plugin->initialize(host);

  LocalHttpServer::Response response;
  response.body = "from plugin";
  m_server.setResponse(response);
  const QUrl url(
      QString("http://127.0.0.1:%1/plugin").arg(m_server.port()));

  QFuture<QVariant> future;
  const bool invoked = QMetaObject::invokeMethod(
      instance, "httpGet", Qt::DirectConnection,
      Q_RETURN_ARG(QFuture<QVariant>, future),
      Q_ARG(QString, url.toString()), Q_ARG(QVariant, QVariant(QVariantMap())));
  QVERIFY(invoked);

  const QVariant result = waitForFuture(future);
  QVERIFY(result.isValid());
  const QVariantMap map = result.toMap();
  QCOMPARE(map.value("ok").toBool(), true);
  QCOMPARE(map.value("status").toInt(), 200);
  QCOMPARE(map.value("body").toString(),
           "from plugin");

  plugin->shutdown();
  QVERIFY(loader.unload());
}
#endif

QTEST_GUILESS_MAIN(WebioTest)
#include "test_webio.moc"
