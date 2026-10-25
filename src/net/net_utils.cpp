// SPDX-License-Identifier: AGPL-3.0-only

#include "net_utils.hpp"

#include "errors.h"

#include <QHostAddress>
#include <QHostInfo>
#include <QString>
#include <QUrl>

#include <algorithm>
#include <cctype>

namespace webio {

std::string trim(std::string s) {
    const auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string url_form_encode(const std::map<std::string, std::string>& form) {
    std::string out;
    for (const auto& [k, v] : form) {
        if (!out.empty()) out += '&';
        out += QUrl::toPercentEncoding(QString::fromStdString(k)).toStdString();
        out += '=';
        out += QUrl::toPercentEncoding(QString::fromStdString(v)).toStdString();
    }
    return out;
}

namespace {

bool is_disallowed_v4(quint32 ip) {
    const quint32 b0 = (ip >> 24) & 0xff;
    const quint32 b1 = (ip >> 16) & 0xff;
    const quint32 b2 = (ip >> 8) & 0xff;
    if (b0 == 0)                            return true;  // 0.0.0.0/8
    if (b0 == 10)                           return true;  // RFC1918
    if (b0 == 127)                          return true;  // loopback
    if (b0 == 169 && b1 == 254)             return true;  // link-local / metadata
    if (b0 == 172 && b1 >= 16 && b1 <= 31)  return true;  // RFC1918
    if (b0 == 192 && b1 == 168)             return true;  // RFC1918
    if (b0 == 100 && b1 >= 64 && b1 <= 127) return true;  // CGNAT 100.64/10
    if (b0 == 192 && b1 == 0 && b2 == 0)    return true;  // 192.0.0.0/24
    if (b0 == 192 && b1 == 0 && b2 == 2)    return true;  // TEST-NET-1
    if (b0 == 198 && (b1 == 18 || b1 == 19)) return true; // 198.18.0.0/15
    if (b0 == 198 && b1 == 51 && b2 == 100) return true;  // TEST-NET-2
    if (b0 == 203 && b1 == 0 && b2 == 113)  return true;  // TEST-NET-3
    if (b0 >= 224)                          return true;  // multicast + reserved + broadcast
    return false;
}

bool is_disallowed(const QHostAddress& addr) {
    if (addr.protocol() == QAbstractSocket::IPv4Protocol) {
        return is_disallowed_v4(addr.toIPv4Address());
    }
    bool          ok     = false;
    const quint32 mapped = addr.toIPv4Address(&ok);  // IPv4-mapped IPv6
    if (ok) return is_disallowed_v4(mapped);

    if (addr.isLoopback() || addr.isLinkLocal() || addr.isSiteLocal() ||
        addr.isUniqueLocalUnicast() || addr.isMulticast() || addr.isBroadcast()) {
        return true;
    }
    if (addr == QHostAddress(QHostAddress::AnyIPv6) ||
        addr == QHostAddress(QHostAddress::Any)) {
        return true;
    }
    return !addr.isGlobal();
}

}  // namespace

void validate_public_url(const std::string& url) {
    const QUrl        u(QString::fromStdString(url));
    const std::string scheme = u.scheme().toLower().toStdString();
    if (scheme != "http" && scheme != "https") {
        throw BlockedUrlError("unsupported URL scheme '" + u.scheme().toStdString() +
                              "://' (only http and https are allowed)");
    }
    const QString host_q = u.host();
    if (host_q.isEmpty()) throw BlockedUrlError("URL has no host");
    const std::string host = host_q.toStdString();

    const std::string lowered = to_lower(host);
    if (lowered == "localhost" || lowered.ends_with(".localhost")) {
        throw BlockedUrlError("refusing to request loopback host '" + host + "'");
    }

    const int port = u.port(scheme == "https" ? 443 : 80);
    if (port <= 0 || port > 65535) {
        throw BlockedUrlError("invalid port in URL '" + url + "'");
    }

    const QHostInfo info = QHostInfo::fromName(host_q);
    if (info.error() != QHostInfo::NoError || info.addresses().isEmpty()) {
        throw BlockedUrlError("could not resolve host '" + host + "': " +
                              info.errorString().toStdString());
    }
    for (const QHostAddress& addr : info.addresses()) {
        if (is_disallowed(addr)) {
            throw BlockedUrlError("refusing to request '" + host +
                                  "' — it resolves to non-public address " +
                                  addr.toString().toStdString());
        }
    }
}

}  // namespace webio
