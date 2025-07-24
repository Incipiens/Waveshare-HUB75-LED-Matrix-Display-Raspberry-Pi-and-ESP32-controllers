#include "icons_weather.h"
#include <unordered_map>
#include <algorithm>
#include <cstring>


// Encoded icons:
// To avoid a huge static byte dump, we encode each 16x16 icon as 16 strings,
// each 16 chars wide. Each character maps to an entry in a palette of 24-bit
// RGB colors. On InitIcons(), we expand into Icon16 pixel buffers.
//
// This keeps the source human-readable and allows easy editing of icons.

namespace {

struct RGB { uint8_t r,g,b; };
struct EncodedIcon {
    const char *name;
    const char **rows; // 2d array, 16 strings of length 16
    const RGB *palette;
    const char *legend; // characters in same order as palette entries
};

// Reusable colors (choose dim-ish values to avoid overpowering display at low gamma)
// Some of the G and B values are swapped to match my Waveshare panel's hardware mapping, though not all.
static constexpr RGB BLACK {0,0,0};
static constexpr RGB WHITE {255,255,255};
static constexpr RGB YELLOW {255,0,200};
static constexpr RGB ORANGE {255,120,0};
static constexpr RGB BLUE {0,120,255};
static constexpr RGB LIGHTBL {120,180,255};
static constexpr RGB GRAY {90,90,90};
static constexpr RGB LGREY {140,140,140};
static constexpr RGB GREEN {0,210,0};
static constexpr RGB CYAN {0,200,200};
static constexpr RGB PURPLE {180,0,180};


// Legend maps characters -> palette entries
// '.' becomes BLACK, 'O' becomes YELLOW (outer), ORANGE shading painted later.
// "clear" (sun)"
static const RGB PAL_CLEAR[] = { BLACK, YELLOW, ORANGE };
static const char *SUN_ROWS[16] = {
"......OO......",
".....OOOO.....",
".....OOOO.....",
"..O..OOOO..O..",
".OOO.OOOO.OOO.",
".OOOOOOOOOOOO.",
".OOOOOOOOOOOO.",
"..OOOOOOOOOO..",
"..OOOOOOOOOO..",
".OOOOOOOOOOOO.",
".OOOOOOOOOOOO.",
".OOO.OOOO.OOO.",
"..O..OOOO..O..",
".....OOOO.....",
".....OOOO.....",
"......OO......"
};


// "cloudy" (cloud)
static const RGB PAL_CLOUD[] = { BLACK, LGREY, WHITE };
static const char *CLOUD_ROWS[16] = {
"................",
"................",
".....11.........",
"...1112111......",
"..112222211.....",
".11222222211....",
".112222222211...",
".112222222211...",
"..1122222211....",
"...11122111.....",
".....1111.......",
"................",
"................",
"................",
"................",
"................"
};

// "rain" (cloud + drops)
static const RGB PAL_RAIN[] = { BLACK, LGREY, WHITE, BLUE };
static const char *RAIN_ROWS[16] = {
"................",
"................",
".....11.........",
"...1112111......",
"..112222211.....",
".11222222211....",
".112222222211...",
".112222222211...",
"..1122222211....",
"...11122111.....",
"..3..3..3..3....",
"...3..3..3......",
"....3.....3.....",
"...3..3..3......",
"................",
"................"
};

// "snow" (cloud + snowflakes)
static const RGB PAL_SNOW[] = { BLACK, LGREY, WHITE, CYAN };
static const char *SNOW_ROWS[16] = {
"................",
"................",
".....11.........",
"...1112111......",
"..112222211.....",
".11222222211....",
".112222222211...",
".112222222211...",
"..1122222211....",
"...11122111.....",
"..3..3..3..3....",
"...3..3..3......",
"..3..3..3..3....",
"...3..3..3......",
"................",
"................"
};

// "thunder" (cloud + thunderbolt)
static const RGB PAL_THUNDER[] = { BLACK, LGREY, WHITE, YELLOW, ORANGE };
static const char *THUNDER_ROWS[16] = {
"................",
"................",
".....11.........",
"...1112111......",
"..112222211.....",
".11222222211....",
".112222222211...",
".112222222211...",
"..1122222211....",
"...11122111.....",
"...44...........",
"..4444..........",
"...4444.........",
"....44..........",
"................",
"................"
}; 

// "unknown" (question mark)
static const RGB PAL_UNKNOWN[] = { BLACK, PURPLE, WHITE };
static const char *UNKNOWN_ROWS[16] = {
"................",
"..11111.........",
".11...11........",
".11...11........",
".....111........",
"....11..........",
"...11...........",
"...11...........",
"...11...........",
"................",
"...11...........",
"...11...........",
"................",
"................",
"................",
"................"
};

// "play" icon, left over from testing
static const RGB PAL_PLAY[] = { BLACK, GREEN };
static const char *PLAY_ROWS[16] = {
"................",
"................",
"...1............",
"...11...........",
"...111..........",
"...1111.........",
"...11111........",
"...111111.......",
"...11111........",
"...1111.........",
"...111..........",
"...11...........",
"...1............",
"................",
"................",
"................"
};

// "pause" icon, left over from testing
static const RGB PAL_PAUSE[] = { BLACK, CYAN };
static const char *PAUSE_ROWS[16] = {
"................",
"................",
"..11....11......",
"..11....11......",
"..11....11......",
"..11....11......",
"..11....11......",
"..11....11......",
"..11....11......",
"..11....11......",
"..11....11......",
"..11....11......",
"................",
"................",
"................",
"................"
};

static EncodedIcon ENCODED[] = {
    { "clear", SUN_ROWS, PAL_CLEAR, ".O" },
    { "sunny", SUN_ROWS, PAL_CLEAR, ".O" },  // alias
    { "cloudy", CLOUD_ROWS, PAL_CLOUD, ".12" },
    { "rain", RAIN_ROWS, PAL_RAIN, ".12 3" }, // technically unsafe to use a space to skip when mapping icons to colors
    { "snow", SNOW_ROWS, PAL_SNOW, ".12 3" },
    { "thunder", THUNDER_ROWS, PAL_THUNDER, ".12 4" },
    { "unknown", UNKNOWN_ROWS, PAL_UNKNOWN, ".1" },
    { "play", PLAY_ROWS, PAL_PLAY, ".1" },
    { "pause", PAUSE_ROWS, PAL_PAUSE, ".1" },
};

// Storing decoded pixel buffers
// TODO: make guarded
static std::unordered_map<std::string, Icon16> gIcons;
static bool gInitialized = false;

// Map character to RGB color using icon's palette legend.
// If symbol not found, returns same as '.' (background color).
static RGB LookupColor(char c, const RGB *palette, const char *legend) {
    if (c == '.') return palette[0]; // fast path for background, minimal resource usage
    const char *p = legend;
    int idx = 0;
    while (*p) {
        if (*p == c) return palette[idx];
        ++p; ++idx;
    }
    return {0,0,0};
}

// post-processing (shading) for specific icons
// Only adjusts g channel, should probably be expanded to work across whole rgb space
static void PostProcess(const std::string &name, Icon16 &icon) {
    if (name == "clear" || name == "sunny") {
        // Add orange rim to differentiate the two by modifying outer ring pixels, detects yellow near boundary
        for (int y=0; y<16; ++y) {
            for (int x=0; x<16; ++x) {
                int idx = (y*16 + x)*3;
                uint8_t &r = icon.pixels[idx], &g = icon.pixels[idx+1], &b = icon.pixels[idx+2];
                if (r > 200 && g > 150 && b < 50) {
                    // If near edge ( measuring distance from center > ~5.5) we tint orange
                    float dx = x - 7.5f;
                    float dy = y - 7.5f;
                    float dist = dx*dx + dy*dy;
                    if (dist > 30.f) { // outer region
                        g = (uint8_t)(g * 0.6f);
                    }
                }
            }
        }
    } else if (name == "thunder") {
        // slight bit of orange at the bottom of the bolt by checking for any yellow pixel with y > 9
        for (int y=10; y<16; ++y) {
            for (int x=0; x<16; ++x) {
                int idx = (y*16 + x)*3;
                uint8_t &r = icon.pixels[idx], &g = icon.pixels[idx+1], &b = icon.pixels[idx+2];
                if (r > 200 && g > 150 && b < 40) {
                    g = (uint8_t)(g * 0.7f);
                }
            }
        }
    }
}

} 

