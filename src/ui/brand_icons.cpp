#include "brand_icons.hpp"

#include <wrl/client.h>

#include <cctype>
#include <cmath>

namespace brand_icons {
namespace {

// Simple Icons (CC0): steam, gogdotcom, epicgames, battledotnet, ea, ubisoft, playstation, commodore, retroarch;
// wii and wiiu are the two halves of its former wiiu logo (simple-icons 5.24), each framed on its own; gamecube is
// Wikimedia Commons' GC_Logo.svg, its three faces filled with one colour.
constexpr Icon icons[] = {
    {"steam", "M11.979 0C5.678 0 .511 4.86.022 11.037l6.432 2.658c.545-.371 1.203-.59 1.912-.59.063 0 .125.004.188.006l2.861-4.142V8.91c0-2.495 2.028-4.524 4.524-4.524 2.494 0 4.524 2.031 4.524 4.527s-2.03 4.525-4.524 4.525h-.105l-4.076 2.911c0 .052.004.105.004.159 0 1.875-1.515 3.396-3.39 3.396-1.635 0-3.016-1.173-3.331-2.727L.436 15.27C1.862 20.307 6.486 24 11.979 24c6.627 0 11.999-5.373 11.999-12S18.605 0 11.979 0zM7.54 18.21l-1.473-.61c.262.543.714.999 1.314 1.25 1.297.539 2.793-.076 3.332-1.375.263-.63.264-1.319.005-1.949s-.75-1.121-1.377-1.383c-.624-.26-1.29-.249-1.878-.03l1.523.63c.956.4 1.409 1.5 1.009 2.455-.397.957-1.497 1.41-2.454 1.012H7.54zm11.415-9.303c0-1.662-1.353-3.015-3.015-3.015-1.665 0-3.015 1.353-3.015 3.015 0 1.665 1.35 3.015 3.015 3.015 1.663 0 3.015-1.35 3.015-3.015zm-5.273-.005c0-1.252 1.013-2.266 2.265-2.266 1.249 0 2.266 1.014 2.266 2.266 0 1.251-1.017 2.265-2.266 2.265-1.253 0-2.265-1.014-2.265-2.265z"},
    {"gog", "M7.15 15.24H4.36a.4.4 0 0 0-.4.4v2c0 .21.18.4.4.4h2.8v1.32h-3.5c-.56 0-1.02-.46-1.02-1.03v-3.39c0-.56.46-1.02 1.03-1.02h3.48v1.32zM8.16 11.54c0 .58-.47 1.05-1.05 1.05H2.63v-1.35h3.78a.4.4 0 0 0 .4-.4V6.39a.4.4 0 0 0-.4-.4H4.39a.4.4 0 0 0-.41.4v2.02c0 .23.18.4.4.4H6v1.35H3.68c-.58 0-1.05-.46-1.05-1.04V5.68c0-.57.47-1.04 1.05-1.04H7.1c.58 0 1.05.47 1.05 1.04v5.86zM21.36 19.36h-1.32v-4.12h-.93a.4.4 0 0 0-.4.4v3.72h-1.33v-4.12h-.93a.4.4 0 0 0-.4.4v3.72h-1.33v-4.42c0-.56.46-1.02 1.03-1.02h5.61v5.44zM21.37 11.54c0 .58-.47 1.05-1.05 1.05h-4.48v-1.35h3.78a.4.4 0 0 0 .4-.4V6.39a.4.4 0 0 0-.4-.4h-2.03a.4.4 0 0 0-.4.4v2.02c0 .23.18.4.4.4h1.62v1.35H16.9c-.58 0-1.05-.46-1.05-1.04V5.68c0-.57.47-1.04 1.05-1.04h3.43c.58 0 1.05.47 1.05 1.04v5.86zM13.72 4.64h-3.44c-.58 0-1.04.47-1.04 1.04v3.44c0 .58.46 1.04 1.04 1.04h3.44c.57 0 1.04-.46 1.04-1.04V5.68c0-.57-.47-1.04-1.04-1.04m-.3 1.75v2.02a.4.4 0 0 1-.4.4h-2.03a.4.4 0 0 1-.4-.4V6.4c0-.22.17-.4.4-.4H13c.23 0 .4.18.4.4zM12.63 13.92H9.24c-.57 0-1.03.46-1.03 1.02v3.39c0 .57.46 1.03 1.03 1.03h3.39c.57 0 1.03-.46 1.03-1.03v-3.39c0-.56-.46-1.02-1.03-1.02m-.3 1.72v2a.4.4 0 0 1-.4.4v-.01H9.94a.4.4 0 0 1-.4-.4v-1.99c0-.22.18-.4.4-.4h2c.22 0 .4.18.4.4zM23.49 1.1a1.74 1.74 0 0 0-1.24-.52H1.75A1.74 1.74 0 0 0 0 2.33v19.34a1.74 1.74 0 0 0 1.75 1.75h20.5A1.74 1.74 0 0 0 24 21.67V2.33c0-.48-.2-.92-.51-1.24m0 20.58a1.23 1.23 0 0 1-1.24 1.24H1.75A1.23 1.23 0 0 1 .5 21.67V2.33a1.23 1.23 0 0 1 1.24-1.24h20.5a1.24 1.24 0 0 1 1.24 1.24v19.34z"},
    {"epic", "M3.537 0C2.165 0 1.66.506 1.66 1.879V18.44a4.262 4.262 0 00.02.433c.031.3.037.59.316.92.027.033.311.245.311.245.153.075.258.13.43.2l8.335 3.491c.433.199.614.276.928.27h.002c.314.006.495-.071.928-.27l8.335-3.492c.172-.07.277-.124.43-.2 0 0 .284-.211.311-.243.28-.33.285-.621.316-.92a4.261 4.261 0 00.02-.434V1.879c0-1.373-.506-1.88-1.878-1.88zm13.366 3.11h.68c1.138 0 1.688.553 1.688 1.696v1.88h-1.374v-1.8c0-.369-.17-.54-.523-.54h-.235c-.367 0-.537.17-.537.539v5.81c0 .369.17.54.537.54h.262c.353 0 .523-.171.523-.54V8.619h1.373v2.143c0 1.144-.562 1.71-1.7 1.71h-.694c-1.138 0-1.7-.566-1.7-1.71V4.82c0-1.144.562-1.709 1.7-1.709zm-12.186.08h3.114v1.274H6.117v2.603h1.648v1.275H6.117v2.774h1.74v1.275h-3.14zm3.816 0h2.198c1.138 0 1.7.564 1.7 1.708v2.445c0 1.144-.562 1.71-1.7 1.71h-.799v3.338h-1.4zm4.53 0h1.4v9.201h-1.4zm-3.13 1.235v3.392h.575c.354 0 .523-.171.523-.54V4.965c0-.368-.17-.54-.523-.54zm-3.74 10.147a1.708 1.708 0 01.591.108 1.745 1.745 0 01.49.299l-.452.546a1.247 1.247 0 00-.308-.195.91.91 0 00-.363-.068.658.658 0 00-.28.06.703.703 0 00-.224.163.783.783 0 00-.151.243.799.799 0 00-.056.299v.008a.852.852 0 00.056.31.7.7 0 00.157.245.736.736 0 00.238.16.774.774 0 00.303.058.79.79 0 00.445-.116v-.339h-.548v-.565H7.37v1.255a2.019 2.019 0 01-.524.307 1.789 1.789 0 01-.683.123 1.642 1.642 0 01-.602-.107 1.46 1.46 0 01-.478-.3 1.371 1.371 0 01-.318-.455 1.438 1.438 0 01-.115-.58v-.008a1.426 1.426 0 01.113-.57 1.449 1.449 0 01.312-.46 1.418 1.418 0 01.474-.309 1.58 1.58 0 01.598-.111 1.708 1.708 0 01.045 0zm11.963.008a2.006 2.006 0 01.612.094 1.61 1.61 0 01.507.277l-.386.546a1.562 1.562 0 00-.39-.205 1.178 1.178 0 00-.388-.07.347.347 0 00-.208.052.154.154 0 00-.07.127v.008a.158.158 0 00.022.084.198.198 0 00.076.066.831.831 0 00.147.06c.062.02.14.04.236.061a3.389 3.389 0 01.43.122 1.292 1.292 0 01.328.17.678.678 0 01.207.24.739.739 0 01.071.337v.008a.865.865 0 01-.081.382.82.82 0 01-.229.285 1.032 1.032 0 01-.353.18 1.606 1.606 0 01-.46.061 2.16 2.16 0 01-.71-.116 1.718 1.718 0 01-.593-.346l.43-.514c.277.223.578.335.9.335a.457.457 0 00.236-.05.157.157 0 00.082-.142v-.008a.15.15 0 00-.02-.077.204.204 0 00-.073-.066.753.753 0 00-.143-.062 2.45 2.45 0 00-.233-.062 5.036 5.036 0 01-.413-.113 1.26 1.26 0 01-.331-.16.72.72 0 01-.222-.243.73.73 0 01-.082-.36v-.008a.863.863 0 01.074-.359.794.794 0 01.214-.283 1.007 1.007 0 01.34-.185 1.423 1.423 0 01.448-.066 2.006 2.006 0 01.025 0zm-9.358.025h.742l1.183 2.81h-.825l-.203-.499H8.623l-.198.498h-.81zm2.197.02h.814l.663 1.08.663-1.08h.814v2.79h-.766v-1.602l-.711 1.091h-.016l-.707-1.083v1.593h-.754zm3.469 0h2.235v.658h-1.473v.422h1.334v.61h-1.334v.442h1.493v.658h-2.255zm-5.3.897l-.315.793h.624zm-1.145 5.19h8.014l-4.09 1.348z"},
    {"battlenet", "M18.94 8.296C15.9 6.892 11.534 6 7.426 6.332c.206-1.36.714-2.308 1.548-2.508 1.148-.275 2.4.48 3.594 1.854.782.102 1.71.28 2.355.429C12.747 2.013 9.828-.282 7.607.565c-1.688.644-2.553 2.97-2.448 6.094-2.2.468-3.915 1.3-5.013 2.495-.056.065-.181.227-.137.305.034.058.146-.008.194-.04 1.274-.89 2.904-1.373 5.027-1.676.303 3.333 1.713 7.56 4.055 10.952-1.28.502-2.356.536-2.946-.087-.812-.856-.784-2.318-.19-4.04a26.764 26.764 0 0 1-.807-2.254c-2.459 3.934-2.986 7.61-1.143 9.11 1.402 1.14 3.847.725 6.502-.926 1.505 1.672 3.083 2.74 4.667 3.094.084.015.287.043.332-.034.034-.06-.08-.124-.131-.149-1.408-.657-2.64-1.828-3.964-3.515 2.735-1.929 5.691-5.263 7.457-8.988 1.076.86 1.64 1.773 1.398 2.595-.336 1.131-1.615 1.84-3.403 2.185a27.697 27.697 0 0 1-1.548 1.826c4.634.16 8.08-1.22 8.458-3.565.286-1.786-1.295-3.696-4.053-5.17.696-2.139.832-4.04.346-5.588-.029-.08-.106-.27-.196-.27-.068 0-.067.13-.063.187.135 1.547-.263 3.2-1.062 5.19zm-8.533 9.869c-1.96-3.145-3.09-6.849-3.082-10.594 3.702-.124 7.474.748 10.714 2.627-1.743 3.269-4.385 6.1-7.633 7.966h.001z"},
    {"ea", "M16.635 6.162l-5.928 9.377H4.24l1.508-2.3h4.024l1.474-2.335H2.264L.79 13.239h2.156L0 17.84h12.072l4.563-7.259 1.652 2.66h-1.401l-1.473 2.299h4.347l1.473 2.3H24zm-11.461.107L3.7 8.604l9.52-.035 1.474-2.3z"},
    {"ubisoft", "M23.561 11.988C23.301-.304 6.954-4.89.656 6.634c.282.206.661.477.943.672a11.747 11.747 0 00-.976 3.067 11.885 11.885 0 00-.184 2.071C.439 18.818 5.621 24 12.005 24c6.385 0 11.556-5.17 11.556-11.556v-.455zm-20.27 2.06c-.152 1.246-.054 1.636-.054 1.788l-.282.098c-.108-.206-.37-.932-.488-1.908C2.163 10.308 4.7 6.96 8.57 6.33c3.544-.52 6.937 1.68 7.728 4.758l-.282.098c-.087-.087-.228-.336-.77-.878-4.281-4.281-11.002-2.32-11.956 3.74zm11.002 2.081a3.145 3.145 0 01-2.59 1.355 3.15 3.15 0 01-3.155-3.155 3.159 3.159 0 012.927-3.144c1.018-.043 1.972.51 2.416 1.398a2.58 2.58 0 01-.455 2.95c.293.205.575.4.856.595zm6.58.12c-1.669 3.782-5.106 5.766-8.77 5.712-7.034-.347-9.083-8.466-4.38-11.393l.207.206c-.076.108-.358.325-.791 1.182-.51 1.041-.672 2.081-.607 2.732.369 5.67 8.314 6.83 11.045 1.214C21.057 8.217 11.822.401 3.626 6.374l-.184-.184C5.599 2.808 9.816 1.3 13.837 2.309c6.147 1.55 9.453 7.956 7.035 13.94z"},
    {"playstation", "M8.984 2.596v17.547l3.915 1.261V6.688c0-.69.304-1.151.794-.991.636.18.76.814.76 1.505v5.875c2.441 1.193 4.362-.002 4.362-3.152 0-3.237-1.126-4.675-4.438-5.827-1.307-.448-3.728-1.186-5.39-1.502zm4.656 16.241l6.296-2.275c.715-.258.826-.625.246-.818-.586-.192-1.637-.139-2.357.123l-4.205 1.5V14.98l.24-.085s1.201-.42 2.913-.615c1.696-.18 3.785.03 5.437.661 1.848.601 2.04 1.472 1.576 2.072-.465.6-1.622 1.036-1.622 1.036l-8.544 3.107V18.86zM1.807 18.6c-1.9-.545-2.214-1.668-1.352-2.32.801-.586 2.16-1.052 2.16-1.052l5.615-2.013v2.313L4.205 17c-.705.271-.825.632-.239.826.586.195 1.637.15 2.343-.12L8.247 17v2.074c-.12.03-.256.044-.39.073-1.939.331-3.996.196-6.038-.479z"},
    {"commodore", "M11.202.798C5.016.798 0 5.814 0 12s5.016 11.202 11.202 11.202c1.094 0 2.153-.157 3.154-.45v-5.335a6.27 6.27 0 1 1 0-10.839v-5.33c-1-.293-2.057-.45-3.154-.45Zm3.375 6.343v4.304h5.27L24 7.14Zm-.037 5.377v4.304h9.423l-4.156-4.304z"},
    {"retroarch", "M6.84 5.76L8.4 7.68H5.28l-.72 2.88H2.64l.72-2.88H1.44L0 13.44h3.84l-.48 1.92h3.36L4.2 18.24h2.82l2.34-2.88h5.28l2.34 2.88h2.82l-2.52-2.88h3.36l-.48-1.92H24l-1.44-5.76h-1.92l.72 2.88h-1.92l-.72-2.88H15.6l1.56-1.92h-2.04l-1.68 1.92h-2.88L8.88 5.76zm.24 3.84H9v1.92H7.08zm7.925 0h1.92v1.92h-1.92Z"},
    {"wii", "M11.133 8.432c-.465 0-.835.359-.835.814.007.454.381.817.835.812.488 0 .873-.358.873-.81 0-.455-.385-.816-.873-.816zm2.93 0c-.465 0-.848.359-.848.814 0 .442.383.812.848.812.477 0 .861-.358.861-.81 0-.455-.385-.816-.861-.816zM4.833 8.77c-.465 0-.776.232-.938.756-.152.533-1.116 4.242-1.116 4.242l-1.267-4.94H0s1.451 5.264 1.65 5.881c.15.476.521.86 1.058.86.627 0 .917-.454 1.045-.86.14-.421 1.08-3.895 1.08-3.895s.942 3.476 1.069 3.895c.14.406.431.86 1.047.86.547 0 .906-.385 1.07-.86.196-.617 1.65-5.881 1.65-5.881H8.148l-1.258 4.94s-.963-3.709-1.125-4.242c-.15-.526-.479-.756-.93-.756h-.002zm5.605 2.09v4.662h1.441V10.86h-1.441zm2.916 0v4.662h1.442V10.86h-1.442z", false, -0.238f, 4.277f, 15.4f},
    {"wiiu", "M17.718 8.443c-.53 0-.99.335-.99.858v3.315c0 .809.56 1.289 1.377 1.289h4.647c.689 0 1.248-.477 1.248-1.162V9.345c0-.412-.308-.86-.688-.86h-1.075v2.799c0 2.122-3.701 2.104-3.701.04v-2.88h-.818zm1.679 0v2.453c0 1.636 1.934 1.402 1.934.256V8.445h-1.934v-.002z", false, 16.464f, 7.274f, 7.8f},
    {"gamecube", "m 274.90625,281.03128 0,128.75 0.0312,0.0312 111.5,64.375 0,-44.28125 -39.0625,-22.5625 -34.09375,-19.6875 0,-39.375 0,-0.0312 -0.0312,-45.0625 -38.3437,-22.1562 z m 58.46875,34.03125 0,61.25 53.0625,30.65625 0,-61.28125 z M 386.40625,216.65625 l -111.5,64.375 38.34375,22.15625 39.0625,-22.5625 34.125,-19.6875 34.09375,19.6875 0.0312,0.0312 14.125,8.125 38.3125,-22.125 -86.5625,-50 -0.0312,0 z m 0,67.84375 -53.03125,30.625 53.0625,30.625 0,-0.0625 52.875,-30.65625 L 386.40625,284.5 z m -53.03125,91.875 0,0.0312 53.03125,30.625 L 333.375,376.375 z M 497.9375,281.03125 l -111.5,64.65625 0,0.5625 0,60.71875 53.0625,-30.65625 0,-16.4375 20.09375,-11.59375 0,39.375 -73.15625,42.25 0,44.28125 111.5,-64.375 z", false, 255.42f, 214.42f, 262.0f},
    // Drawn here: the four panes; a ring around an X; the two Joy-Con.
    {"windows", "M1 1h10.4v10.4H1zM12.6 1H23v10.4H12.6zM1 12.6h10.4V23H1zM12.6 12.6H23V23H12.6z"},
    {"xbox", "M12 1a11 11 0 1 0 0 22a11 11 0 1 0 0-22zm0 2.2a8.8 8.8 0 1 1 0 17.6a8.8 8.8 0 1 1 0-17.6zM6.4 8 8 6.4l4 4 4-4L17.6 8l-4 4 4 4-1.6 1.6-4-4-4 4L6.4 16l4-4z", true},
    // Amazon Games, drawn here (Simple Icons does not carry Amazon): a smile ending in an arrowhead,
    // under a rounded controller-like bar.
    {"amazon", "M5 4.5h14a3.5 3.5 0 0 1 0 7H5a3.5 3.5 0 0 1 0-7zm0 2a1.5 1.5 0 0 0 0 3h14a1.5 1.5 0 0 0 0-3zM1.6 13.9c6.3 4.7 14.4 5.1 20 1l1.1 1.6c-6.4 4.7-15.6 4.2-22.3-.9zM18.7 12.3l5 .8-.9 5-1.9-.3.5-3-3-.5z"}, // Non-zero: the bar's inner outline runs the other way.
    // A grass block in three faces, the top with its drip edge: Minecraft (MultiMC / Prism).
    {"minecraft", "M12 1.6 22.4 6.8 12 12 1.6 6.8zM1.4 8.6 11.1 13.5V22.4L1.4 17.5zM22.6 8.6V17.5L12.9 22.4V13.5zM1.4 8.6l9.7 4.9v2.6l-2.4-1.2v1.4L6.3 15.1v-1.4L3.8 12.5v1.6L1.4 12.9z", true},
    {"switch", "M10.6 1H6.8A5.3 5.3 0 0 0 1.5 6.3v11.4A5.3 5.3 0 0 0 6.8 23h3.8zM8.6 3v18H6.8a3.3 3.3 0 0 1-3.3-3.3V6.3A3.3 3.3 0 0 1 6.8 3zM6.4 5.6a1.9 1.9 0 1 0 0 3.8a1.9 1.9 0 1 0 0-3.8zM13.4 1h3.8a5.3 5.3 0 0 1 5.3 5.3v11.4a5.3 5.3 0 0 1-5.3 5.3h-3.8zM17.6 12a2.1 2.1 0 1 0 0 4.2a2.1 2.1 0 1 0 0-4.2z", true},
};

// SVG path data → a Direct2D figure list: M L H V C S Q T A Z, absolute and relative, with implicit
// repeats and the compact number and flag forms of minified paths.
class Parser {
public:
    explicit Parser(const char* text) : p_(text) {}
    void skip() { while (*p_ == ' ' || *p_ == ',' || *p_ == '\n' || *p_ == '\t' || *p_ == '\r') ++p_; }
    bool end() { skip(); return !*p_; }
    bool command(char& c) {
        skip();
        if (!std::isalpha(static_cast<unsigned char>(*p_))) return false;
        c = *p_++;
        return true;
    }
    bool number(float& value) {
        skip();
        const char* s = p_;
        double sign = 1;
        if (*s == '+' || *s == '-') { if (*s == '-') sign = -1; ++s; }
        double whole = 0;
        bool digits = false;
        while (std::isdigit(static_cast<unsigned char>(*s))) { whole = whole * 10 + (*s - '0'); ++s; digits = true; }
        if (*s == '.') {
            ++s;
            double scale = 0.1;
            while (std::isdigit(static_cast<unsigned char>(*s))) { whole += (*s - '0') * scale; scale *= 0.1; ++s; digits = true; }
        }
        if (!digits) return false;
        if (*s == 'e' || *s == 'E') {
            const char* e = s + 1;
            int esign = 1, exponent = 0;
            if (*e == '+' || *e == '-') { if (*e == '-') esign = -1; ++e; }
            if (std::isdigit(static_cast<unsigned char>(*e))) {
                while (std::isdigit(static_cast<unsigned char>(*e))) { exponent = exponent * 10 + (*e - '0'); ++e; }
                whole *= std::pow(10.0, esign * exponent);
                s = e;
            }
        }
        value = static_cast<float>(sign * whole);
        p_ = s;
        return true;
    }
    bool flag(bool& value) {
        skip();
        if (*p_ != '0' && *p_ != '1') return false;
        value = *p_++ == '1';
        return true;
    }
    bool startsNumber() {
        skip();
        return std::isdigit(static_cast<unsigned char>(*p_)) || *p_ == '-' || *p_ == '+' || *p_ == '.';
    }

private:
    const char* p_;
};

} // namespace

std::string_view sourceKey(GameSource source) {
    switch (source) {
    case GameSource::steam: return "steam";
    case GameSource::gog: return "gog";
    case GameSource::epic: return "epic";
    case GameSource::battlenet: return "battlenet";
    case GameSource::ea: return "ea";
    case GameSource::ubisoft: return "ubisoft";
    case GameSource::xbox: return "xbox";
    case GameSource::drmfree: return "windows"; // Plain Windows games.
    case GameSource::multimc: return "minecraft";
    case GameSource::amazon: return "amazon";
    default: return {};
    }
}

std::string_view consoleKey(std::wstring_view console) {
    if (console.starts_with(L"PS")) return "playstation"; // PS1–PS5 and PSP.
    if (console == L"Switch") return "switch";
    if (console == L"Wii") return "wii";   // The Wii U logo's Wii...
    if (console == L"Wii U") return "wiiu"; // ...and its U.
    if (console == L"GameCube" || console == L"Wii / GameCube") return "gamecube";
    if (console == L"Amiga") return "commodore";
    if (console == L"Retro") return "retroarch";
    if (console == L"PC") return "windows";
    return {};
}

const Icon* find(std::string_view key) {
    for (const auto& icon : icons) if (icon.key == key) return &icon;
    return nullptr;
}

namespace {
// launchme's logo (res/launchme_logo.svg, viewBox 0 0 1084 256): the portal symbol, then the eight letters of the wordmark.
constexpr const char* logo_path =
    "M28 228 V128 A100 100 0 0 1 228 128 V228 H172 V128 A44 44 0 0 0 84 128 V172 H128 V228 Z"
    "M288 183.38V58H308.53V183.38Z"
    "M354.43 185.94Q344.4 185.94 337.49 182.15Q330.59 178.37 327.06 172.05Q323.53 165.73 323.53 158.14Q323.53 151.46 325.75 146.16Q327.98 140.86 332.59 136.98"
    "Q337.2 133.09 344.48 130.59Q350.08 128.75 357.51 127.34Q364.94 125.92 373.7 124.68Q382.45 123.44 392.05 122L384.66 126.12Q384.73 116.58 380.4 112.06"
    "Q376.07 107.53 365.72 107.53Q359.52 107.53 353.73 110.45Q347.94 113.37 345.62 120.41L326.83 114.52Q330.25 102.87 339.87 95.79Q349.49 88.71 365.78 88.71"
    "Q378.07 88.71 387.39 92.7Q396.7 96.7 401.26 105.85Q403.73 110.68 404.25 115.76Q404.76 120.83 404.76 126.86V183.38H386.73V163.4L389.72 166.69"
    "Q383.49 176.63 375.17 181.29Q366.84 185.94 354.43 185.94ZM358.5 169.54Q365.49 169.54 370.43 167.06Q375.36 164.58 378.32 161.03Q381.27 157.48 382.27 154.27"
    "Q383.87 150.35 384.12 145.28Q384.37 140.21 384.37 137.04L390.68 138.95Q381.33 140.44 374.67 141.52Q368.01 142.6 363.26 143.57Q358.51 144.55 354.83 145.77"
    "Q351.29 147.08 348.81 148.76Q346.33 150.45 345.01 152.68Q343.69 154.91 343.69 157.88Q343.69 161.25 345.37 163.89Q347.05 166.52 350.35 168.03"
    "Q353.64 169.54 358.5 169.54Z"
    "M455.98 186Q445.79 186 439.13 182.62Q432.47 179.23 428.54 173.92Q424.62 168.61 422.74 162.64Q420.87 156.66 420.31 151.32Q419.76 145.98 419.76 142.7"
    "V91.27H440.57V135.65Q440.57 139.91 441.19 145.21Q441.81 150.5 443.95 155.41Q446.1 160.33 450.41 163.48Q454.71 166.64 462.09 166.64Q465.99 166.64 469.82 165.37"
    "Q473.65 164.09 476.75 161Q479.85 157.91 481.73 152.43Q483.61 146.95 483.61 138.6L495.81 143.84Q495.81 155.56 491.25 165.14Q486.69 174.71 477.83 180.36"
    "Q468.97 186 455.98 186ZM486.12 183.38V154.81H483.61V91.27H504.28V183.38Z"
    "M584.14 183.38V139Q584.14 134.72 583.52 129.43Q582.91 124.15 580.76 119.23Q578.61 114.32 574.3 111.17Q570 108.01 562.62 108.01Q558.72 108.01 554.89 109.28"
    "Q551.06 110.56 547.96 113.65Q544.86 116.74 542.98 122.18Q541.1 127.63 541.1 136.05L528.9 130.81Q528.9 119.09 533.46 109.51Q538.03 99.94 546.88 94.29"
    "Q555.74 88.65 568.73 88.65Q578.92 88.65 585.58 92.03Q592.25 95.42 596.17 100.73Q600.09 106.04 601.97 112.01Q603.85 117.99 604.4 123.33Q604.95 128.67 604.95 131.95"
    "V183.38ZM520.28 183.38V91.27H538.59V119.84H541.1V183.38Z"
    "M663.08 185.94Q648.85 185.94 638.77 179.59Q628.69 173.23 623.34 162.24Q618 151.24 617.95 137.32Q618 123.21 623.53 112.22Q629.06 101.23 639.21 94.97"
    "Q649.36 88.71 663.36 88.71Q679.07 88.71 689.94 96.58Q700.82 104.46 704.07 118.1L683.6 123.71Q681.26 116.26 675.7 112.11Q670.14 107.95 663.08 107.95"
    "Q655.03 107.95 649.85 111.78Q644.68 115.6 642.21 122.24Q639.73 128.88 639.7 137.32Q639.75 150.47 645.63 158.58Q651.5 166.7 663.08 166.7Q671.31 166.7 676.03 162.93"
    "Q680.75 159.16 683.15 152.11L704.07 156.74Q699.85 170.82 689.19 178.38Q678.53 185.94 663.08 185.94Z"
    "M781.93 183.38V139Q781.93 134.72 781.31 129.43Q780.7 124.15 778.55 119.23Q776.4 114.32 772.09 111.17Q767.79 108.01 760.41 108.01Q756.51 108.01 752.68 109.28"
    "Q748.85 110.56 745.75 113.65Q742.65 116.74 740.77 122.18Q738.89 127.63 738.89 136.05L726.69 130.81Q726.69 119.09 731.25 109.51Q735.82 99.94 744.67 94.29"
    "Q753.53 88.65 766.52 88.65Q776.71 88.65 783.37 92.03Q790.03 95.42 793.96 100.73Q797.88 106.04 799.76 112.01Q801.63 117.99 802.19 123.33Q802.74 128.67 802.74 131.95"
    "V183.38ZM718.07 183.38V60.56H736.38V125.24H738.89V183.38Z"
    "M929.51 183.38 929.54 127.46Q929.54 117.93 924.83 112.6Q920.12 107.27 912.14 107.27Q907.18 107.27 903.26 109.57Q899.35 111.86 897.04 116.22Q894.74 120.58 894.74 126.69"
    "L885.59 121.29Q885.52 111.71 889.91 104.41Q894.3 97.12 901.79 93.06Q909.27 88.99 918.48 88.99Q933.81 88.99 941.94 98.14Q950.07 107.29 950.07 122.26L950.04 183.38Z"
    "M818.74 183.38V91.27H836.91V119.84H839.41V183.38ZM874.21 183.38 874.24 127.66Q874.24 118.06 869.55 112.67Q864.86 107.27 856.79 107.27Q848.95 107.27 844.18 112.65"
    "Q839.41 118.03 839.41 126.69L830.26 120.55Q830.26 111.54 834.69 104.41Q839.13 97.28 846.66 93.13Q854.2 88.99 863.61 88.99Q873.93 88.99 880.86 93.38"
    "Q887.79 97.76 891.27 105.31Q894.74 112.86 894.74 122.34L894.71 183.38Z"
    "M1010.04 185.94Q996.06 185.94 985.5 179.88Q974.94 173.82 969.01 163.08Q963.07 152.34 963.07 138.32Q963.07 123.22 968.89 112.09Q974.71 100.96 985.03 94.83"
    "Q995.34 88.71 1008.9 88.71Q1023.19 88.71 1033.21 95.39Q1043.23 102.07 1048.12 114.27Q1053 126.46 1051.79 143.07H1031.39V135.56Q1031.31 120.52 1026.1 113.6"
    "Q1020.9 106.67 1009.75 106.67Q997.15 106.67 990.98 114.49Q984.82 122.31 984.82 137.32Q984.82 151.34 990.98 159.02Q997.15 166.7 1008.9 166.7Q1016.49 166.7 1021.98 163.31"
    "Q1027.47 159.93 1030.45 153.56L1050.78 159.67Q1045.51 172.15 1034.44 179.05Q1023.37 185.94 1010.04 185.94ZM978.34 143.07V127.52H1041.74V143.07Z";

// Path data → a filled Direct2D path geometry in the path's own units.
HRESULT parse(ID2D1Factory* factory, const char* path, bool even_odd, Microsoft::WRL::ComPtr<ID2D1PathGeometry>& geometry) {
    using Microsoft::WRL::ComPtr;
    ComPtr<ID2D1GeometrySink> sink;
    if (const auto hr = factory->CreatePathGeometry(geometry.GetAddressOf()); FAILED(hr)) return hr;
    if (const auto hr = geometry->Open(sink.GetAddressOf()); FAILED(hr)) return hr;
    sink->SetFillMode(even_odd ? D2D1_FILL_MODE_ALTERNATE : D2D1_FILL_MODE_WINDING);
    Parser parser(path);
    D2D1_POINT_2F current{0, 0}, start{0, 0}, control{0, 0};
    char command = 0, previous = 0;
    bool open = false, valid = true;
    const auto begin = [&] { if (!open) { sink->BeginFigure(current, D2D1_FIGURE_BEGIN_FILLED); start = current; open = true; } };
    while (valid && !parser.end()) {
        if (char c; parser.command(c)) command = c;
        else if (!command || !parser.startsNumber()) { valid = false; break; }
        const bool relative = std::islower(static_cast<unsigned char>(command)) != 0;
        const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(command)));
        const auto point = [&](float x, float y) { return relative ? D2D1::Point2F(current.x + x, current.y + y) : D2D1::Point2F(x, y); };
        float a = 0, b = 0, c = 0, d = 0, e = 0, f = 0;
        switch (upper) {
        case 'M':
            if (!parser.number(a) || !parser.number(b)) { valid = false; break; }
            if (open) sink->EndFigure(D2D1_FIGURE_END_OPEN);
            open = false;
            current = point(a, b);
            begin();
            command = relative ? 'l' : 'L'; // Further pairs are lines.
            break;
        case 'L':
            if (!parser.number(a) || !parser.number(b)) { valid = false; break; }
            begin(); current = point(a, b); sink->AddLine(current);
            break;
        case 'H':
            if (!parser.number(a)) { valid = false; break; }
            begin(); current.x = relative ? current.x + a : a; sink->AddLine(current);
            break;
        case 'V':
            if (!parser.number(a)) { valid = false; break; }
            begin(); current.y = relative ? current.y + a : a; sink->AddLine(current);
            break;
        case 'C': {
            if (!parser.number(a) || !parser.number(b) || !parser.number(c) || !parser.number(d) || !parser.number(e) || !parser.number(f)) { valid = false; break; }
            begin();
            const auto p1 = point(a, b), p2 = point(c, d), p3 = point(e, f);
            sink->AddBezier(D2D1::BezierSegment(p1, p2, p3));
            control = p2; current = p3;
            break;
        }
        case 'S': {
            if (!parser.number(c) || !parser.number(d) || !parser.number(e) || !parser.number(f)) { valid = false; break; }
            begin();
            const char last = static_cast<char>(std::toupper(static_cast<unsigned char>(previous)));
            const auto p1 = last == 'C' || last == 'S' ? D2D1::Point2F(2 * current.x - control.x, 2 * current.y - control.y) : current;
            const auto p2 = point(c, d), p3 = point(e, f);
            sink->AddBezier(D2D1::BezierSegment(p1, p2, p3));
            control = p2; current = p3;
            break;
        }
        case 'Q': {
            if (!parser.number(a) || !parser.number(b) || !parser.number(c) || !parser.number(d)) { valid = false; break; }
            begin();
            const auto p1 = point(a, b), p2 = point(c, d);
            sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(p1, p2));
            control = p1; current = p2;
            break;
        }
        case 'T': {
            if (!parser.number(c) || !parser.number(d)) { valid = false; break; }
            begin();
            const char last = static_cast<char>(std::toupper(static_cast<unsigned char>(previous)));
            const auto p1 = last == 'Q' || last == 'T' ? D2D1::Point2F(2 * current.x - control.x, 2 * current.y - control.y) : current;
            const auto p2 = point(c, d);
            sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(p1, p2));
            control = p1; current = p2;
            break;
        }
        case 'A': {
            bool large = false, sweep = false;
            if (!parser.number(a) || !parser.number(b) || !parser.number(c) || !parser.flag(large) || !parser.flag(sweep) ||
                !parser.number(e) || !parser.number(f)) { valid = false; break; }
            begin();
            const auto target = point(e, f);
            if (a == 0 || b == 0) sink->AddLine(target); // A zero radius is a straight line in SVG.
            else sink->AddArc(D2D1::ArcSegment(target, D2D1::SizeF(std::abs(a), std::abs(b)), c,
                                               sweep ? D2D1_SWEEP_DIRECTION_CLOCKWISE : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
                                               large ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
            current = target;
            break;
        }
        case 'Z':
            if (open) sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            open = false;
            current = start;
            break;
        default: valid = false; break;
        }
        previous = upper == 'M' ? 'M' : command;
        if (upper == 'Z') command = 0; // A number after Z is not a repeat.
    }
    if (open) sink->EndFigure(D2D1_FIGURE_END_OPEN);
    const auto closed = sink->Close();
    if (!valid) return E_INVALIDARG;
    return closed;
}
} // namespace

