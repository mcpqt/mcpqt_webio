// SPDX-License-Identifier: AGPL-3.0-only
//
// Error types shared by the generic HTTP client.

#pragma once

#include <QString>

#include <stdexcept>

namespace webio
{

  /// Base class for outbound HTTP failures.
  struct HttpError : std::runtime_error
  {
    using std::runtime_error::runtime_error;
    HttpError(QString message) : std::runtime_error(message.toStdString()) {}
  };

  /// Transport-level connection failure.
  struct ConnectError : HttpError
  {
    using HttpError::HttpError;
  };

  /// Request timeout.
  struct TimeoutError : HttpError
  {
    using HttpError::HttpError;
  };

  /// Raised when a request target is not an allowed public http(s)
  /// destination.
  struct BlockedUrlError : std::runtime_error
  {
    using std::runtime_error::runtime_error;
  };

} // namespace webio
