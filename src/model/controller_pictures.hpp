#pragma once
#include "devices.hpp"

namespace devices {
// Single catalogue for download eligibility, URLs and front-view crops. Full
// images use alpha bounds; 8BitDo product strips need a normalized horizontal crop.
struct ControllerPhoto {
    Picture picture;
    const wchar_t* url;
    float left = 0, right = 1;
};
inline constexpr ControllerPhoto controller_photos[] = {
    {Picture::dualsense, L"https://media.direct.playstation.com/is/image/sierialto/dualsense-hero-1?wid=800&fmt=png-alpha"},
    {Picture::ultimate2, L"https://www.8bitdo.com/images/2025/ultimate-2-wireless/ultimate-2-wireless-white.png", 0.38f, 0.62f},
    {Picture::xbox, L"https://cdn-dynmedia-1.microsoft.com/is/image/microsoftcorp/gldn-Xbox-Cntrl-White-b01-1?wid=800&fmt=png-alpha"},
    {Picture::steam_controller2, L"https://thumbnails.pcgamingwiki.com/5/59/Steam_Controller_2nd_generation.png/600px-Steam_Controller_2nd_generation.png"},
    {Picture::eightbitdo_zero2, L"https://www.8bitdo.com/images/2019/zero2/zero2-yellow.png", 425.0f / 1196, 771.0f / 1196},
};
inline constexpr const ControllerPhoto* controllerPhoto(Picture picture) {
    for (const auto& photo : controller_photos) if (photo.picture == picture) return &photo;
    return nullptr;
}
}
