#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct SteamFriend {
    uint64_t id = 0;
    std::wstring name;
    std::wstring game;
    std::wstring avatar_url;
    int status = 0; // 0=offline, >0=online; game nonempty=in-game.
};

struct FriendsResult {
    std::vector<SteamFriend> friends;
    std::wstring error;
};

FriendsResult fetchSteamFriends(uint64_t steam_id);
std::vector<uint8_t> fetchFriendAvatar(const SteamFriend& friend_info,
                                        const std::filesystem::path& cache);
