#include "launcher.hpp"

std::shared_ptr<companion::semantic::Service> Launcher::companionSemantic(bool start) {
    if (!settings_.companion || !settings_.companion_semantic_memory || !settings_.companion_memory || !settings_.companion_notes) {
        if (companion_.semantic) companion_.semantic->stop();
        companion_.semantic.reset();
        return {};
    }
    if (!companion_.semantic) {
        if (!start || !companion_.persist) return {};
#ifdef LAUNCHME_UI_TEST
        return {}; // UI cases explicitly inject their own service; never load a real model by accident.
#else
        wchar_t exe[32768]{};
        if (!GetModuleFileNameW(nullptr, exe, 32768)) return {};
        const auto directory = std::filesystem::path(exe).parent_path() / L"assets/companion-embeddings";
        companion_.semantic = std::make_shared<companion::semantic::Service>(userConfigDirectory() / L"companion_vectors.dat",
            [directory] { return companion::semantic::localEmbedder(directory); });
#endif
    }
    companion_.semantic->replace(companion::semantic::documents(companionMemory(), companionCognition(), companionMindState()));
    return companion_.semantic;
}
