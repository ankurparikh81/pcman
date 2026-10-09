#pragma once
#include <raylib.h>
#include <cstdint>

namespace pcman {

// Authentic PC-Man v1.com PC Speaker sound synthesis (0x0F21..0x0FB9 in ROM)
class PcSpeaker {
public:
    PcSpeaker();
    ~PcSpeaker();

    void Init();
    void Close();

    // Authentic PC-Man v1.com 18.2065 Hz BIOS tick update (0x07F0 -> 0x0F21 in ROM)
    void Tick18Hz();

    // In-game channel triggers matching authentic writes to DS:0x2F90..0x2F93
    void TriggerDotEat(bool paused); // DS:0x2F90: Dot eaten timer (2 or 3 ticks)
    void TriggerEnergizer();         // DS:0x2F91: Energizer timer (25 ticks)
    void TriggerFruitEat();          // DS:0x2F92: Fruit eaten timer (9 ticks)
    void TriggerExtraLife();         // DS:0x2F93: Extra life timer (24 ticks)

    // Modal sequences matching 0x005E (Title), 0x0414 (Death), and 0x0CE1 (Ghost Eaten)
    void PlayTitleTune();            // 137 notes at DS:0x04E6 (plays ONCE at 18.2Hz)
    bool IsTitleTunePlaying() const { return m_titleNote >= 0; }
    bool IsTitleTuneDone() const { return m_titleNote < 0; }

    void TriggerDeathSound();        // 34 notes at DS:0x2CD0 (1 note per tick = 34 ticks)
    void TriggerGhostEatenSound();   // 24 notes at DS:0x2FF6 (~18.27ms per note hardware arpeggio)
    void Silence();                  // Immediately silences speaker (divisor 2)

    void SetMuted(bool muted);
    bool IsMuted() const;
    void ToggleMute();

private:
    AudioStream m_stream{};
    bool m_initialized = false;
    bool m_muted = false;

    // Authentic PC-Man v1.com hardware state variables:
    uint8_t m_channel0 = 0; // DS:0x2F90: Dot eat timer (0 = silent)
    uint8_t m_channel1 = 0; // DS:0x2F91: Energizer timer
    uint8_t m_channel2 = 0; // DS:0x2F92: Fruit eat timer
    uint8_t m_channel3 = 0; // DS:0x2F93: Extra life timer
    uint8_t m_ch0Step = 0;  // DS:0x2FA1: Channel 0 table index (0..7)

    // Modal sequence states
    int m_titleNote = -1;       // 0..136, -1 when inactive (plays ONCE)
    int m_deathTick = -1;       // 0..33 (34 notes * 1 tick), -1 when inactive

    // Ghost eaten arpeggio (DS:0x2FF6, 24 notes at 806 samples/note @ 44.1kHz = ~18.27ms per note)
    int m_ghostEatenSample = 0;
    bool m_ghostEatenActive = false;

    // Frequency synthesis
    float m_frequency = 0.0f;
    float m_phase = 0.0f;

    static void AudioInputCallback(void* buffer, unsigned int frames);
    void FillAudioBuffer(int16_t* buffer, unsigned int frames);
};

} // namespace pcman
