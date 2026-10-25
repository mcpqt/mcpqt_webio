// SPDX-License-Identifier: AGPL-3.0-only
//
// Small string/URL helpers and the SSRF guard used by the generic HTTP client.

#pragma once

#include <map>
#include <string>

namespace webio {

std::string trim(std::string s);
std::string to_lower(std::string s);

/// application/x-www-form-urlencoded encoding of a key/value map.
std::string url_form_encode(const std::map<std::string, std::string>& form);

/// Reject non-public request targets. Throws BlockedUrlError. Resolves the host
/// (blocking) and rejects loopback / private / link-local / reserved /
/// multicast / unspecified destinations.
void validate_public_url(const std::string& url);

}  // namespace webio
