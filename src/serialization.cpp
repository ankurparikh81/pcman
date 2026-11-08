#include "serialization.hpp"
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <istream>
#include <ostream>

namespace pcman {

namespace {

constexpr size_t HEADER_SIZE = 12; // magic (4) + version (2) + payloadSize (2) + checksum (4)

constexpr uint32_t ComputeFnv1a32(std::span<const uint8_t> data) {
    uint32_t hash = 0x811C9DC5u;
    for (uint8_t b : data) {
        hash ^= b;
        hash *= 0x01000193u;
    }
    return hash;
}

constexpr bool IsValidDirection(Direction d, bool allowNone) {
    uint8_t v = static_cast<uint8_t>(d);
    if (v <= 3) return true;
    return allowNone && (d == Direction::None);
}

constexpr bool IsValidGameState(GameState s) {
    return static_cast<uint8_t>(s) <= static_cast<uint8_t>(GameState::GameOver);
}

constexpr bool IsValidGhostFlags(uint8_t flags) {
    // Base state is flags & ~1: 0 (Active), 2 (ExitingPen), 4 (InPen), 6 (EatenEyes)
    uint8_t base = flags & static_cast<uint8_t>(~GhostFlags::Frightened);
    return base == GhostFlags::Active ||
           base == GhostFlags::ExitingPen ||
           base == GhostFlags::InPen ||
           base == GhostFlags::EatenEyes;
}

class BinaryWriter {
public:
    explicit BinaryWriter(std::vector<uint8_t>& buf) : m_buf(buf) {}

    void WriteU8(uint8_t v) {
        m_buf.push_back(v);
    }

    void WriteBool(bool v) {
        m_buf.push_back(v ? 1u : 0u);
    }

    void WriteU16(uint16_t v) {
        m_buf.push_back(static_cast<uint8_t>(v & 0xFFu));
        m_buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
    }

    void WriteU32(uint32_t v) {
        m_buf.push_back(static_cast<uint8_t>(v & 0xFFu));
        m_buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
        m_buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
        m_buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
    }

    void WriteI32(int32_t v) {
        WriteU32(static_cast<uint32_t>(v));
    }

    void WriteF32(float v) {
        WriteU32(std::bit_cast<uint32_t>(v));
    }

    void WriteBytes(std::span<const uint8_t> bytes) {
        m_buf.insert(m_buf.end(), bytes.begin(), bytes.end());
    }

private:
    std::vector<uint8_t>& m_buf;
};

class BinaryReader {
public:
    explicit BinaryReader(std::span<const uint8_t> data) : m_data(data) {}

    bool ReadU8(uint8_t& out) {
        if (m_pos + 1 > m_data.size()) return false;
        out = m_data[m_pos++];
        return true;
    }

    bool ReadBool(bool& out) {
        uint8_t v = 0;
        if (!ReadU8(v) || v > 1) return false;
        out = (v != 0);
        return true;
    }

    bool ReadU16(uint16_t& out) {
        if (m_pos + 2 > m_data.size()) return false;
        out = static_cast<uint16_t>(m_data[m_pos]) |
              (static_cast<uint16_t>(m_data[m_pos + 1]) << 8);
        m_pos += 2;
        return true;
    }

    bool ReadU32(uint32_t& out) {
        if (m_pos + 4 > m_data.size()) return false;
        out = static_cast<uint32_t>(m_data[m_pos]) |
              (static_cast<uint32_t>(m_data[m_pos + 1]) << 8) |
              (static_cast<uint32_t>(m_data[m_pos + 2]) << 16) |
              (static_cast<uint32_t>(m_data[m_pos + 3]) << 24);
        m_pos += 4;
        return true;
    }

    bool ReadI32(int32_t& out) {
        uint32_t u = 0;
        if (!ReadU32(u)) return false;
        out = static_cast<int32_t>(u);
        return true;
    }

