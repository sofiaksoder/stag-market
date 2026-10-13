// TCP front end for the exchange. One thread, poll() loop.
// Usage: stag_gateway [port] [journal file]
// Orders come in on one port, market data goes out on port + 1 (raw TCP)
// and port + 2 (WebSocket, for the browser).
// (Phase 5 moves this onto its own thread with SPSC rings, and epoll on Linux.)
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>

#include "exchange.hpp"
#include "ws.hpp"

using namespace stag;

enum class Kind { Order, Md, Ws };

struct Conn {
    int           fd;
    Kind          kind;
    bool          ws_open = false;   // handshake done
    proto::Buffer rx, tx;

    Conn(int f = -1, Kind k = Kind::Order) : fd(f), kind(k) {}

    bool subscriber() const { return kind == Kind::Md || (kind == Kind::Ws && ws_open); }
};

// A subscriber this far behind is dropped rather than buffered forever.
constexpr std::size_t MAX_MD_BACKLOG = 4 << 20;

static void set_nonblocking(int fd) { fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK); }

static int listen_on(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0 || listen(fd, 16) < 0) {
        std::perror("bind/listen");
        std::exit(1);
    }
    set_nonblocking(fd);
    return fd;
}

// Market data goes to browsers as one WebSocket message per feed message,
// so the 2-byte length prefix isn't needed.
static void append_ws(proto::Buffer& tx, const proto::Buffer& framed) {
    ws::encode_frame(tx, ws::Binary, framed.data() + 2, framed.size() - 2);
}

static void send_snapshot(Conn& c, Exchange& ex) {
    if (c.kind == Kind::Ws) {
        proto::Buffer snap;
        ex.snapshot(snap);
        append_ws(c.tx, snap);
    } else {
        ex.snapshot(c.tx);
    }
}

// Handles whatever a browser sent so far. False means drop the connection.
static bool process_ws(Conn& c, Exchange& ex) {
    if (!c.ws_open) {
        static const std::string end = "\r\n\r\n";
        auto at = std::search(c.rx.begin(), c.rx.end(), end.begin(), end.end());
        if (at == c.rx.end()) return c.rx.size() < ws::MAX_REQUEST;
        std::string req(c.rx.begin(), at + 4);
        if (!ws::handshake(req, c.tx)) return false;
        c.rx.erase(c.rx.begin(), at + 4);
        c.ws_open = true;
        send_snapshot(c, ex);
    }
    for (;;) {
        ws::Frame f;
        std::size_t used = 0;
        auto r = ws::parse_frame(c.rx.data(), c.rx.size(), f, used);
        if (r == ws::Parse::NeedMore) return true;
        if (r == ws::Parse::Bad) return false;
        c.rx.erase(c.rx.begin(), c.rx.begin() + static_cast<std::ptrdiff_t>(used));
        if (f.op == ws::Ping) {
            ws::encode_frame(c.tx, ws::Pong, f.payload.data(), f.payload.size());
        } else if (f.op == ws::Binary && !f.payload.empty()) {
            // Same messages as the TCP protocol, minus the length prefix.
            auto type = static_cast<proto::Msg>(f.payload[0]);
            const std::uint8_t* body = f.payload.data() + 1;
            std::size_t blen = f.payload.size() - 1;
            if (type == proto::Msg::SnapshotReq && blen == 0) {
                send_snapshot(c, ex);
            } else if (type == proto::Msg::NewOrder) {
                proto::NewOrder m;
                if (!proto::decode(body, blen, m)) return false;
                ex.on_new_order(c.fd, m);
            } else if (type == proto::Msg::Cancel) {
                proto::Cancel m;
                if (!proto::decode(body, blen, m)) return false;
                ex.on_cancel(c.fd, m);
            } else {
                return false;
            }
        } else if (f.op != ws::Pong) {
            return false;   // close, text, or anything we don't speak
        }
    }
}

