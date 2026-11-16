#ifndef JOTAPE_AUDIO_CODEC_H
#define JOTAPE_AUDIO_CODEC_H

#include <esp_timer.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

#include "codecs/box_audio_codec.h"

// BoxAudioCodec that also measures the loudness of the microphone and of
// the speaker output, so the UI can draw a sound wave that follows real audio.
class JotapeAudioCodec : public BoxAudioCodec {
public:
    using BoxAudioCodec::BoxAudioCodec;

    void OutputData(std::vector<int16_t>& data) override {
        Measure(data.data(), data.size(), 1, output_level_, output_time_us_);
        BoxAudioCodec::OutputData(data);
    }

    bool InputData(std::vector<int16_t>& data) override {
        const bool ok = BoxAudioCodec::InputData(data);
        if (ok) {
            // Interleaved mic (+ reference) channels: measure the first mic only.
            const int step = input_channels() > 0 ? input_channels() : 1;
            Measure(data.data(), data.size(), step, input_level_, input_time_us_);
        }
        return ok;
    }

    // Loudness 0..100 of the last ~250 ms; 0 when silent or stale.
    int output_level() const { return Fresh(output_level_, output_time_us_); }
    int input_level() const { return Fresh(input_level_, input_time_us_); }

private:
    static void Measure(const int16_t* samples, size_t count, int step, std::atomic<int>& level,
                        std::atomic<int64_t>& time_us) {
        if (count == 0) {
            return;
        }
        double sum = 0;
        size_t n = 0;
        for (size_t i = 0; i < count; i += step) {
            const double s = samples[i];
            sum += s * s;
            n++;
        }
        const double rms = std::sqrt(sum / (n > 0 ? n : 1));
        // Map -50 dBFS .. -12 dBFS to 0..100.
        const double db = 20.0 * std::log10(rms / 32768.0 + 1e-9);
        int value = static_cast<int>((db + 50.0) * 100.0 / 38.0);
        value = value < 0 ? 0 : (value > 100 ? 100 : value);
        level.store(value, std::memory_order_relaxed);
        time_us.store(esp_timer_get_time(), std::memory_order_relaxed);
    }

    static int Fresh(const std::atomic<int>& level, const std::atomic<int64_t>& time_us) {
        if (esp_timer_get_time() - time_us.load(std::memory_order_relaxed) > 250000) {
            return 0;
        }
        return level.load(std::memory_order_relaxed);
    }

    std::atomic<int> output_level_{0};
    std::atomic<int64_t> output_time_us_{0};
    std::atomic<int> input_level_{0};
    std::atomic<int64_t> input_time_us_{0};
};

#endif  // JOTAPE_AUDIO_CODEC_H