    bool ReadF32(float& out) {
        uint32_t u = 0;
        if (!ReadU32(u)) return false;
        out = std::bit_cast<float>(u);
        return std::isfinite(out);
    }

    bool ReadBytes(std::span<uint8_t> out) {
        if (m_pos + out.size() > m_data.size()) return false;
        std::memcpy(out.data(), m_data.data() + m_pos, out.size());
        m_pos += out.size();
        return true;
    }

    [[nodiscard]] bool IsAtEnd() const { return m_pos == m_data.size(); }

private:
    std::span<const uint8_t> m_data;
    size_t m_pos = 0;
};

} // namespace

bool ValidateSnapshot(const GameSnapshot& s) {
    // 1. Validate Maze cells & remainingDots consistency
    int countedDots = 0;
    for (uint8_t c : s.maze.cells) {
        if (c > 3) return false;
        if (c == 2 || c == 3) countedDots++;
    }
    if (s.maze.remainingDots != countedDots) return false;

    // 2. Validate Pac-Man fields
    if (!IsValidDirection(s.pacman.currentDir, false)) return false;
    if (!IsValidDirection(s.pacman.requestedDir, true)) return false;
    if (s.pacman.mouthFrame < 0 || s.pacman.mouthFrame > 3) return false;
    if (s.pacman.deathFrame < 0 || s.pacman.deathFrame > 16) return false;
    if (s.pacman.deathTick < 0 || s.pacman.deathTick > 2) return false;

    // 3. Validate all 4 Ghosts
    for (size_t i = 0; i < s.ghosts.size(); ++i) {
        const auto& g = s.ghosts[i];
        if (static_cast<size_t>(g.type) != i) return false;
        if (!IsValidGhostFlags(g.flags)) return false;
        if (!IsValidDirection(g.dir, false)) return false;
        if (g.speedSkip > 2) return false;
        if (g.footFrame < 0 || g.footFrame > 1) return false;
    }

    // 4. Validate Game state & counters
    if (!IsValidGameState(s.state)) return false;
    if (s.score < 0 || s.highScore < 0) return false;
    if (s.lives < 0) return false;
    if (s.level < 1 || s.level > 12) return false;
    if (s.fruitTimer < 0 || s.fruitPopupTimer < 0) return false;
    if (s.ghostCombo < 0) return false;
    if (s.dotPauseToggle != 1 && s.dotPauseToggle != -1) return false;
    if (!std::isfinite(s.ghostFreezeTimer) || !std::isfinite(s.stateTimer) || !std::isfinite(s.tickAccumulator)) {
        return false;
    }
    if (s.deathTicks < 0 || s.frightTimer < 0) return false;
    if (!IsValidDirection(s.pendingInputDir, true)) return false;

    return true;
}

std::vector<uint8_t> SerializeSnapshot(const GameSnapshot& snapshot) {
    std::vector<uint8_t> payload;
    payload.reserve(1024);
    BinaryWriter pw(payload);

    // 1. Maze
    pw.WriteBytes(snapshot.maze.cells);
    pw.WriteI32(snapshot.maze.remainingDots);

    // 2. Pac-Man
    pw.WriteI32(snapshot.pacman.gridY);
    pw.WriteI32(snapshot.pacman.gridX);
    pw.WriteU8(static_cast<uint8_t>(snapshot.pacman.currentDir));
    pw.WriteU8(static_cast<uint8_t>(snapshot.pacman.requestedDir));
    pw.WriteBool(snapshot.pacman.isMoving);
    pw.WriteBool(snapshot.pacman.closedMouth);
    pw.WriteBool(snapshot.pacman.isDying);
    pw.WriteI32(snapshot.pacman.mouthFrame);
    pw.WriteI32(snapshot.pacman.deathFrame);
    pw.WriteI32(snapshot.pacman.deathTick);

    // 3. Ghosts (4)
    for (const auto& g : snapshot.ghosts) {
        pw.WriteU8(static_cast<uint8_t>(g.type));
        pw.WriteU8(g.flags);
        pw.WriteU8(static_cast<uint8_t>(g.dir));
        pw.WriteU8(g.speedSkip);
        pw.WriteI32(g.gridY);
        pw.WriteI32(g.gridX);
        pw.WriteI32(g.penPixelX);
        pw.WriteI32(g.penPixelY);
        pw.WriteI32(g.footFrame);
    }

    // 4. Game & RNG
    pw.WriteU16(snapshot.rngSeed);
    pw.WriteU8(static_cast<uint8_t>(snapshot.state));
    pw.WriteI32(snapshot.score);
    pw.WriteI32(snapshot.highScore);
    pw.WriteI32(snapshot.lives);
    pw.WriteI32(snapshot.level);
    pw.WriteBool(snapshot.fruitActive);
    pw.WriteI32(snapshot.fruitTimer);
    pw.WriteI32(snapshot.fruitPopupTimer);
    pw.WriteI32(snapshot.ghostCombo);
    pw.WriteI32(snapshot.dotPauseToggle);
    pw.WriteF32(snapshot.ghostFreezeTimer);
    pw.WriteI32(snapshot.eatenGhostPosX);
    pw.WriteI32(snapshot.eatenGhostPosY);
    pw.WriteI32(snapshot.deathTicks);
    pw.WriteBool(snapshot.awardedExtraLife);
    pw.WriteF32(snapshot.stateTimer);
    pw.WriteF32(snapshot.tickAccumulator);
    pw.WriteI32(snapshot.frightTimer);
    pw.WriteU8(static_cast<uint8_t>(snapshot.pendingInputDir));

    uint32_t checksum = ComputeFnv1a32(payload);

    std::vector<uint8_t> out;
    out.reserve(HEADER_SIZE + payload.size());
    BinaryWriter hw(out);
    hw.WriteU32(SNAPSHOT_MAGIC);
    hw.WriteU16(SNAPSHOT_VERSION);
    hw.WriteU16(static_cast<uint16_t>(payload.size()));
    hw.WriteU32(checksum);
    hw.WriteBytes(payload);
    return out;
}

bool DeserializeSnapshot(std::span<const uint8_t> bytes, GameSnapshot& outSnapshot) {
    if (bytes.size() < HEADER_SIZE) return false;

    BinaryReader hr(bytes.subspan(0, HEADER_SIZE));
    uint32_t magic = 0;
    uint16_t version = 0;
    uint16_t payloadSize = 0;
    uint32_t expectedChecksum = 0;
    if (!hr.ReadU32(magic) || !hr.ReadU16(version) ||
        !hr.ReadU16(payloadSize) || !hr.ReadU32(expectedChecksum)) {
        return false;
    }

    if (magic != SNAPSHOT_MAGIC || version != SNAPSHOT_VERSION) return false;
    if (bytes.size() != HEADER_SIZE + payloadSize) return false;

    std::span<const uint8_t> payload = bytes.subspan(HEADER_SIZE, payloadSize);
    if (ComputeFnv1a32(payload) != expectedChecksum) return false;

    BinaryReader pr(payload);
    GameSnapshot s{};

    // 1. Maze
    if (!pr.ReadBytes(s.maze.cells)) return false;
    if (!pr.ReadI32(s.maze.remainingDots)) return false;

    // 2. Pac-Man
    uint8_t curDir = 0;
    uint8_t reqDir = 0;
    if (!pr.ReadI32(s.pacman.gridY) ||
        !pr.ReadI32(s.pacman.gridX) ||
        !pr.ReadU8(curDir) ||
        !pr.ReadU8(reqDir) ||
        !pr.ReadBool(s.pacman.isMoving) ||
        !pr.ReadBool(s.pacman.closedMouth) ||
        !pr.ReadBool(s.pacman.isDying) ||
        !pr.ReadI32(s.pacman.mouthFrame) ||
        !pr.ReadI32(s.pacman.deathFrame) ||
        !pr.ReadI32(s.pacman.deathTick)) {
        return false;
    }
    s.pacman.currentDir = static_cast<Direction>(curDir);
    s.pacman.requestedDir = static_cast<Direction>(reqDir);

    // 3. Ghosts (4)
    for (auto& g : s.ghosts) {
        uint8_t gType = 0;
        uint8_t gDir = 0;
        if (!pr.ReadU8(gType) ||
            !pr.ReadU8(g.flags) ||
            !pr.ReadU8(gDir) ||
            !pr.ReadU8(g.speedSkip) ||
            !pr.ReadI32(g.gridY) ||
            !pr.ReadI32(g.gridX) ||
            !pr.ReadI32(g.penPixelX) ||
            !pr.ReadI32(g.penPixelY) ||
            !pr.ReadI32(g.footFrame)) {
            return false;
        }
        g.type = static_cast<GhostType>(gType);
        g.dir = static_cast<Direction>(gDir);
    }

    // 4. Game & RNG
    uint8_t stateVal = 0;
    uint8_t pendingDir = 0;
    if (!pr.ReadU16(s.rngSeed) ||
        !pr.ReadU8(stateVal) ||
        !pr.ReadI32(s.score) ||
        !pr.ReadI32(s.highScore) ||
        !pr.ReadI32(s.lives) ||
        !pr.ReadI32(s.level) ||
        !pr.ReadBool(s.fruitActive) ||
        !pr.ReadI32(s.fruitTimer) ||
        !pr.ReadI32(s.fruitPopupTimer) ||
        !pr.ReadI32(s.ghostCombo) ||
        !pr.ReadI32(s.dotPauseToggle) ||
        !pr.ReadF32(s.ghostFreezeTimer) ||
        !pr.ReadI32(s.eatenGhostPosX) ||
        !pr.ReadI32(s.eatenGhostPosY) ||
        !pr.ReadI32(s.deathTicks) ||
        !pr.ReadBool(s.awardedExtraLife) ||
        !pr.ReadF32(s.stateTimer) ||
        !pr.ReadF32(s.tickAccumulator) ||
        !pr.ReadI32(s.frightTimer) ||
        !pr.ReadU8(pendingDir)) {
        return false;
    }
    s.state = static_cast<GameState>(stateVal);
    s.pendingInputDir = static_cast<Direction>(pendingDir);

    if (!pr.IsAtEnd()) return false;
    if (!ValidateSnapshot(s)) return false;

    outSnapshot = s;
    return true;
}

bool WriteSnapshotToStream(const GameSnapshot& snapshot, std::ostream& os) {
    if (!ValidateSnapshot(snapshot)) return false;
    auto bytes = SerializeSnapshot(snapshot);
    os.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return os.good();
}

bool ReadSnapshotFromStream(std::istream& is, GameSnapshot& outSnapshot) {
    std::array<uint8_t, HEADER_SIZE> header{};
    if (!is.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(HEADER_SIZE))) {
        return false;
    }
    uint16_t payloadSize = static_cast<uint16_t>(header[6]) |
                           (static_cast<uint16_t>(header[7]) << 8);
    std::vector<uint8_t> bytes(HEADER_SIZE + payloadSize);
    std::memcpy(bytes.data(), header.data(), HEADER_SIZE);
    if (!is.read(reinterpret_cast<char*>(bytes.data() + HEADER_SIZE), static_cast<std::streamsize>(payloadSize))) {
        return false;
    }
    return DeserializeSnapshot(bytes, outSnapshot);
}

bool SaveSnapshotToFile(const GameSnapshot& snapshot, const std::filesystem::path& path) {
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) return false;
    return WriteSnapshotToStream(snapshot, ofs);
}

bool LoadSnapshotFromFile(const std::filesystem::path& path, GameSnapshot& outSnapshot) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) return false;
    return ReadSnapshotFromStream(ifs, outSnapshot);
}

} // namespace pcman
