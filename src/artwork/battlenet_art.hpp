#pragma once
#include "steamgriddb_search.hpp"

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

// Battle.net games' official art, from the public catalog the Battle.net client itself reads (no key,
// no account): versions → build config → root (its fragments) → each fragment's products (program_id,
// key_art, logo) and files (a reference's content hash). Files live at
// https://us.cdn.blizzard.com/tpr/catalogs/data/<h0h1>/<h2h3>/<md5>. Key art is a 3:4 JPEG (600×800),
// the logo a transparent PNG; there is no wide art. Only unconditional fragments (no "requires": the
// regional/CN/old-client variants) are read.
struct BattlenetArt { std::string tall, logo; }; // Full https URLs; empty when the catalog has none.

std::string battlenetBuildConfig(std::string_view versions); // The newest PUB-N row's BuildConfig.
std::string battlenetRoot(std::string_view build_config);      // Its "root = <md5>".
std::vector<std::pair<std::string, std::string>> battlenetFragments(std::string_view root); // (name, md5), unconditional only.
std::string battlenetDataUrl(std::string_view md5);
// Program ID (lowercase, as the launcher's Battle.net game keys) → its art, from the fragments' JSON
// (name, text) in any order. A program ID several fragments claim is kept only from the fragment of
// its own name (Call of Duty's AUKS); D1 is the catalog's DRTL.
std::map<std::string, BattlenetArt> battlenetCatalogArt(const std::vector<std::pair<std::string, std::string>>& fragments);

// The art URL of a launch code ("Fen", "WoW", …), the tall key art or the logo. The whole catalog is
// read once and kept for 30 days in <cache>\..\artwork\battlenet-art.v1 (a failure: asked again in an
// hour). Runs on an artwork worker.
std::string battlenetArtUrl(const std::string& code, bool logo, const std::filesystem::path& cache, const ArtworkHttp& get = httpGet);
