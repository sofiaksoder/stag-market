#include <cstdio>

#include "ws.hpp"

using namespace stag::ws;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

static std::string hex(const std::array<std::uint8_t, 20>& d) {
    std::string s;
    char b[3];
    for (auto x : d) { std::snprintf(b, sizeof b, "%02x", x); s += b; }
    return s;
}

static void test_hashing() {
    CHECK(hex(sha1("abc")) == "a9993e364706816aba3e25717850c26c9cd0d89d");
    CHECK(hex(sha1("")) == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    std::string two_blocks = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    CHECK(hex(sha1(two_blocks)) == "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
    CHECK(base64(reinterpret_cast<const std::uint8_t*>("Ma"), 2) == "TWE=");
    // The example from the RFC.
    CHECK(accept_key("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

static void test_handshake() {
    Buffer out;
    std::string req =
        "GET /feed HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\n"
        "Connection: Upgrade\r\nsec-websocket-key:  dGhlIHNhbXBsZSBub25jZQ==  \r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n";
    CHECK(handshake(req, out));
    std::string reply(out.begin(), out.end());
    CHECK(reply.find("101 Switching Protocols") != std::string::npos);
    CHECK(reply.find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n") != std::string::npos);

    out.clear();
    CHECK(!handshake("GET / HTTP/1.1\r\nHost: x\r\n\r\n", out));   // no key
    CHECK(!handshake("POST / HTTP/1.1\r\nSec-WebSocket-Key: abc\r\n\r\n", out));
}

static Buffer masked(std::uint8_t first, const Buffer& payload) {
    const std::uint8_t mask[4] = {0x37, 0xfa, 0x21, 0x3d};
    Buffer f{first, static_cast<std::uint8_t>(0x80 | payload.size())};
    f.insert(f.end(), mask, mask + 4);
    for (std::size_t i = 0; i < payload.size(); ++i) f.push_back(payload[i] ^ mask[i % 4]);
    return f;
}

static void test_frames() {
    Frame f;
    std::size_t used = 0;
    Buffer in = masked(0x82, {23});   // binary, one byte
    CHECK(parse_frame(in.data(), in.size(), f, used) == Parse::Ok);
    CHECK(used == in.size() && f.op == Binary && f.payload.size() == 1 && f.payload[0] == 23);
    CHECK(parse_frame(in.data(), in.size() - 1, f, used) == Parse::NeedMore);
    CHECK(parse_frame(in.data(), 1, f, used) == Parse::NeedMore);

    Buffer unmasked{0x82, 0x01, 23};
    CHECK(parse_frame(unmasked.data(), unmasked.size(), f, used) == Parse::Bad);
    Buffer fragment = masked(0x02, {23});   // FIN not set
    CHECK(parse_frame(fragment.data(), fragment.size(), f, used) == Parse::Bad);

    Buffer out;
    std::uint8_t small[3] = {1, 2, 3};
    encode_frame(out, Binary, small, 3);
    CHECK(out.size() == 5 && out[0] == 0x82 && out[1] == 3);
    out.clear();
    Buffer big(300, 7);
    encode_frame(out, Binary, big.data(), big.size());
    CHECK(out.size() == 4 + 300 && out[1] == 126 && out[2] == 1 && out[3] == 44);
}

int main() {
    test_hashing();
    test_handshake();
    test_frames();
    if (g_fail) { std::printf("%d check(s) failed\n", g_fail); return 1; }
    std::puts("all websocket tests passed");
}
