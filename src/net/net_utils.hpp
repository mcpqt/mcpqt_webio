// SPDX-License-Identifier: AGPL-3.0-only
//
// Small string/URL helpers and the SSRF guard used by the generic HTTP client.

#pragma once

#include <string>
#include <QString>
#include <QByteArray>
#include <QMap>

namespace webio {

std::string trim(std::string s);
std::string to_lower(std::string s);


}  // namespace webio
