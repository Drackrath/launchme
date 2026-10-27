#pragma once
#include "aurelia.hpp"

namespace aurelia {
// Incremental framing for pretty JSON and NDJSON on either pipe. Chunks may split UTF-8,
// strings, escapes and ANSI sequences. Logs outside JSON are ignored; one oversized frame fails.
class JsonStream {
public:
    explicit JsonStream(size_t limit = 16 * 1024 * 1024) : limit_(limit) {}
    bool feed(std::string_view chunk, const Event& emit) {
        for (const unsigned char c : chunk) {
            if (ansi_) {
                if (ansi_ == 1) ansi_ = c == '[' ? 2 : 0;
                else if (c >= 0x40 && c <= 0x7e) ansi_ = 0;
                continue;
            }
            if (!quoted_ && c == 0x1b) { ansi_ = 1; continue; }
            if (!depth_) {
                if (c != '{' && c != '[') continue;
                frame_.assign(1, static_cast<char>(c)); depth_ = 1; continue;
            }
            if (frame_.size() == limit_) return false;
            frame_.push_back(static_cast<char>(c));
            if (quoted_) {
                if (escaped_) escaped_ = false;
                else if (c == '\\') escaped_ = true;
                else if (c == '"') quoted_ = false;
            } else if (c == '"') quoted_ = true;
            else if (c == '{' || c == '[') ++depth_;
            else if (c == '}' || c == ']') {
                if (!--depth_) {
                    if (auto value = json::parse(frame_, limit_); value && emit) emit(*value);
                    frame_.clear();
                }
            }
        }
        return true;
    }
    bool incomplete() const { return depth_ != 0; }
private:
    size_t limit_, depth_ = 0;
    int ansi_ = 0;
    bool quoted_ = false, escaped_ = false;
    std::string frame_;
};
}
