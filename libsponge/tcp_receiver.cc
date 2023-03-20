#include "tcp_receiver.hh"

#include <cassert>

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    const auto &header = seg.header();

    if (!received_syn_) {
        /* invalid segment */
        if (!header.syn) {
            return;
        }
        /* the first time to receive syn, recording `isn_` */
        received_syn_ = true;
        isn_ = header.seqno;
    }
    /* decode seqno */
    uint64_t stream_index = 0;
    if (get_abs_seqno(header.seqno) != 0) {
        /* not the first segment */
        stream_index = get_stream_index(header.seqno, true);
    } else if (!header.syn) {
        // Header send a abs_seqno == 0 but it's not the beginning of stream,
        // from which we can infer that it's an invalid segment.
        return;
    }
    /* handle fin */
    const auto &payload = seg.payload();
    if (header.fin) {
        uint64_t abs_fin_seqno = stream_index_to_abs_seqno(stream_index) + payload.size();
        if (!received_fin_) {
            /* the first time to receive fin, recording `abs_fin_seq_` */
            received_fin_ = true;
            abs_fin_seqno_ = abs_fin_seqno;
        } else {
            assert(abs_fin_seqno_ == abs_fin_seqno);
        }
    }

    /* push_segment inside the window */
    auto win_begin = get_abs_ackno();
    auto win_end = win_begin + window_size();
    if (stream_index + 1 + seg.payload().size() <= win_end) {
        reassembler_.push_substring(seg.payload().copy(), stream_index, header.fin);
    } else if (stream_index + 1 < win_end) {
        reassembler_.push_substring(
            seg.payload().copy().substr(0, win_end - (stream_index + 1)), stream_index, header.fin);
    }
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    if (!received_syn_) {
        return nullopt;
    }
    return wrap(get_abs_ackno(), isn_);
}

size_t TCPReceiver::window_size() const {
    return capacity_ - reassembler_.stream_out().buffer_size();
}

/* -------- private -------- */

uint64_t TCPReceiver::get_abs_ackno() const {
    size_t bytes_written = reassembler_.stream_out().bytes_written();
    size_t abs_ackno = 1 + bytes_written;  // syn | bytes
    // check fin
    if (received_fin_ && abs_ackno == abs_fin_seqno_) {
        ++abs_ackno;
    }
    return abs_ackno;
}

uint64_t TCPReceiver::get_stream_index(WrappingInt32 seqno, bool update_cp) {
    uint64_t abs_seqno = get_abs_seqno(seqno);
    assert(abs_seqno != 0);
    if (update_cp) {
        checkpoint_ = abs_seqno;
    }
    return abs_seqno - 1;
}