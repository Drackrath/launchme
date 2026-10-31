#include "companion_embeddings.hpp"
#include "../platform/hashing.hpp"
#include "../platform/text_encoding.hpp"
#include "../platform/updater.hpp"
#include <onnxruntime_cxx_api.h>
#include <sentencepiece_processor.h>
#ifdef _WIN32
#include <Windows.h>
#include <bcrypt.h>
#endif
#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace companion::semantic {
namespace {
constexpr const char* model_sha = "66fc00f5f29afcaff34092e1bdd20008ca3918265a82fb9695a551e510cc4ebc";
constexpr const char* tokenizer_sha = "cfc8146abe2a0488e9e2a0c56de7952f7c11ab059eca145a0a727afce0db2865";
constexpr size_t token_limit = 128;
// The file has this digest. Checked once a run while its size and write time stay: the model is
// loaded again whenever the semantic memory needs it after an idle unload (companion_semantic.cpp),
// and hashing its 100+ MB each time would cost more than the load.
bool verified(const std::filesystem::path& path, std::string_view sha) {
    static std::mutex mutex;
    static std::map<std::filesystem::path, std::pair<uintmax_t, std::filesystem::file_time_type>> checked;
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) return false;
    const auto time = std::filesystem::last_write_time(path, ec);
    if (ec) return false;
    {
        std::lock_guard lock(mutex);
        if (const auto found = checked.find(path); found != checked.end() && found->second == std::pair{size, time}) return true;
    }
    if (updater::sha256File(path) != sha) return false;
    std::lock_guard lock(mutex);
    checked[path] = {size, time};
    return true;
}
void loadTokenizer(sentencepiece::SentencePieceProcessor& sp, const std::filesystem::path& directory) {
    const auto path = directory / L"sentencepiece.bpe.model";
    if (!verified(path, tokenizer_sha)) throw std::runtime_error("Embedding tokenizer is missing or has an unexpected digest.");
    std::ifstream input(path, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(input)), {});
    if (!sp.LoadFromSerializedProto(bytes).ok()) throw std::runtime_error("Cannot load the embedding tokenizer.");
}
std::vector<int64_t> tokenize(const sentencepiece::SentencePieceProcessor& sp, std::wstring_view text, bool /*query*/) {
    // XLMRobertaTokenizer: fairseq <s>=0, </s>=2, <unk>=3; ordinary SentencePiece ids + 1.
    std::vector<int> pieces;
    const auto input = text_encoding::utf8Lenient(std::wstring(text.substr(0, 4096)));
    if (!sp.Encode(input, &pieces).ok()) throw std::runtime_error("Embedding tokenization failed.");
    std::vector<int64_t> ids{0};
    for (const auto id : pieces) {
        if (ids.size() >= token_limit - 1) break;
        ids.push_back(id == sp.unk_id() ? 3 : static_cast<int64_t>(id) + 1);
    }
    ids.push_back(2);
    return ids;
}
class Local final : public Embedder {
    Ort::Env environment_{ORT_LOGGING_LEVEL_ERROR, "launchme-embeddings"};
    Ort::Session session_{nullptr};
    sentencepiece::SentencePieceProcessor tokenizer_;
public:
    explicit Local(const std::filesystem::path& directory) {
        environment_.DisableTelemetryEvents();
        const auto path = directory / L"model.onnx";
        if (!verified(path, model_sha)) throw std::runtime_error("Embedding model is missing or has an unexpected digest.");
        loadTokenizer(tokenizer_, directory);
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(2);
        options.SetInterOpNumThreads(1);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        // One short sentence at a time: no arena kept at its peak between them (plain allocations).
        options.DisableCpuMemArena();
        session_ = Ort::Session(environment_, path.c_str(), options);
    }
    std::string identity() const override { return std::string("multilingual-minilm-l12/mean-l2/128/xlmr-v1/") + model_sha + "/" + tokenizer_sha; }
    Vector encode(std::wstring_view text, bool query) override {
        auto ids = tokenize(tokenizer_, text, query);
        std::vector<int64_t> mask(ids.size(), 1), types(ids.size(), 0);
        const std::array<int64_t, 2> shape{1, static_cast<int64_t>(ids.size())};
        const auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        std::array<Ort::Value, 3> values{
            Ort::Value::CreateTensor<int64_t>(memory, ids.data(), ids.size(), shape.data(), shape.size()),
            Ort::Value::CreateTensor<int64_t>(memory, mask.data(), mask.size(), shape.data(), shape.size()),
            Ort::Value::CreateTensor<int64_t>(memory, types.data(), types.size(), shape.data(), shape.size())};
        const char* names[]{"input_ids", "attention_mask", "token_type_ids"};
        const char* outputs[]{"last_hidden_state"};
        auto result = session_.Run(Ort::RunOptions{nullptr}, names, values.data(), values.size(), outputs, 1);
        const auto actual = result[0].GetTensorTypeAndShapeInfo().GetShape();
        if (actual != std::vector<int64_t>{1, shape[1], static_cast<int64_t>(dimensions)})
            throw std::runtime_error("Unexpected embedding tensor shape.");
        const auto* data = result[0].GetTensorData<float>();
        Vector vector{};
        for (size_t token = 0; token < ids.size(); ++token)
            for (size_t d = 0; d < dimensions; ++d) vector[d] += data[token * dimensions + d] / static_cast<float>(ids.size());
        if (!normalize(vector)) throw std::runtime_error("Invalid embedding vector.");
        return vector;
    }
};
}
bool normalize(Vector& vector) {
    double squared = 0;
    for (const auto value : vector) { if (!std::isfinite(value)) return false; squared += static_cast<double>(value) * value; }
    if (squared <= 1e-20 || !std::isfinite(squared)) return false;
    const auto norm = std::sqrt(squared);
    for (auto& value : vector) value = static_cast<float>(value / norm);
    return true;
}
float cosine(const Vector& a, const Vector& b) {
    double dot = 0;
    for (size_t i = 0; i < dimensions; ++i) dot += static_cast<double>(a[i]) * b[i];
    return static_cast<float>(std::clamp(dot, -1.0, 1.0));
}
std::string contentKey(std::wstring_view text) {
    const auto bytes = text_encoding::utf8Lenient(std::wstring(text));
    auto out = hashing::bytesHex(hashing::Algorithm::sha256, std::span(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()));
    if (out.empty()) throw std::runtime_error("Cannot hash embedding content.");
    return out;
}
std::unique_ptr<Embedder> localEmbedder(const std::filesystem::path& directory) { return std::make_unique<Local>(directory); }
std::vector<int64_t> localTokens(const std::filesystem::path& directory, std::wstring_view text, bool query) {
    sentencepiece::SentencePieceProcessor sp;
    loadTokenizer(sp, directory);
    return tokenize(sp, text, query);
}
}
