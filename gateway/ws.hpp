#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Just enough WebSocket (RFC 6455) for a browser to subscribe to the feed:
// the handshake, binary frames, ping/pong and close. No fragmentation.
namespace stag::ws {

using Buffer = std::vector<std::uint8_t>;

enum Opcode : std::uint8_t { Text = 0x1, Binary = 0x2, Close = 0x8, Ping = 0x9, Pong = 0xA };

constexpr std::size_t MAX_REQUEST = 8192;   // handshake headers
constexpr std::size_t MAX_PAYLOAD = 4096;   // from clients, they only send tiny messages

inline std::array<std::uint8_t, 20> sha1(const std::string& msg) {
    std::uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    std::string data = msg;
    data.push_back(static_cast<char>(0x80));
    while (data.size() % 64 != 56) data.push_back(0);
    std::uint64_t bits = static_cast<std::uint64_t>(msg.size()) * 8;
    for (int i = 7; i >= 0; --i) data.push_back(static_cast<char>(bits >> (i * 8)));

    auto rol = [](std::uint32_t x, int n) { return (x << n) | (x >> (32 - n)); };
    for (std::size_t off = 0; off < data.size(); off += 64) {
        std::uint32_t w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = (std::uint32_t(std::uint8_t(data[off + 4 * i])) << 24) |
                   (std::uint32_t(std::uint8_t(data[off + 4 * i + 1])) << 16) |
                   (std::uint32_t(std::uint8_t(data[off + 4 * i + 2])) << 8) |
                    std::uint32_t(std::uint8_t(data[off + 4 * i + 3]));
        for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            std::uint32_t f, k;
            if (i < 20)      { f = (b & c) | (~b & d);          k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else             { f = b ^ c ^ d;                   k = 0xCA62C1D6; }
            std::uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    std::array<std::uint8_t, 20> out;
    for (int i = 0; i < 20; ++i) out[i] = static_cast<std::uint8_t>(h[i / 4] >> (24 - 8 * (i % 4)));
    return out;
}

inline std::string base64(const std::uint8_t* p, std::size_t n) {
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (std::size_t i = 0; i < n; i += 3) {
        std::uint32_t v = std::uint32_t(p[i]) << 16;
        if (i + 1 < n) v |= std::uint32_t(p[i + 1]) << 8;
        if (i + 2 < n) v |= p[i + 2];
        out.push_back(tbl[(v >> 18) & 63]);
        out.push_back(tbl[(v >> 12) & 63]);
        out.push_back(i + 1 < n ? tbl[(v >> 6) & 63] : '=');
        out.push_back(i + 2 < n ? tbl[v & 63] : '=');
    }
    return out;
}

inline std::string accept_key(const std::string& client_key) {
    auto d = sha1(client_key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    return base64(d.data(), d.size());
}

// Reads the client's upgrade request and appends our reply to `out`.
// False if it isn't a usable WebSocket request.
inline bool handshake(const std::string& req, Buffer& out) {
    std::string lower = req;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (lower.compare(0, 4, "get ") != 0) return false;
    const std::string name = "\r\nsec-websocket-key:";
    auto at = lower.find(name);
    if (at == std::string::npos) return false;
    at += name.size();
    auto end = req.find("\r\n", at);
    if (end == std::string::npos) return false;
    std::string key = req.substr(at, end - at);
    key.erase(0, key.find_first_not_of(' '));
    while (!key.empty() && key.back() == ' ') key.pop_back();
    if (key.empty()) return false;

    std::string reply =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + accept_key(key) + "\r\n\r\n";
    out.insert(out.end(), reply.begin(), reply.end());
    return true;
}

// Server frames are never masked.
inline void encode_frame(Buffer& out, Opcode op, const std::uint8_t* data, std::size_t n) {
    out.push_back(0x80 | op);
    if (n < 126) {
        out.push_back(static_cast<std::uint8_t>(n));
    } else {   // our payloads (snapshots included) stay under 64 KB
        out.push_back(126);
        out.push_back(static_cast<std::uint8_t>(n >> 8));
        out.push_back(static_cast<std::uint8_t>(n));
    }
    out.insert(out.end(), data, data + n);
}

struct Frame {
    Opcode op;
    Buffer payload;
};

enum class Parse { NeedMore, Ok, Bad };

// Reads one client frame from the front of the data. `used` is how many bytes it took.
// Client frames must be masked, whole (FIN set) and small.
inline Parse parse_frame(const std::uint8_t* p, std::size_t n, Frame& f, std::size_t& used) {
    if (n < 2) return Parse::NeedMore;
    if (!(p[0] & 0x80) || (p[0] & 0x70) || !(p[1] & 0x80)) return Parse::Bad;
    std::uint64_t len = p[1] & 0x7F;
    std::size_t at = 2;
    if (len == 126) {
        if (n < 4) return Parse::NeedMore;
        len = (std::uint64_t(p[2]) << 8) | p[3];
        at = 4;
    } else if (len == 127) {
        return Parse::Bad;
    }
    if (len > MAX_PAYLOAD) return Parse::Bad;
    if (n < at + 4 + len) return Parse::NeedMore;
    const std::uint8_t* mask = p + at;
    f.op = static_cast<Opcode>(p[0] & 0x0F);
    f.payload.resize(len);
    for (std::size_t i = 0; i < len; ++i) f.payload[i] = p[at + 4 + i] ^ mask[i % 4];
    used = at + 4 + len;
    return Parse::Ok;
}

}  // namespace stag::ws
