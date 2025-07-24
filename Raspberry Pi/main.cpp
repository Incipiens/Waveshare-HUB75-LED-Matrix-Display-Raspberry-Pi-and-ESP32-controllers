#include <unistd.h>
#include <signal.h>
#include <mosquitto.h>
#include <rpi-rgb-led-matrix/include/led-matrix.h>
#include <rpi-rgb-led-matrix/include/graphics.h>
#include <mutex>
#include <atomic>
#include <string>
#include <iostream>
#include <cstdint>
#include "icons_weather.h"
#include "shared_state.h"

using namespace rgb_matrix;

// Global state and signals, interrupt is not atomic
static SharedState gState;
static volatile bool interrupt_received = false;
static void InterruptHandler(int) { interrupt_received = true; }

// Measure text width
static int TextWidth(const rgb_matrix::Font &font, const std::string &txt) {
  int width = 0;
  for (size_t i = 0; i < txt.size(); ++i) {
    // NOTE: This treats each byte as a glyph. Real UTF-8 needs decoding, and multi-byte characters will give wrong widths. Only works for ASCII
    const uint8_t codepoint = static_cast<uint8_t>(txt[i]);
    width += font.CharacterWidth(codepoint);
  }
  return width;
}

struct ScrollInfo {
  std::string text;
  int width = 0; // Total cycle width: = textW if it fits, else textW + GAP
  int pos   = 0; // X-pos left‑edge in pixels
};

static ScrollInfo trackScroll;
static ScrollInfo artistScroll;

// MQTT message handler
void on_message(struct mosquitto *, void *, const struct mosquitto_message *msg) {
  std::string topic(msg->topic);
  std::string payload(reinterpret_cast<char*>(msg->payload), msg->payloadlen);

  std::lock_guard<std::mutex> lk(gState.m);
  if (topic == "matrix/weather/cond")           gState.weatherCond = payload;
  else if (topic == "matrix/weather/temp")       gState.weatherTemp = payload;
  else if (topic == "matrix/weather/summary")    gState.weatherSummary = payload;
  else if (topic == "matrix/spotify/track")      gState.track = payload;
  else if (topic == "matrix/spotify/artist")     gState.artist = payload;
  else if (topic == "matrix/control/brightness") {
    // Brightness is a number 0->100, but we clamp it to 5->100. This code is unsafe if the payload is not a valid number or is too large of a number.
    int b = std::stoi(payload);
    if (b < 5)   b = 5;
    if (b > 100) b = 100;
    gState.brightness = b;
  }
  gState.dirty = true; // force redraw
}

