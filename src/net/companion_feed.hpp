#pragma once
// Launchfeed: immutable public posts, separate from private chat photos and memory. Each
// character owns her archive; SHA-256 of the original picture is its permanent publication ID.
#include "companion.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace companion::feed {
inline constexpr int daily_limit = 2;
inline constexpr uint64_t publication_gap = 4 * 3600;
struct Comment {
    std::wstring author, text;
    std::string look; // Empty: a local visitor; otherwise a companion key.
    uint64_t at = 0;
    bool player = false;
};
struct Social {
    uint64_t revision = 0;
    bool liked = false, like_considered = false, comment_considered = false, seeded = false;
    bool reaction_pending = false, reaction_done = false, reaction_private = false, private_claimed = false;
    std::wstring reaction_context, private_text;
    std::vector<Comment> comments;
};
struct Post {
    std::string id;
    uint64_t at = 0; // Local seconds, like chats::Conversation. The actual publication time.
    int day = 0;
    std::wstring description, text; // Source TXT and the LLM's finished public caption.
    Social social;
    uint32_t width = 0, height = 0;
};
struct State { std::vector<Post> posts; }; // Newest first. Never pruned: also the used-image ledger.
struct Draft {
    std::string id;
    std::vector<uint8_t> bytes;
    std::wstring description;
};
std::filesystem::path folderOf(const std::filesystem::path& config, std::string_view look);
std::string fingerprint(std::span<const uint8_t> bytes);
// A missing archive is empty; unreadable/malformed records fail closed instead of forgetting usage.
std::optional<State> load(const std::filesystem::path& folder);
bool due(const State& state, int day, uint64_t now);
// Only <source>/<look>/*.png,jpg,... paired with a nonempty UTF-8 <stem>.txt. No exhausted-pool fallback.
std::optional<Draft> pick(const std::filesystem::path& source, std::string_view look, const State& state, uint32_t seed);
// Reloads the archive under an exclusive file lock; commits media first, then its immutable
// record. Failure consumes neither the picture nor a daily slot. Returns the updated archive.
std::optional<State> publish(const std::filesystem::path& folder, const Draft& draft, std::wstring text, int day, uint64_t now);
std::optional<std::vector<uint8_t>> image(const std::filesystem::path& folder, std::string_view id);
std::pair<uint32_t, uint32_t> dimensions(std::span<const uint8_t> bytes);
std::wstring generationPrompt(Persona persona, Look look, bool english, std::wstring_view public_context);
std::wstring generationRequest(std::wstring_view description, bool english);
std::wstring reference(const Post& post, bool english);
std::wstring context(const State& state, std::wstring_view query, bool english);
// Mutable social sidecars, written atomically under the same exclusive lock as publication.
// A missing sidecar is empty; malformed data is never silently overwritten.
std::optional<Social> loadSocial(const std::filesystem::path& folder, std::string_view id);
std::optional<Social> setLike(const std::filesystem::path& folder, std::string_view id, bool liked, uint32_t roll);
std::optional<Social> addComment(const std::filesystem::path& folder, std::string_view id, std::wstring text, uint64_t now, uint32_t roll);
std::optional<Social> seedComments(const std::filesystem::path& folder, std::string_view id, std::vector<Comment> comments);
std::optional<Social> completeReaction(const std::filesystem::path& folder, std::string_view id, std::wstring_view expected_context,
                                     std::wstring text, std::wstring author, std::string look, uint64_t now);
std::optional<Social> claimPrivate(const std::filesystem::path& folder, std::string_view id);
struct CommentAuthor { std::wstring name, style; std::string look; };
std::wstring commentsPrompt(const Post& post, const std::vector<CommentAuthor>& authors, bool english);
std::vector<Comment> parseComments(std::wstring_view raw, const std::vector<CommentAuthor>& authors, uint64_t now);
std::wstring reactionPrompt(const Post& post, bool english);
}
