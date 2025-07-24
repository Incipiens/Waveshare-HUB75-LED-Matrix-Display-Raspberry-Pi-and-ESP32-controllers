#pragma once
#include <string>
#include <mutex>

struct SharedState {
    std::string weatherCond;
    std::string weatherTemp;
    std::string weatherSummary;
    std::string track;
    std::string artist;
    int brightness = 50; // 0 to 100
    bool dirty = true; // set when something changed
    std::mutex m;
};

// Lock to snapshot so render loop can use local copy.
inline void SnapshotState(SharedState &src, SharedState &dest) {
    std::lock_guard<std::mutex> lk(src.m);
    dest.weatherCond = src.weatherCond;
    dest.weatherTemp = src.weatherTemp;
    dest.weatherSummary = src.weatherSummary;
    dest.track = src.track;
    dest.artist = src.artist;
    dest.brightness = src.brightness;
    dest.dirty = src.dirty;
}

// Mark dirty when external update occurs.
inline void MarkDirty(SharedState &st) {
    std::lock_guard<std::mutex> lk(st.m);
    st.dirty = true;
}