// main
int main(int argc, char **argv) {
  // Signals
  signal(SIGTERM, InterruptHandler);
  signal(SIGINT,  InterruptHandler);

  // Defining matrix values
  RGBMatrix::Options matrix_options;
  RuntimeOptions     runtime;
  matrix_options.rows                 = 32;
  matrix_options.cols                 = 64;
  matrix_options.chain_length         = 1;
  matrix_options.parallel             = 1;
  matrix_options.hardware_mapping     = "regular";
  matrix_options.limit_refresh_rate_hz= 120;
  runtime.drop_privileges             = 1;

  // Supports chaining displays
  const int DISPLAY_WIDTH = matrix_options.cols * matrix_options.chain_length;

  RGBMatrix *matrix = CreateMatrixFromOptions(matrix_options, runtime);
  InitIcons();
  if (matrix == nullptr) return 1;

  // Font
  rgb_matrix::Font font;
  if (!font.LoadFont("rpi-rgb-led-matrix/fonts/6x13.bdf")) {
    std::cerr << "Font load failed\n";
    return 1;
  }

  // NOTE: Some Waveshare displays swap the G and B channels in hardware, so data interpreted as blue is actually green, and vice versa. 
  // This is a workaround to fix that as it affects my panel. If yours is not affected, you can swap the G and B values in the Color constructor below.
  Color white(255,255,255), green(0,0,255), cyan(0,255,255), yellow(255,0,255);

  // MQTT, right now a failure to connect will not stop the program, but it will not receive any updates.
  mosquitto_lib_init();
  mosquitto *m = mosquitto_new("matrix-display", true, nullptr);
  mosquitto_message_callback_set(m, on_message);
  if (mosquitto_connect(m, "192.168.1.71", 1883, 60) != MOSQ_ERR_SUCCESS) {
    std::cerr << "MQTT connect failed\n";
  }
  const char *topics[] = {
    "matrix/weather/cond", "matrix/weather/temp", "matrix/weather/summary",
    "matrix/spotify/track", "matrix/spotify/artist", "matrix/control/brightness"
  };
  for (auto t : topics) mosquitto_subscribe(m, nullptr, t, 0);
  mosquitto_loop_start(m);

  // Draw canvas and scroll positions
  FrameCanvas *offscreen = matrix->CreateFrameCanvas();
  trackScroll.pos  = DISPLAY_WIDTH;
  artistScroll.pos = DISPLAY_WIDTH;

  constexpr int GAP = 10; // pixels between repeated copies
  const int FRAME_DELAY_US = 50 * 1000; // 20 fps, 50 ms per frame

  while (!interrupt_received) {
    // Check if we need to render by evaluating dirty state
    bool do_render = false;
    {
      std::lock_guard<std::mutex> lk(gState.m);
      if (gState.dirty) { do_render = true; gState.dirty = false; }
    }

    // If either line's stored width exceeds display width, keep rendering to scroll
    const bool scrolling_active = (trackScroll.width > DISPLAY_WIDTH) ||
                                  (artistScroll.width > DISPLAY_WIDTH);

    if (scrolling_active) do_render = true;

    if (do_render) {
      // Apply latest MQTT brightness safely (grab under lock inside a lambda). Right now only checked while rendering is active.
      // This is to ensure that brightness changes are applied without blocking the main loop.
      matrix->SetBrightness([]{
        std::lock_guard<std::mutex> lk(gState.m);
        return gState.brightness;
      }());

      offscreen->Clear();

      SharedState snapshot; SnapshotState(gState, snapshot);

      // Update scroll metadata if text changed 
      auto update_scroll = [&](ScrollInfo &si, const std::string &newText) {
        if (newText == si.text) return;
        si.text = newText;
        const int textW = TextWidth(font, si.text);
        si.width = (textW <= DISPLAY_WIDTH) ? textW : textW + GAP;
        si.pos   = DISPLAY_WIDTH;
      };

      update_scroll(trackScroll,  snapshot.track);
      update_scroll(artistScroll, snapshot.artist);
      

      // Draw the weather icon with text
      DrawWeatherIcon(snapshot.weatherCond, offscreen, 0, 0);
      DrawText(offscreen, font, 18, 10, yellow, nullptr,
               (snapshot.weatherTemp + "C").c_str());
      DrawText(offscreen, font, 0, 22, cyan, nullptr,
               snapshot.weatherSummary.substr(0, 20).c_str());

      // Printing track and artist from Spotify
      const int TRACK_Y  = 32 - 12;
      const int ARTIST_Y = 32 - 1;
      
      // Helper to draw (and scroll) a line of text. "si" holds scroll state; "col" is the color; "y" is the baseline.
      auto draw_scrolling = [&](ScrollInfo &si, const Color &col, int y) {
        if (si.text.empty()) return;
        if (si.width <= DISPLAY_WIDTH) {
          DrawText(offscreen, font, 0, y, col, nullptr, si.text.c_str());
        } else {
          DrawText(offscreen, font, si.pos, y, col, nullptr, si.text.c_str());
          DrawText(offscreen, font, si.pos + si.width, y, col, nullptr,
                   si.text.c_str());
          if (--si.pos + si.width < 0) si.pos += si.width; // seamless wrap
        }
      };

      draw_scrolling(trackScroll,  white,  TRACK_Y);
      draw_scrolling(artistScroll, green,  ARTIST_Y);

      // Swap offscreen to visible frame (double-buffered, synced to VSync)
      offscreen = matrix->SwapOnVSync(offscreen);
    }


    usleep(FRAME_DELAY_US);
  }

  // Cleanup -------------------------------------------------------------------
  mosquitto_loop_stop(m, true);
  mosquitto_destroy(m);
  mosquitto_lib_cleanup();
  delete matrix;
  return 0;
}
