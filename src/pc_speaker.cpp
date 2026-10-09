#include "pc_speaker.hpp"
#include "rom_data.hpp"
#include <cmath>
#include <algorithm>

namespace pcman {

namespace {
    PcSpeaker* s_instance = nullptr;
    constexpr int SAMPLE_RATE = 44100;
    constexpr float PIT_CLOCK = 1193182.0f;

    // Authentic Channel tables matching v1.com
    constexpr uint16_t CH0_TABLE[8] = { 5000, 2, 6000, 2, 7500, 2, 10000, 2 }; // DS:0x2FB4
    constexpr uint16_t CH1_TABLE[8] = { 675, 736, 803, 875, 955, 1041, 1135, 1238 }; // DS:0x2FC4
    constexpr uint16_t CH2_TABLE[8] = { 400, 436, 480, 533, 600, 533, 480, 436 }; // DS:0x2FA2
}

PcSpeaker::PcSpeaker() {
    s_instance = this;
}

PcSpeaker::~PcSpeaker() {
    Close();
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

void PcSpeaker::Init() {
    if (m_initialized) return;

    m_stream = LoadAudioStream(SAMPLE_RATE, 16, 1);
    SetAudioStreamCallback(m_stream, AudioInputCallback);
    PlayAudioStream(m_stream);

    Silence();
    m_initialized = true;
}

void PcSpeaker::Close() {
    if (m_initialized) {
        StopAudioStream(m_stream);
        UnloadAudioStream(m_stream);
        m_initialized = false;
    }
}

void PcSpeaker::SetMuted(bool muted) {
    m_muted = muted;
    if (m_muted) m_frequency = 0.0f;
}

bool PcSpeaker::IsMuted() const {
    return m_muted;
}

void PcSpeaker::ToggleMute() {
    SetMuted(!m_muted);
}

void PcSpeaker::Silence() {
    m_channel0 = 0;
    m_channel1 = 0;
    m_channel2 = 0;
    m_channel3 = 0;
    m_titleNote = -1;
    m_deathTick = -1;
    m_ghostEatenActive = false;
    m_ghostEatenSample = 0;
    m_frequency = 0.0f;
}

void PcSpeaker::PlayTitleTune() {
    Silence();
    m_titleNote = 0;
}

void PcSpeaker::TriggerDotEat(bool paused) {
    // DS:0x2F90 written at 0x062E and 0x0639 in v1.com
    m_channel0 = paused ? 3 : 2;
}

void PcSpeaker::TriggerEnergizer() {
    // DS:0x2F91 written at 0x05EB in v1.com
    m_channel1 = 25;
}

void PcSpeaker::TriggerFruitEat() {
    // DS:0x2F92 written at 0x0B70 in v1.com
    m_channel2 = 9;
}

void PcSpeaker::TriggerExtraLife() {
    // DS:0x2F93 written at 0x0B18 in v1.com
    m_channel3 = 24;
}

void PcSpeaker::TriggerDeathSound() {
    Silence();
    m_deathTick = 0;
}

void PcSpeaker::TriggerGhostEatenSound() {
    m_ghostEatenActive = true;
    m_ghostEatenSample = 0;
}

void PcSpeaker::Tick18Hz() {
    if (!m_initialized || m_muted) {
        m_frequency = 0.0f;
        return;
    }

    uint16_t divisor = 2; // Default silent (2 turns PC speaker off in v1.com)

    // 1. Title tune sequence (DS:0x04E6, 137 notes at 1 note per tick, plays ONCE)
    if (m_titleNote >= 0) {
        if (m_titleNote < 137) {
            divisor = RomData::GetTitleTuneDivisor(m_titleNote);
            m_titleNote++;
        } else {
            m_titleNote = -1; // Done, stops and does not loop!
            divisor = 2;
        }
    }
    // 2. Pac-Man death sequence (DS:0x2CD0, 34 notes at 1 note per tick = 34 ticks)
    else if (m_deathTick >= 0) {
        if (m_deathTick < 34) {
            divisor = RomData::GetDeathSoundDivisor(m_deathTick);
            m_deathTick++;
        } else {
            m_deathTick = -1;
            divisor = 2;
        }
    }
    // 3. In-game channel engine (DS:0x2F90..0x2F93 scanned at 0x0F21..0x0FB9)
    else {
        if (!m_ghostEatenActive) {
            int activeChannel = -1;
            uint8_t remaining = 0;

            if (m_channel0 > 0) {
                m_channel0--;
                activeChannel = 0;
                remaining = m_channel0;
            }
            if (m_channel1 > 0) {
                m_channel1--;
                activeChannel = 1;
                remaining = m_channel1;
            }
            if (m_channel2 > 0) {
                m_channel2--;
                activeChannel = 2;
                remaining = m_channel2;
            }
            if (m_channel3 > 0) {
                m_channel3--;
                activeChannel = 3;
                remaining = m_channel3;
            }

            switch (activeChannel) {
                case 0: // Dot eat: 0x0F9B in ROM
                    m_ch0Step = (m_ch0Step + 1) % 8;
                    divisor = CH0_TABLE[m_ch0Step];
                    break;
                case 1: // Energizer: 0x0F8A in ROM
                    divisor = CH1_TABLE[remaining % 8];
                    break;
                case 2: // Fruit eaten: 0x0F79 in ROM
                    divisor = CH2_TABLE[remaining % 8];
                    break;
                case 3: // Extra life: 0x0F6A in ROM
                    divisor = (remaining & 1) ? 400 : 2;
                    break;
                default: // No channels active -> complete silence!
                    divisor = 2;
                    break;
            }
        }
    }

    // Set frequency: divisor <= 2 means silence in PC-Man v1.com
    if (divisor > 2) {
        m_frequency = PIT_CLOCK / static_cast<float>(divisor);
    } else {
        m_frequency = 0.0f;
    }
}

void PcSpeaker::AudioInputCallback(void* buffer, unsigned int frames) {
    if (s_instance) {
        s_instance->FillAudioBuffer(static_cast<int16_t*>(buffer), frames);
    } else {
        std::fill_n(static_cast<int16_t*>(buffer), frames, 0);
    }
}

void PcSpeaker::FillAudioBuffer(int16_t* buffer, unsigned int frames) {
    if (m_muted) {
        std::fill_n(buffer, frames, 0);
        return;
    }

    constexpr int16_t AMPLITUDE = 12000; // Comfortable PC Speaker volume
    // 5130 loop iterations * 17 cycles / 4.77MHz * 44100 = 805.8 samples (~18.27ms per note)
    constexpr int SAMPLES_PER_GHOST_NOTE = 806;

    for (unsigned int i = 0; i < frames; ++i) {
        float curFreq = 0.0f;

        if (m_ghostEatenActive) {
            int note = m_ghostEatenSample / SAMPLES_PER_GHOST_NOTE;
            if (note < 24) {
                uint16_t divisor = RomData::GetGhostEatenSoundDivisor(note);
                if (divisor > 2) {
                    curFreq = PIT_CLOCK / static_cast<float>(divisor);
                }
                m_ghostEatenSample++;
            } else {
                m_ghostEatenActive = false;
                m_ghostEatenSample = 0;
            }
        } else {
            curFreq = m_frequency;
        }

        if (curFreq > 20.0f) {
            float phaseIncr = curFreq / static_cast<float>(SAMPLE_RATE);
            int16_t sample = (m_phase < 0.5f) ? AMPLITUDE : -AMPLITUDE;
            buffer[i] = sample;

            m_phase += phaseIncr;
            if (m_phase >= 1.0f) {
                m_phase -= 1.0f;
            }
        } else {
            buffer[i] = 0;
        }
    }
}

} // namespace pcman
