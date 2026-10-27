#pragma once
#ifdef _WIN32
#include <Windows.h>
#endif
#include <memory>
#include <utility>

namespace posted_message {
// A successful PostMessage transfers ownership even if the receiver has gone away.
// Own first, then dispatch: shutdown, null payloads and throwing handlers are safe.
template<class Receiver, class Payload>
void deliver(Receiver* receiver, void (Receiver::*handler)(std::unique_ptr<Payload>), LPARAM data) {
    auto payload = std::unique_ptr<Payload>(reinterpret_cast<Payload*>(data));
    if (receiver && payload) (receiver->*handler)(std::move(payload));
}
}
