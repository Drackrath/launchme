// Her embedded pictures (cmake/CompanionResources.cmake, src/platform/embedded_resources.hpp): every
// character the manifest lists has her basic outfit's calm face under companion_ui::spriteId, each
// costume's optional pose under poseId, and the phone page under 65000; unknown numbers are none.
#include "../src/net/companion_characters.hpp"
#include "../src/platform/embedded_resources.hpp"

#include <iostream>
#include <string_view>

#define CHECK(value) do { if (!(value)) { std::cerr << "companion resources check failed at line " << __LINE__ << ": " << #value << '\n'; return __LINE__; } } while (false)

namespace {
// companion_ui::spriteId and poseId (src/ui/companion_view.hpp, not built on Linux yet).
int spriteId(companion::Look look, int frame, companion::Emotion emotion, int costume) {
    return 2000 + ((static_cast<int>(look) * 32 + costume) * 3 + frame) * 32 + static_cast<int>(emotion);
}
int poseId(companion::Look look, int costume) { return 64000 + static_cast<int>(look) * 32 + costume; }
bool png(std::span<const uint8_t> bytes) { return bytes.size() > 8 && std::string_view(reinterpret_cast<const char*>(bytes.data()), 4) == "\x89PNG"; }
}

int main() {
    const auto& characters = companion::characters();
    CHECK(!characters.empty());
    size_t pictures = 0;
    for (size_t i = 0; i < characters.size(); ++i) {
        const auto look = static_cast<companion::Look>(i);
        if (characters[i].soon) continue; // A card only: her pictures may not exist yet.
        const auto calm = embedded_resources::find(spriteId(look, 0, companion::calm, 0));
        CHECK(calm && png(*calm));
        for (const auto& costume : characters[i].costumes) {
            for (int emotion = 0; emotion < companion::emotion_count; ++emotion)
                for (int frame = 0; frame < 3; ++frame)
                    if (const auto sprite = embedded_resources::find(spriteId(look, frame, static_cast<companion::Emotion>(emotion), costume.slot))) {
                        CHECK(png(*sprite));
                        ++pictures;
                    }
            if (const auto pose = embedded_resources::find(poseId(look, costume.slot))) { CHECK(png(*pose)); ++pictures; }
        }
    }
    CHECK(pictures > characters.size());
    const auto page = embedded_resources::find(65000);
    CHECK(page && std::string_view(reinterpret_cast<const char*>(page->data()), page->size()).find("<html") != std::string_view::npos);
    CHECK(!embedded_resources::find(1999) && !embedded_resources::find(65001) && !embedded_resources::find(-1));
    std::cout << "Companion resources: " << pictures << " embedded pictures and the phone page passed\n";
    return 0;
}
