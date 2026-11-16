// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "PdfDocument.h"
#include "AppPreferences.h"
namespace propernotes {
struct DocumentSession {
    std::unique_ptr<PdfDocument> document;
    std::unique_ptr<AppPreferences> preferences;
};
}
