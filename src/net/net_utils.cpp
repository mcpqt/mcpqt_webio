// SPDX-License-Identifier: AGPL-3.0-only

#include "net_utils.h"

#include "errors.h"

#include <QHostAddress>
#include <QHostInfo>
#include <QString>
#include <QUrl>

#include <algorithm>
#include <cctype>

namespace webio {

std::string trim(std::string s)
{
  const auto not_space = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
  s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
  return s;
}

std::string to_lower(std::string s)
{
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

}  // namespace webio
