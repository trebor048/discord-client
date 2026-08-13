#pragma once

#include <QString>
#include <QStringList>

struct ma_context;
struct ma_context_config;

namespace Acheron {
namespace Core {
namespace AV {

QStringList supportedAudioBackends();

QString configuredAudioBackend();
void setConfiguredAudioBackend(const QString &name);

bool initAudioContext(ma_context *context, const ma_context_config *config);

} // namespace AV
} // namespace Core
} // namespace Acheron
