#pragma once

#include "Enums.hpp"
#include "Objects.hpp"

namespace Acheron {
namespace Discord {

struct Inbound
{
    OpCode opcode;
    QJsonValue data;

    std::optional<int> s;
    std::optional<QString> t;

    static Inbound fromJson(const QJsonObject &root)
    {
        Inbound obj;
        obj.opcode = root["op"].isDouble() ? static_cast<OpCode>(root["op"].toInt()) : OpCode::DISPATCH;
        obj.data = root["d"];

        if (root.contains("s") && !root["s"].isNull())
            obj.s = root["s"].toInt();

        if (root.contains("t") && !root["t"].isNull())
            obj.t = root["t"].toString();

        return obj;
    }

    template <typename T>
    T getData() const
    {
        return T::fromJson(data.toObject());
    }
};

struct Hello
{
    int heartbeatInterval;

    static Hello fromJson(const QJsonObject &root)
    {
        Hello obj;
        const int interval = root["heartbeat_interval"].toInt();
        obj.heartbeatInterval = interval > 0 ? interval : 41250;
        return obj;
    }
};

} // namespace Discord
} // namespace Acheron
