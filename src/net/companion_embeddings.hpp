#pragma once
#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace companion::semantic {
inline constexpr size_t dimensions = 384;
using Vector = std::array<float, dimensions>;
inline constexpr float similarity_floor = 0.30f; // MiniLM cosine, not a probability; weak semantic-only matches abstain.
// Injected in unit tests; production uses the pinned multilingual MiniLM model, never a network endpoint.
struct Embedder {
    virtual ~Embedder() = default;
    virtual std::string identity() const = 0;
    virtual Vector encode(std::wstring_view text, bool query) = 0;
};
bool normalize(Vector& vector);
float cosine(const Vector& a, const Vector& b);
std::string contentKey(std::wstring_view text);
std::unique_ptr<Embedder> localEmbedder(const std::filesystem::path& model_directory);
// Exposed for tokenizer regression checks against the original XLM-R tokenizer.
std::vector<int64_t> localTokens(const std::filesystem::path& model_directory, std::wstring_view text, bool query);
}