HRESULT build(ID2D1Factory* factory, const Icon& icon, ID2D1Geometry** result) {
    using Microsoft::WRL::ComPtr;
    if (!factory || !result) return E_POINTER;
    *result = nullptr;
    ComPtr<ID2D1PathGeometry> geometry;
    if (const auto hr = parse(factory, icon.path, icon.even_odd, geometry); FAILED(hr)) return hr;
    if (icon.left == 0 && icon.top == 0 && icon.size == 24) { *result = geometry.Detach(); return S_OK; }
    // Its own box scaled to the whole 24×24 square.
    ComPtr<ID2D1TransformedGeometry> framed;
    const float scale = 24.0f / icon.size;
    if (const auto hr = factory->CreateTransformedGeometry(geometry.Get(), D2D1::Matrix3x2F::Translation(-icon.left, -icon.top) * D2D1::Matrix3x2F::Scale(scale, scale),
                                                           framed.GetAddressOf()); FAILED(hr)) return hr;
    *result = framed.Detach();
    return S_OK;
}

HRESULT buildLogo(ID2D1Factory* factory, ID2D1Geometry** result) {
    using Microsoft::WRL::ComPtr;
    if (!factory || !result) return E_POINTER;
    *result = nullptr;
    ComPtr<ID2D1PathGeometry> geometry;
    if (const auto hr = parse(factory, logo_path, false, geometry); FAILED(hr)) return hr; // Non-zero, as the SVG.
    *result = geometry.Detach();
    return S_OK;
}

HRESULT buildLogoMark(ID2D1Factory* factory, ID2D1Geometry** result) {
    using Microsoft::WRL::ComPtr;
    if (!factory || !result) return E_POINTER;
    *result = nullptr;
    // The first sub-path of logo_path: the portal.
    constexpr const char* mark = "M28 228 V128 A100 100 0 0 1 228 128 V228 H172 V128 A44 44 0 0 0 84 128 V172 H128 V228 Z";
    ComPtr<ID2D1PathGeometry> geometry;
    if (const auto hr = parse(factory, mark, false, geometry); FAILED(hr)) return hr;
    *result = geometry.Detach();
    return S_OK;
}

} // namespace brand_icons
