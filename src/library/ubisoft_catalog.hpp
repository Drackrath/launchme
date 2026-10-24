#pragma once
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

// Network half of the Ubisoft library (the reader in ubisoft.hpp stays offline). Titles of
// owned products come from unifiDB (https://github.com/mubaraknumann/unifiDB): a weekly
// export of Ubisoft Connect's public product index (UUID -> title) and the community list
// iArtorias/ubisoft_game_ids (product ID -> title). Only titles are taken; nothing is sent.
namespace ubisoft {
// Stores titles of {product ID, UUID} pairs in <directory>/names.tsv: the UUID catalogue
// first, the ID list as a fallback. Blocking: call from a worker. Returns true when at least
// one new title was stored. Products absent from both catalogues are not requested again for
// 24 h; a failed download is retried after 15 minutes.
bool refreshNames(const std::filesystem::path& directory, const std::vector<std::pair<std::string, std::string>>& products);
}
