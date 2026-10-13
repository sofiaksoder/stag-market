#pragma once
#include "exchange.hpp"
#include "journal.hpp"

namespace stag {

// Feeds one journal event back into an exchange.
inline void apply(Exchange& ex, const JournalEvent& e) {
    switch (e.type) {
        case static_cast<std::uint8_t>(proto::Msg::NewOrder): ex.on_new_order(e.conn, e.order); break;
        case static_cast<std::uint8_t>(proto::Msg::Cancel):   ex.on_cancel(e.conn, e.cancel); break;
        case DISCONNECT_TYPE:                                  ex.on_disconnect(e.conn); break;
    }
}

}  // namespace stag
