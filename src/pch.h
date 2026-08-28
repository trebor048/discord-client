#pragma once

// Standard library (heavy, stable)
#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

// Qt (all modules the app links)
#include <QtWidgets>
#include <QtNetwork>
#include <QtSql>
#include <QtConcurrent>
#include <QtMultimedia>
#include <QtMultimediaWidgets>
#include <QtSvg>

// curl (Discord HTTP/gateway transport)
#include <curl/curl.h>

// Some transitively included Windows headers define min/max macros even when
// NOMINMAX is absent from the caller's compile definitions (test targets that
// compile src/ sources directly). Neutralize them so std::numeric_limits
// <...>::max()/min() calls keep compiling in every TU using this PCH.
#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif
