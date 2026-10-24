#pragma once
// Towarzyszka's model providers on the wire: one request body, the model's tool calls, the
// answers to them and her reply in words, for each dialect — Gemini (Foundry's proxy and Google's
// API share it), Claude (Anthropic Messages API), Ollama (/api/chat) and the Responses API (Grok,
// from xAI or Foundry's xAI proxy). companion::ask() runs the same loop over all of them. Pure:
// building and reading JSON only.
#include "companion.hpp"

namespace companion::wire {
struct Request {
    std::wstring system;
    std::vector<Turn> turns;           // Alternating, starting with the player (alternate()).
    int limit = 400;                   // Output tokens.
    float temperature = 1.0f;
    std::vector<std::string> contents; // Raw messages after the turns: the model's calls, verbatim, and their answers.
    const Tools* tools = nullptr;
    bool tools_off = false;            // The last round: she must answer in words.
    bool no_thinking = false;          // Gemini Flash: thinkingBudget 0.
};
std::string body(const Connection& connection, const Request& request);
std::optional<Calls> calls(Dialect dialect, const HttpResponse& response);
// The messages that answer `calls` (`results[i]` answers `calls.calls[i]`).
std::vector<std::string> answers(Dialect dialect, const Calls& calls, std::vector<json::Value> results);
Reply reply(Dialect dialect, const HttpResponse& response);

// The model to ask when the connection names none exactly: Claude's auto-<family> → the newest
// such model of GET /v1/models; Ollama's empty name → the first installed (/api/tags). The
// connection's own model otherwise; empty when none could be found.
std::string chooseModel(const Connection& connection, const Post& transport);
// Tool declarations (Gemini's functionDeclarations array) in each dialect's form.
std::string claudeTools(const std::string& declarations);
std::string ollamaTools(const std::string& declarations);
std::string responsesTools(const std::string& declarations);
}
