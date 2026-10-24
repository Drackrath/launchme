#pragma once
#include "worker_messages.hpp"
#include "../platform/posted_message.hpp"

template<class Receiver>
bool dispatchWorkerMessage(Receiver* receiver, UINT message, LPARAM payload) {
    switch (message) {
#define LAUNCHME_MESSAGE(id, offset, type, handler) \
    case id: posted_message::deliver(receiver, &Receiver::handler, payload); return true;
#include "worker_messages.def"
#undef LAUNCHME_MESSAGE
    default: return false;
    }
}

template<class Receiver>
void discardWorkerMessages(HWND window) {
    constexpr UINT ids[]{
#define LAUNCHME_MESSAGE(id, offset, type, handler) id,
#include "worker_messages.def"
#undef LAUNCHME_MESSAGE
    };
    MSG queued{};
    for (const auto id : ids)
        while (PeekMessageW(&queued, window, id, id, PM_REMOVE))
            dispatchWorkerMessage(static_cast<Receiver*>(nullptr), queued.message, queued.lParam);
}