// declaring namespace

bool InitIcons() {
    if (gInitialized) return true;

    for (const auto &e : ENCODED) {
        Icon16 icon{};
        int outIdx = 0;
        for (int row=0; row<16; ++row) {
            const char *line = e.rows[row];
            for (int col=0; col<16; ++col) {
                char sym = line[col];
                RGB color = LookupColor(sym, e.palette, e.legend);
                icon.pixels[outIdx++] = color.r;
                icon.pixels[outIdx++] = color.g;
                icon.pixels[outIdx++] = color.b;
            }
        }
        PostProcess(e.name, icon);
        gIcons.emplace(e.name, icon);
    }

    // Provide mapping to existing icon buffers
    // todo: add actual aliasing support rather than just copying
    if (gIcons.find("clear") != gIcons.end() && gIcons.find("sunny") == gIcons.end()) {
        gIcons["sunny"] = gIcons["clear"];
    }
    if (gIcons.find("unknown") == gIcons.end()) {
        // guarantee unknown exists
        gIcons["unknown"] = Icon16{};
    }

    gInitialized = true;
    return true;
}

const Icon16* GetIconByName(const std::string &name) {
    auto it = gIcons.find(name);
    if (it != gIcons.end()) return &it->second;
    return nullptr;
}

static void BlitIcon(const Icon16 *icon, rgb_matrix::Canvas *canvas, int x, int y) {
    if (!icon) return;
    for (int j=0; j<16; ++j) {
        int yy = y + j;
        if (yy < 0 || yy >= canvas->height()) continue;
        for (int i=0; i<16; ++i) {
            int xx = x + i;
            if (xx < 0 || xx >= canvas->width()) continue;
            int idx = (j*16 + i)*3;
            canvas->SetPixel(xx, yy,
                             icon->pixels[idx],
                             icon->pixels[idx+1],
                             icon->pixels[idx+2]);
        }
    }
}

