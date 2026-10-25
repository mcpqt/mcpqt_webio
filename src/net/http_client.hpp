// SPDX-License-Identifier: AGPL-3.0-only
//
// Generic outbound HTTP client backed by QNetworkAccessManager. It performs
// raw GET and form POST requests and returns the response without interpreting
// the body.

#pragma once

#include "http_response.hpp"
#include "tls_verify.hpp"

#include <map>
#include <string>

namespace webio {

class HttpClient {
public:
    explicit HttpClient(TlsVerify verify);

    /// GET without following redirects.
    HttpResponse get(const std::string&                       url,
                     const std::map<std::string, std::string>& headers    = {},
                     int                                       timeout_ms = 30000);

    /// application/x-www-form-urlencoded POST.
    HttpResponse post_form(const std::string&                       url,
                           const std::map<std::string, std::string>& form,
                           const std::map<std::string, std::string>& headers    = {},
                           int                                       timeout_ms = 30000);

private:
    HttpResponse perform(const std::string&                       method,
                         const std::string&                       url,
                         const std::map<std::string, std::string>& headers,
                         const std::string&                       body,
                         int                                      timeout_ms);

    TlsVerify verify_;
};

}  // namespace webio
