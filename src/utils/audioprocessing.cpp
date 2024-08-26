#include "audioprocessing.h"
#include <cmath>
#include <algorithm>

AGC::AGC(float desiredLevel, float attackTimeMs, float releaseTimeMs, float lookAheadTimeMs, float sr)
    : desired_level(desiredLevel), gain(1.0f), sample_rate(sr),
      noise_estimate(0), noise_adapt_speed(0.001f), 
      noise_reduction_smoothing(0.9f), last_noise_reduction(1.0f) {
    look_ahead_samples = static_cast<size_t>(lookAheadTimeMs * sample_rate / 1000.0f);
    attack_coeff = 1 - exp(-1.0f / (attackTimeMs * 0.001f * sample_rate));
    release_coeff = 1 - exp(-1.0f / (releaseTimeMs * 0.001f * sample_rate));
}

void AGC::push(float sample) {
    lookahead_buffer.push_back(sample);
    while (lookahead_max.size() && std::abs(lookahead_max.back()) < std::abs(sample)) {
        lookahead_max.pop_back();
    }
    lookahead_max.push_back(sample);

    if (lookahead_buffer.size() > look_ahead_samples) {
        this->pop();
    }
}

void AGC::pop() {
    float sample = lookahead_buffer.front();
    lookahead_buffer.pop_front();
    if (sample == lookahead_max.front()) {
        lookahead_max.pop_front();
    }
}

float AGC::max() { return std::abs(lookahead_max.front()); }

void AGC::updateNoiseEstimate(float sample) {
    float abs_sample = std::abs(sample);
    if (abs_sample < noise_estimate) {
        noise_estimate = noise_estimate * (1 - noise_adapt_speed) + abs_sample * noise_adapt_speed;
    } else {
        noise_estimate = noise_estimate * (1 - noise_adapt_speed * 0.1f) + abs_sample * noise_adapt_speed * 0.1f;
    }
}


float AGC::calculateNoiseReduction(float sample) {
    float abs_sample = std::abs(sample);
    float snr = abs_sample / (noise_estimate + 1e-10f);
    float raw_reduction = std::min(1.0f, std::max(0.0f, (snr - 1) / (snr + 1)));
    
    // Apply a more gentle curve to the reduction
    float gentle_reduction = std::pow(raw_reduction, 0.5f);
    
    // Smooth the reduction to avoid abrupt changes
    float smoothed_reduction = last_noise_reduction * noise_reduction_smoothing + 
                               gentle_reduction * (1 - noise_reduction_smoothing);
    
    last_noise_reduction = smoothed_reduction;
    return smoothed_reduction;
}

void AGC::process(float *arr, size_t len) {
    for (size_t i = 0; i < len; i++) {
        this->push(arr[i]);
        updateNoiseEstimate(arr[i]);

        if (lookahead_buffer.size() == look_ahead_samples) {
            float current_sample = lookahead_buffer.front();

            float peak_sample = this->max();
            float desired_gain = desired_level / (peak_sample + 1e-10f);

            if (desired_gain < gain) {
                gain = gain - attack_coeff * (gain - desired_gain);
            } else {
                gain = gain + release_coeff * (desired_gain - gain);
            }

            // Noise reduction
            float noise_reduction = calculateNoiseReduction(current_sample);
            float reduced_sample = current_sample * (noise_reduction * 0.7f + 0.3f);  // Less aggressive reduction

            // Apply the gain to the current sample
            arr[i] = reduced_sample * (gain * 0.01);

            if (i % 1000 == 0) {
                printf("Noise estimate: %f - Noise Reduction: %f\n", noise_estimate, noise_reduction);
            }
        } else {
            arr[i] = 0.0f;
        }
    }
}

void AGC::reset() {
    gain = 1.0f;
    lookahead_buffer.clear();
    lookahead_max.clear();
    noise_estimate = 0;
    last_noise_reduction = 1.0f;
}