void DrawWeatherIcon(const std::string &condition,
                     rgb_matrix::Canvas *canvas,
                     int x, int y) {
    if (!gInitialized) InitIcons();

    // Normalize condition to a small set.
    std::string key = condition;
    std::transform(key.begin(), key.end(), key.begin(), ::tolower);

    // Mapping Home Assistant names
    if (key == "clear-night") key = "clear";
    else if (key == "partlycloudy" || key == "partly-cloudy" || key == "partly-cloudy-day" ||
             key == "partlycloudy-day") key = "cloudy"; // (create a separate partly icon later)
    else if (key == "lightning" || key == "lightning-rainy" || key == "thunderstorm") key = "thunder";
    else if (key == "rainy" || key == "pouring" || key == "showers") key = "rain";
    else if (key == "snowy" || key == "snowy-rainy" || key == "hail" || key == "snow") key = "snow";

    const Icon16 *icon = GetIconByName(key);
    if (!icon) icon = GetIconByName("unknown");
    BlitIcon(icon, canvas, x, y);
}

// Generic icons reuse same storage (play, pause)
void DrawPlayIcon(rgb_matrix::Canvas *canvas, int x, int y) {
    if (!gInitialized) InitIcons();
    BlitIcon(GetIconByName("play"), canvas, x, y);
}
void DrawPauseIcon(rgb_matrix::Canvas *canvas, int x, int y) {
    if (!gInitialized) InitIcons();
    BlitIcon(GetIconByName("pause"), canvas, x, y);
}
