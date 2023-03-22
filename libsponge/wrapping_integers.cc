#include "wrapping_integers.hh"

#include <cassert>
#include <limits>
#include <vector>

// Dummy implementation of a 32-bit wrapping integer

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

constexpr static uint64_t FACTOR = static_cast<uint64_t>(numeric_limits<uint32_t>::max()) + 1;
constexpr static uint64_t FACTOR_MASK = static_cast<uint64_t>(numeric_limits<uint32_t>::max());

//! Transform an "absolute" 64-bit sequence number (zero-indexed) into a WrappingInt32
//! \param n The input absolute 64-bit sequence number
//! \param isn The initial sequence number
WrappingInt32 wrap(uint64_t n, WrappingInt32 isn) { return isn + (n & FACTOR_MASK); }

//! Transform a WrappingInt32 into an "absolute" 64-bit sequence number (zero-indexed)
//! \param n The relative sequence number
//! \param isn The initial sequence number
//! \param checkpoint A recent absolute 64-bit sequence number
//! \returns the 64-bit sequence number that wraps to `n` and is closest to `checkpoint`
//!
//! \note Each of the two streams of the TCP connection has its own ISN. One stream
//! runs from the local TCPSender to the remote TCPReceiver and has one ISN,
//! and the other stream runs from the remote TCPSender to the local TCPReceiver and
//! has a different ISN.
uint64_t unwrap(WrappingInt32 n, WrappingInt32 isn, uint64_t checkpoint) {
    /* assume no overflow */

    // `abs_n` must equal to (some base + `n_minus_isn`)
    uint64_t n_minus_isn = n.raw_value() - isn.raw_value();
    // guess a base
    uint64_t base = checkpoint / FACTOR * FACTOR;
    // `abs_n` and `checkpoint` must locate at the same interval,
    // i.e., `abs_n` / FACTOR == `checkpoint` / FACTOR
    uint64_t abs_n = base + n_minus_isn;
    // check the neighbor interval
    auto dist = [checkpoint](uint64_t x) {
        return x > checkpoint ? x - checkpoint : checkpoint - x;
    };
    if (abs_n > checkpoint && dist(abs_n - FACTOR) < dist(abs_n)) {
        return abs_n - FACTOR;
    } else if (abs_n < checkpoint && dist(abs_n + FACTOR) < dist(abs_n)) {
        return abs_n + FACTOR;
    }
    // if the neighbor is not the target, we choose `abs_n`
    return abs_n;
}
