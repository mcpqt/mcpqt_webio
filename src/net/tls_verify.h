// SPDX-License-Identifier: AGPL-3.0-only
//
// TLS verification settings (plugin TLS verification settings).

#pragma once

#include <QString>

namespace webio
{

  struct TlsVerify
  {
    bool enabled = true;
    QString ca_bundle; // when non-empty (and enabled), trust this PEM file
  };

} // namespace webio
