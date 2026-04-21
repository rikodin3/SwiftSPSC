#ifndef SWIFTSPSC_HFT_TYPES_H
#define SWIFTSPSC_HFT_TYPES_H

#include <cstdint>
#include <string_view>

namespace hft {

enum class Side : uint8_t { Buy = 0, Sell = 1 };

struct MarketUpdate {
    uint64_t timestamp_ns;
    uint64_t sequence;
    double price;
    uint32_t quantity;
    char symbol[8];
    Side side;
    bool is_trade;
};

// Map this onto the SwiftSPSC Item data field or extend queue_common.h
// Since SwiftSPSC::ipc::Item is a simple wrapper, we'll design 
// our HFT messages to fit or be compatible.
}

#endif