int main(int argc, char** argv) {
    int port = argc > 1 ? std::atoi(argv[1]) : 9000;
    std::signal(SIGPIPE, SIG_IGN);
    const char* journal_path = argc > 2 ? argv[2] : nullptr;

    int lfd = listen_on(port);
    int mfd = listen_on(port + 1);
    int wfd = listen_on(port + 2);
    int on = 1;

    std::map<int, Conn> conns;   // keyed by fd, which doubles as the ConnId
    // STAG: 1 tick = $0.01, so the prices go up to $1000.00
    const OrderBook::Config cfg{1, 100'000, 1 << 20};
    Exchange ex(cfg, [&](int id, const proto::Buffer& b) {
        auto it = conns.find(id);
        if (it == conns.end()) return;
        if (it->second.kind == Kind::Ws) append_ws(it->second.tx, b);
        else it->second.tx.insert(it->second.tx.end(), b.begin(), b.end());
    }, [&](const proto::Buffer& b) {
        for (auto& [fd, c] : conns)
            if (c.kind == Kind::Md) c.tx.insert(c.tx.end(), b.begin(), b.end());
            else if (c.subscriber()) append_ws(c.tx, b);
    });

    std::unique_ptr<JournalWriter> journal;
    if (journal_path) {
        journal = std::make_unique<JournalWriter>(journal_path, cfg);
        if (!journal->ok()) {
            std::perror(journal_path);
            return 1;
        }
        ex.set_journal(journal.get());
        std::printf("journaling to %s\n", journal_path);
    }

    std::printf("stag gateway: orders on 127.0.0.1:%d, market data on %d, websocket on %d\n",
                port, port + 1, port + 2);
    std::vector<pollfd> fds;
    for (;;) {
        fds.clear();
        fds.push_back({lfd, POLLIN, 0});
        fds.push_back({mfd, POLLIN, 0});
        fds.push_back({wfd, POLLIN, 0});
        for (auto& [fd, c] : conns)
            fds.push_back({fd, static_cast<short>(POLLIN | (c.tx.empty() ? 0 : POLLOUT)), 0});
        if (poll(fds.data(), fds.size(), -1) < 0) {
            if (errno == EINTR) continue;
            std::perror("poll");
            return 1;
        }

        std::vector<int> dead;
        for (std::size_t i = 0; i < 3; ++i) {
            if (!(fds[i].revents & POLLIN)) continue;
            int fd = accept(fds[i].fd, nullptr, nullptr);
            if (fd < 0) continue;
            set_nonblocking(fd);
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof on);
            Kind kind = i == 0 ? Kind::Order : i == 1 ? Kind::Md : Kind::Ws;
            conns[fd] = Conn(fd, kind);
            // Subscribers start from a snapshot (browsers get theirs after the handshake).
            if (kind == Kind::Md) send_snapshot(conns[fd], ex);
        }

        for (std::size_t i = 3; i < fds.size(); ++i) {
            int fd = fds[i].fd;
            Conn& c = conns[fd];
            if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) { dead.push_back(fd); continue; }

            if (fds[i].revents & POLLIN) {
                std::uint8_t buf[4096];
                ssize_t n = recv(fd, buf, sizeof buf, 0);
                if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) { dead.push_back(fd); continue; }
                if (n > 0) c.rx.insert(c.rx.end(), buf, buf + n);

                if (c.kind == Kind::Ws) {
                    if (!process_ws(c, ex)) dead.push_back(fd);
                    continue;
                }

                // Peel off whole frames.
                std::size_t pos = 0;
                bool bad = false;
                while (c.rx.size() - pos >= 3) {
                    std::uint16_t len;
                    std::memcpy(&len, &c.rx[pos], 2);
                    if (len < 1) { bad = true; break; }
                    if (c.rx.size() - pos - 2 < len) break;
                    auto type = static_cast<proto::Msg>(c.rx[pos + 2]);
                    const std::uint8_t* body = &c.rx[pos + 3];
                    std::size_t blen = len - 1;
                    if (c.kind == Kind::Md) {
                        if (type == proto::Msg::SnapshotReq && blen == 0) send_snapshot(c, ex);
                        else bad = true;
                    } else if (type == proto::Msg::NewOrder) {
                        proto::NewOrder m;
                        if (proto::decode(body, blen, m)) ex.on_new_order(fd, m); else bad = true;
                    } else if (type == proto::Msg::Cancel) {
                        proto::Cancel m;
                        if (proto::decode(body, blen, m)) ex.on_cancel(fd, m); else bad = true;
                    } else {
                        bad = true;
                    }
                    if (bad) break;
                    pos += 2 + len;
                }
                c.rx.erase(c.rx.begin(), c.rx.begin() + static_cast<std::ptrdiff_t>(pos));
                if (bad) { dead.push_back(fd); continue; }
            }
        }

        // Flush whatever replies were queued, including to clients that didn't poll as writable.
        for (auto& [fd, c] : conns) {
            if (c.tx.empty()) continue;
            ssize_t n = send(fd, c.tx.data(), c.tx.size(), 0);
            if (n > 0) c.tx.erase(c.tx.begin(), c.tx.begin() + n);
            else if (n < 0 && errno != EAGAIN && errno != EINTR) dead.push_back(fd);
            if (c.kind != Kind::Order && c.tx.size() > MAX_MD_BACKLOG) dead.push_back(fd);
        }

        for (int fd : dead) {
            if (!conns.count(fd)) continue;
            ex.on_disconnect(fd);
            close(fd);
            conns.erase(fd);
        }
    }
}
