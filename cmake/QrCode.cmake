# QR codes (Aurelia's Steam sign-in): Project Nayuki's QR Code generator, MIT, pinned to v1.8.0.
# Only its C++ encoder is built; nothing is downloaded at run time.
include(FetchContent)
FetchContent_Declare(qrcodegen GIT_REPOSITORY https://github.com/nayuki/QR-Code-generator.git
    GIT_TAG 720f62bddb7226106071d4728c292cb1df519ceb)
FetchContent_MakeAvailable(qrcodegen) # No CMakeLists of its own: only fetched.
add_library(launchme_qrcodegen STATIC "${qrcodegen_SOURCE_DIR}/cpp/qrcodegen.cpp")
target_compile_features(launchme_qrcodegen PRIVATE cxx_std_17)
target_include_directories(launchme_qrcodegen SYSTEM PUBLIC "${qrcodegen_SOURCE_DIR}/cpp")
