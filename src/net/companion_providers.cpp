#include "companion_providers.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <mutex>

namespace companion::wire {
namespace {
std::string utf8(std::wstring_view text) { return text_encoding::utf8Lenient(text); }
std::optional<json::Value> parsed(const HttpResponse& response) {
    return json::parse(std::string_view(reinterpret_cast<const char*>(response.body.data()), response.body.size()), 8 * 1024 * 1024);
}
std::string number(double value) {
    char buffer[32];
    const int length = std::snprintf(buffer, sizeof(buffer), "%.2f", value);
    return std::string(buffer, static_cast<size_t>(std::max(0, length)));
}
// Each declaration of Gemini's functionDeclarations array.
std::vector<const json::Value*> declarationsOf(const json::Value& array) {
    std::vector<const json::Value*> result;
    if (array.type != json::Value::Type::array) return result;
    for (const auto& item : array.elements) if (item.get("name")) result.push_back(&item);
    return result;
}
// Reasoning models may write their thoughts inline: <think>…</think> is not her reply.
std::wstring withoutThoughts(std::wstring text) {
    for (auto open = text.find(L"<think>"); open != std::wstring::npos; open = text.find(L"<think>")) {
        const auto close = text.find(L"</think>", open);
        text.erase(open, close == std::wstring::npos ? std::wstring::npos : close + 8 - open);
    }
    return text;
}

std::string claudeBody(const Connection& connection, const Request& request) {
    std::string out = "{\"model\":" + json::quote(connection.model) + ",\"max_tokens\":" + std::to_string(std::clamp(request.limit, 16, 8192)) +
                      ",\"temperature\":" + number(std::clamp(request.temperature, 0.0f, 1.0f)) + ",\"system\":" + json::quote(utf8(request.system)) + ",\"messages\":[";
    bool first = true;
    for (const auto& turn : alternate(request.turns)) {
        if (!first) out += ',';
        first = false;
        out += std::string("{\"role\":\"") + (turn.user ? "user" : "assistant") + "\",\"content\":" + json::quote(utf8(turn.text)) + "}";
    }
    for (const auto& content : request.contents) { if (!first) out += ','; first = false; out += content; }
    out += "]";
    if (request.tools && !request.tools->declarations.empty())
        out += ",\"tools\":" + claudeTools(request.tools->declarations) + ",\"tool_choice\":{\"type\":\"" + (request.tools_off ? "none" : "auto") + "\"}";
    return out + "}";
}

std::string ollamaBody(const Connection& connection, const Request& request) {
    std::string out = "{\"model\":" + json::quote(connection.model) + ",\"stream\":false,\"messages\":[{\"role\":\"system\",\"content\":" + json::quote(utf8(request.system)) + "}";
    for (const auto& turn : alternate(request.turns))
        out += std::string(",{\"role\":\"") + (turn.user ? "user" : "assistant") + "\",\"content\":" + json::quote(utf8(turn.text)) + "}";
    for (const auto& content : request.contents) out += "," + content;
    // Thinking models spend part of the limit on thoughts too.
    out += "],\"options\":{\"temperature\":" + number(std::clamp(request.temperature, 0.0f, 2.0f)) + ",\"num_predict\":" + std::to_string(std::clamp(request.limit + thought_room, 16, 16384)) + "}";
    if (request.tools && !request.tools->declarations.empty() && !request.tools_off) out += ",\"tools\":" + ollamaTools(request.tools->declarations);
    return out + "}";
}

// The Responses API (Grok): instructions and turns as input items; reasoning cannot be switched off,
// so it runs at its lowest effort (a chat line, not a proof) and nothing is stored server-side.
std::string responsesBody(const Connection& connection, const Request& request) {
    std::string out = "{\"model\":" + json::quote(connection.wireModel()) + ",\"store\":false,\"reasoning\":{\"effort\":\"low\"},\"max_output_tokens\":" +
                      std::to_string(std::clamp(request.limit, 16, 16384)) + ",\"temperature\":" + number(std::clamp(request.temperature, 0.0f, 2.0f)) +
                      ",\"input\":[{\"role\":\"system\",\"content\":" + json::quote(utf8(request.system)) + "}";
    for (const auto& turn : alternate(request.turns))
        out += std::string(",{\"role\":\"") + (turn.user ? "user" : "assistant") + "\",\"content\":" + json::quote(utf8(turn.text)) + "}";
    for (const auto& content : request.contents) out += "," + content;
    out += "]";
    if (request.tools && !request.tools->declarations.empty())
        out += ",\"tools\":" + responsesTools(request.tools->declarations) + ",\"tool_choice\":\"" + (request.tools_off ? "none" : "auto") + "\"";
    return out + "}";
}
const json::Value* outputOf(const json::Value& root) {
    const auto* output = root.get("output");
    return output && output->type == json::Value::Type::array ? output : nullptr;
}
}

std::string claudeTools(const std::string& declarations) {
    const auto parsed_declarations = json::parse(declarations);
    auto tools = json::array();
    if (parsed_declarations)
        for (const auto* item : declarationsOf(*parsed_declarations)) {
            auto tool = json::object();
            tool.properties["name"] = *item->get("name");
            if (const auto* description = item->get("description")) tool.properties["description"] = *description;
            const auto* parameters = item->get("parameters");
            tool.properties["input_schema"] = parameters ? *parameters : [] { auto empty = json::object(); empty.properties["type"] = json::string("object"); return empty; }();
            tools.elements.push_back(std::move(tool));
        }
    return json::serialize(tools);
}

std::string ollamaTools(const std::string& declarations) {
    const auto parsed_declarations = json::parse(declarations);
    auto tools = json::array();
    if (parsed_declarations)
        for (const auto* item : declarationsOf(*parsed_declarations)) {
            auto tool = json::object();
            tool.properties["type"] = json::string("function");
            tool.properties["function"] = *item;
            tools.elements.push_back(std::move(tool));
        }
    return json::serialize(tools);
}

std::string responsesTools(const std::string& declarations) {
    const auto parsed_declarations = json::parse(declarations);
    auto tools = json::array();
    if (parsed_declarations)
        for (const auto* item : declarationsOf(*parsed_declarations)) {
            auto tool = *item; // Flat: name, description and parameters beside the type.
            tool.properties["type"] = json::string("function");
            tools.elements.push_back(std::move(tool));
        }
    return json::serialize(tools);
}

std::string body(const Connection& connection, const Request& request) {
    switch (connection.dialect()) {
    case Dialect::claude: return claudeBody(connection, request);
    case Dialect::ollama: return ollamaBody(connection, request);
    case Dialect::responses: return responsesBody(connection, request);
    default: {
        RequestOptions options;
        options.contents = request.contents;
        options.tools = request.tools;
        options.tools_off = request.tools_off;
        options.no_thinking = request.no_thinking;
        return requestBody(request.system, request.turns, request.limit, request.temperature, options);
    }
    }
}

std::optional<Calls> calls(Dialect dialect, const HttpResponse& response) {
    if (dialect == Dialect::gemini) return functionCalls(response);
    if (response.status != 200) return std::nullopt;
    const auto root = parsed(response);
    if (!root) return std::nullopt;
    Calls result;
    if (dialect == Dialect::responses) {
        const auto* output = outputOf(*root);
        if (!output) return std::nullopt;
        for (const auto& item : output->elements) {
            if (!item.get("type") || item.get("type")->stringOr() != "function_call") continue;
            FunctionCall call;
            if (const auto* id = item.get("call_id")) call.id = id->stringOr();
            if (const auto* name = item.get("name")) call.name = name->stringOr();
            const auto* arguments = item.get("arguments");
            call.args = arguments && arguments->type == json::Value::Type::string ? json::parse(arguments->text).value_or(json::object()) :
                        arguments && arguments->type == json::Value::Type::object ? *arguments : json::object();
            if (call.args.type != json::Value::Type::object) call.args = json::object();
            if (!call.name.empty()) result.calls.push_back(std::move(call));
        }
        if (result.calls.empty()) return std::nullopt;
        // Every output item goes back as it came (its encrypted reasoning too), before the answers.
        for (const auto& item : output->elements) {
            if (!result.content.empty()) result.content += ',';
            result.content += json::serialize(item);
        }
        return result;
    }
    if (dialect == Dialect::claude) {
        const auto* content = root->get("content");
        if (!content || content->type != json::Value::Type::array) return std::nullopt;
        for (const auto& block : content->elements) {
            if (!block.get("type") || block.get("type")->stringOr() != "tool_use") continue;
            FunctionCall call;
            if (const auto* id = block.get("id")) call.id = id->stringOr();
            if (const auto* name = block.get("name")) call.name = name->stringOr();
            const auto* input = block.get("input");
            call.args = input && input->type == json::Value::Type::object ? *input : json::object();
            if (!call.name.empty()) result.calls.push_back(std::move(call));
        }
        if (result.calls.empty()) return std::nullopt;
        // Her whole turn goes back as it came (text and tool_use blocks).
        auto turn = json::object();
        turn.properties["role"] = json::string("assistant");
        turn.properties["content"] = *content;
        result.content = json::serialize(turn);
        return result;
    }
    const auto* message = root->get("message");
    const auto* tool_calls = message ? message->get("tool_calls") : nullptr;
    if (!tool_calls || tool_calls->type != json::Value::Type::array) return std::nullopt;
    for (const auto& item : tool_calls->elements) {
        const auto* function = item.get("function");
        if (!function) continue;
        FunctionCall call;
        if (const auto* id = item.get("id")) call.id = id->stringOr();
        if (const auto* name = function->get("name")) call.name = name->stringOr();
        const auto* arguments = function->get("arguments");
        if (arguments && arguments->type == json::Value::Type::object) call.args = *arguments;
        else if (arguments && arguments->type == json::Value::Type::string) call.args = json::parse(arguments->text).value_or(json::object());
        else call.args = json::object();
        if (call.args.type != json::Value::Type::object) call.args = json::object();
        if (!call.name.empty()) result.calls.push_back(std::move(call));
    }
    if (result.calls.empty()) return std::nullopt;
    auto turn = *message;
    turn.properties["role"] = json::string("assistant");
    result.content = json::serialize(turn);
    return result;
}

std::vector<std::string> answers(Dialect dialect, const Calls& calls, std::vector<json::Value> results) {
    results.resize(calls.calls.size(), json::object());
    for (auto& result : results) if (result.type != json::Value::Type::object) result = json::object();
    if (dialect == Dialect::responses) {
        std::vector<std::string> items;
        for (size_t i = 0; i < calls.calls.size(); ++i) {
            auto item = json::object();
            item.properties["type"] = json::string("function_call_output");
            item.properties["call_id"] = json::string(calls.calls[i].id);
            item.properties["output"] = json::string(json::serialize(results[i]));
            items.push_back(json::serialize(item));
        }
        return items;
    }
    if (dialect == Dialect::claude) {
        auto blocks = json::array();
        for (size_t i = 0; i < calls.calls.size(); ++i) {
            auto block = json::object();
            block.properties["type"] = json::string("tool_result");
            block.properties["tool_use_id"] = json::string(calls.calls[i].id);
            block.properties["content"] = json::string(json::serialize(results[i]));
            blocks.elements.push_back(std::move(block));
        }
        auto message = json::object();
        message.properties["role"] = json::string("user");
        message.properties["content"] = std::move(blocks);
        return {json::serialize(message)};
    }
    if (dialect == Dialect::ollama) {
        std::vector<std::string> messages;
        for (size_t i = 0; i < calls.calls.size(); ++i) {
            auto message = json::object();
            message.properties["role"] = json::string("tool");
            message.properties["tool_name"] = json::string(calls.calls[i].name);
            if (!calls.calls[i].id.empty()) message.properties["tool_call_id"] = json::string(calls.calls[i].id);
            message.properties["content"] = json::string(json::serialize(results[i]));
            messages.push_back(json::serialize(message));
        }
        return messages;
    }
    // Gemini: one user turn of functionResponse parts, each with its call's id.
    auto parts = json::array();
    for (size_t i = 0; i < calls.calls.size(); ++i) {
        auto answer = json::object();
        if (!calls.calls[i].id.empty()) answer.properties["id"] = json::string(calls.calls[i].id);
        answer.properties["name"] = json::string(calls.calls[i].name);
        answer.properties["response"] = std::move(results[i]);
        auto part = json::object();
        part.properties["functionResponse"] = std::move(answer);
        parts.elements.push_back(std::move(part));
    }
    auto message = json::object();
    message.properties["role"] = json::string("user");
    message.properties["parts"] = std::move(parts);
    return {json::serialize(message)};
}

Reply reply(Dialect dialect, const HttpResponse& response) {
    if (dialect == Dialect::gemini) return parseResponse(response);
    if (response.status != 200) return failedReply(response);
    const auto root = parsed(response);
    if (!root) { Reply failed; failed.status = response.status; failed.error = L"invalid JSON"; return failed; }
    std::wstring text;
    std::string reason;
    if (dialect == Dialect::responses) {
        // Her words: the output_text parts of the message items (reasoning items are not hers to say).
        if (const auto* output = outputOf(*root))
            for (const auto& item : output->elements) {
                if (!item.get("type") || item.get("type")->stringOr() != "message") continue;
                if (const auto* content = item.get("content"); content && content->type == json::Value::Type::array)
                    for (const auto& part : content->elements)
                        if (part.get("type") && part.get("type")->stringOr() == "output_text" && part.get("text")) text += text_encoding::wideLenient(part.get("text")->stringOr());
            }
        if (const auto* status = root->get("status")) reason = status->stringOr();
        const auto* details = root->get("incomplete_details");
        const bool cut = reason == "incomplete" && details && details->get("reason") && details->get("reason")->stringOr() == "max_output_tokens";
        return finishReply(std::move(text), cut, cut ? std::string("max_output_tokens") : reason);
    }
    if (dialect == Dialect::claude) {
        if (const auto* stop = root->get("stop_reason")) reason = stop->stringOr();
        if (const auto* content = root->get("content"); content && content->type == json::Value::Type::array)
            for (const auto& block : content->elements)
                if (block.get("type") && block.get("type")->stringOr() == "text" && block.get("text")) text += text_encoding::wideLenient(block.get("text")->stringOr());
        return finishReply(std::move(text), reason == "max_tokens", reason);
    }
    if (const auto* done = root->get("done_reason")) reason = done->stringOr();
    if (const auto* message = root->get("message"); message && message->get("content"))
        text = withoutThoughts(text_encoding::wideLenient(message->get("content")->stringOr()));
    return finishReply(std::move(text), reason == "length", reason);
}

std::string chooseModel(const Connection& connection, const Post& transport) {
    const bool claude_auto = connection.provider == Provider::claude && connection.model.starts_with("auto-");
    const bool ollama_auto = connection.provider == Provider::ollama && connection.model.empty();
    if (!claude_auto && !ollama_auto) return connection.model;
    // One look per server, key and family while the launcher runs.
    static std::mutex mutex;
    static std::map<std::wstring, std::string> chosen;
    const auto cache = connection.host + L"\n" + connection.token + L"\n" + text_encoding::wideLenient(connection.model);
    {
        std::lock_guard lock(mutex);
        if (const auto found = chosen.find(cache); found != chosen.end()) return found->second;
    }
    std::string model;
    if (claude_auto) {
        const auto family = connection.model.substr(5);
        const auto response = transport(L"https://api.anthropic.com/v1/models?limit=100", connection.headers(), {});
        const auto root = response.status == 200 ? parsed(response) : std::nullopt;
        const auto* data = root ? root->get("data") : nullptr;
        // Newest first (the API's order); release dates break ties when given.
        std::vector<std::pair<std::string, std::string>> found;
        if (data && data->type == json::Value::Type::array)
            for (const auto& item : data->elements) {
                const auto id = item.get("id") ? item.get("id")->stringOr() : std::string{};
                if (id.find(family) != std::string::npos) found.emplace_back(item.get("created_at") ? item.get("created_at")->stringOr() : std::string{}, id);
            }
        std::stable_sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        if (!found.empty()) model = found.front().second;
    } else {
        const auto response = transport(connection.host + L"api/tags", connection.headers(), {});
        const auto root = response.status == 200 ? parsed(response) : std::nullopt;
        const auto* installed = root ? root->get("models") : nullptr;
        if (installed && installed->type == json::Value::Type::array)
            for (const auto& item : installed->elements)
                if (const auto* name = item.get("name"); name && !name->stringOr().empty()) { model = name->stringOr(); break; }
    }
    if (!model.empty()) {
        std::lock_guard lock(mutex);
        chosen[cache] = model;
    }
    return model;
}
}
