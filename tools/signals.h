#pragma once
// Deterministic audition sources shared by before/after renders.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>
namespace audition {
struct Rng {
    std::uint32_t s;
    float uni() { s = s * 1664525u + 1013904223u; return static_cast<float>(s >> 8) * (1.f / 16777216.f); }
    float bipolar() { return uni() * 2.f - 1.f; }
};
inline void addPianoNote(std::vector<float>& l, std::vector<float>& r, int start, float freq, float amp, float pan, int sr, float decayScale) {
    constexpr float pi = 3.14159265358979323846f;
    constexpr float B = 0.00032f;
    const int n = static_cast<int>(l.size());
    const float gL = std::cos((pan + 1.f) * pi * .25f), gR = std::sin((pan + 1.f) * pi * .25f);
    for (int p = 1; p <= 14; ++p) {
        const float f = freq * p * std::sqrt(1.f + B * static_cast<float>(p * p));
        if (f > sr * .45f) break;
        const float pamp = amp / std::pow(static_cast<float>(p), 1.22f);
        const float decay = (1.35f + p * 1.85f) * decayScale;
        const float phase = .17f * p;
        for (int i = start; i < n; ++i) {
            const float t = static_cast<float>(i - start) / static_cast<float>(sr);
            const float s = std::sin(2.f * pi * (f * t + phase)) * pamp * std::exp(-decay * t);
            l[static_cast<std::size_t>(i)] += s * gL;
            r[static_cast<std::size_t>(i)] += s * gR;
        }
    }
    Rng rng{10007u + static_cast<std::uint32_t>(freq)};
    float lp = 0;
    for (int i = start; i < n && i < start + sr / 70; ++i) {
        const float t = static_cast<float>(i - start) / static_cast<float>(sr);
        lp += .35f * (rng.bipolar() - lp);
        const float s = lp * amp * .22f * std::exp(-t * 90.f);
        l[static_cast<std::size_t>(i)] += s * gL;
        r[static_cast<std::size_t>(i)] += s * gR;
    }
}
inline void addPianoChord(std::vector<float>& l, std::vector<float>& r, int start, int sr, bool stab) {
    const float decay = stab ? 2.4f : 1.f;
    const float amp = stab ? .34f : .28f;
    const float notes[][2] = {{220.f, -.35f}, {261.63f, -.12f}, {329.63f, .12f}, {392.f, .32f}};
    for (const auto& note : notes) addPianoNote(l, r, start, note[0], amp, note[1], sr, decay);
}
inline void addClap(std::vector<float>& l, std::vector<float>& r, int start, int sr) {
    Rng rng{0xC1A9u};
    const int n = static_cast<int>(l.size());
    float hp = 0, prev = 0;
    for (int burst = 0; burst < 4; ++burst) {
        const int at = start + burst * static_cast<int>(.011 * sr);
        const int len = static_cast<int>((burst == 3 ? .09 : .045) * sr);
        for (int i = at; i < n && i < at + len; ++i) {
            const float t = static_cast<float>(i - at) / static_cast<float>(sr);
            const float noise = rng.bipolar();
            hp = noise - prev + .94f * hp;
            prev = noise;
            const float env = std::exp(-t * (burst == 3 ? 16.f : 48.f));
            const float s = hp * .55f * env;
            l[static_cast<std::size_t>(i)] += s;
            r[static_cast<std::size_t>(i)] += s * (burst % 2 ? .92f : 1.f);
        }
    }
    for (int i = start; i < n && i < start + sr / 40; ++i) {
        const float t = static_cast<float>(i - start) / static_cast<float>(sr);
        const float s = std::sin(2.f * 3.14159265358979323846f * 168.f * t) * .28f * std::exp(-t * 36.f);
        l[static_cast<std::size_t>(i)] += s;
        r[static_cast<std::size_t>(i)] += s;
    }
}
inline void addPluck(std::vector<float>& l, std::vector<float>& r, int start, float freq, int sr) {
    const int n = static_cast<int>(l.size());
    const int period = std::max(8, static_cast<int>(std::lround(static_cast<float>(sr) / freq)));
    std::vector<float> buf(static_cast<std::size_t>(period));
    Rng rng{0x91u + static_cast<std::uint32_t>(freq)};
    for (auto& s : buf) s = rng.bipolar();
    float prev = 0;
    int idx = 0;
    for (int i = start; i < n && i < start + sr * 2; ++i) {
        const float in = buf[static_cast<std::size_t>(idx)];
        const float lp = .5f * (in + prev);
        prev = in;
        buf[static_cast<std::size_t>(idx)] = lp * .9945f;
        idx = (idx + 1) % period;
        const float t = static_cast<float>(i - start) / static_cast<float>(sr);
        const float s = lp * .45f * std::exp(-t * .4f);
        l[static_cast<std::size_t>(i)] += s;
        r[static_cast<std::size_t>(i)] += s * .97f;
    }
}
inline void normalize(std::vector<float>& l, std::vector<float>& r, float peak) {
    float m = 0;
    for (std::size_t i = 0; i < l.size(); ++i) m = std::max(m, std::max(std::abs(l[i]), std::abs(r[i])));
    if (m < 1.e-8f) return;
    const float g = peak / m;
    for (std::size_t i = 0; i < l.size(); ++i) { l[i] *= g; r[i] *= g; }
}
inline void writeWav16(const std::string& path, const std::vector<float>& l, const std::vector<float>& r, int sr) {
    const auto frames = static_cast<std::uint32_t>(std::min(l.size(), r.size()));
    std::ofstream out(path, std::ios::binary);
    const auto put = [&](auto v) { out.write(reinterpret_cast<const char*>(&v), sizeof(v)); };
    const std::uint32_t dataBytes = frames * 4u, chunk = 36u + dataBytes;
    out.write("RIFF", 4); put(chunk); out.write("WAVE", 4);
    out.write("fmt ", 4); put(std::uint32_t{16}); put(std::uint16_t{1}); put(std::uint16_t{2});
    put(std::uint32_t{static_cast<std::uint32_t>(sr)}); put(std::uint32_t{static_cast<std::uint32_t>(sr) * 4u});
    put(std::uint16_t{4}); put(std::uint16_t{16});
    out.write("data", 4); put(dataBytes);
    Rng rng{0xD17Eu};
    for (std::uint32_t i = 0; i < frames; ++i) {
        for (float s : {l[i], r[i]}) {
            const float d = (rng.uni() - rng.uni()) / 65536.f;
            const float y = std::clamp(s + d, -1.f, 1.f);
            const auto q = static_cast<std::int16_t>(std::lrint(y * 32767.f));
            put(q);
        }
    }
}
}
