#include "tcp_connection.hh"

#include <iostream>
#include <limits>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const {
    return sender_.stream_in().remaining_capacity();
}

size_t TCPConnection::bytes_in_flight() const { return sender_.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return receiver_.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const {
    return time_since_last_segment_received_;
}

void TCPConnection::segment_received(const TCPSegment &seg) {
    time_since_last_segment_received_ = 0;

    auto const &header = seg.header();
    if (header.rst) {
        cerr << "Warning: Received rst, unclean shutdown of TCPConnection\n";
        end_uncleanly();
        return;
    }

    /* give the segmet to receiver */
    receiver_.segment_received(seg);
    // try_to_active();
    if (!receiver_.ackno().has_value()) {
        return;
    }
    check_not_need_to_linger();

    /* respond to "keep-alive" segment */
    if (seg.length_in_sequence_space() == 0 && receiver_.ackno().has_value() &&
        header.seqno == receiver_.ackno().value() - 1) {
        sender_.send_empty_segment();
    }
    /* If something is acknowledged, tell sender.
        In my opinion, `header.ack` is always true
        unless it's the first time that peer send
        a message to us in order to connect with us
        (instead of the first time to respond to us). */
    if (header.ack) {
        sender_.ack_received(header.ackno, header.win);
        if (sent_fin_ && sender_.get_abs_seqno(header.ackno) == abs_fin_seqno_ + 1) {
            fin_acked_ = true;
        }
    }

    /* Send something regardless of receiving ack or not.
       If not, the sender should send syn. */
    sender_.fill_window();
    /* We respond to every valid incoming segment, except
       when we are closing. */
    if (sender_.segments_out().empty() && seg.length_in_sequence_space() != 0) {
        sender_.send_empty_segment();
    }
    /* Send all the segments that sender want to send,
       combining with receiver's information */
    send_all();

    try_to_end_cleanly();
}

bool TCPConnection::active() const { return active_; }

size_t TCPConnection::write(const string &data) {
    size_t len = sender_.stream_in().write(data);
    sender_.fill_window();
    send_all();

    return len;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    time_since_last_segment_received_ += ms_since_last_tick;

    sender_.tick(ms_since_last_tick);
    if (sender_.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS) {
        send_rst();
        cerr << "Warning: Sent rst, unclean shutdown of TCPConnection\n";
        end_uncleanly();
        return;
    }
    send_all();

    if (linger_after_streams_finish_ && linger_begin_) {
        if (time_since_last_segment_received_ >= 10 * cfg_.rt_timeout) {
            linger_after_streams_finish_ = false;
            end_cleanly();
        }
    }
}

void TCPConnection::end_input_stream() {
    sender_.stream_in().end_input();
    sender_.fill_window();
    send_all();
}

void TCPConnection::connect() {
    auto &segs_out = sender_.segments_out();
    assert(segs_out.empty());
    assert(segments_out_.empty());
    sender_.fill_window();

    auto &seg_out = segs_out.front();
    auto &out_header = seg_out.header();
    set_win(out_header);

    segments_out_.push(seg_out);
    segs_out.pop();
    assert(segs_out.empty());
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the peer
            send_rst();
            end_uncleanly();
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}

/* -------- private -------- */
void TCPConnection::set_win(TCPHeader &header) {
    auto win_size = receiver_.window_size();
    auto ceil = std::numeric_limits<decltype(header.win)>::max();
    if (win_size > static_cast<decltype(win_size)>(ceil)) {
        header.win = ceil;
    } else {
        header.win = static_cast<decltype(header.win)>(win_size);
    }
}

void TCPConnection::send_rst() {
    if(!receiver_.ackno().has_value()){
        return;
    }
    TCPSegment empty_seg;
    auto &header = empty_seg.header();

    header.rst = true;
    header.seqno = sender_.next_seqno();
    header.ack = true;
    assert(receiver_.ackno().has_value());
    header.ackno = receiver_.ackno().value();
    set_win(header);

    segments_out_.push(empty_seg);
}

void TCPConnection::send_all() {
    if (!receiver_.ackno().has_value()) {
        return;
    }
    auto &segs_out = sender_.segments_out();
    while (!segs_out.empty()) {
        auto &seg_out = segs_out.front();
        auto &out_header = seg_out.header();
        /* set out_header */
        out_header.ack = true;
        assert(receiver_.ackno().has_value());
        out_header.ackno = receiver_.ackno().value();
        set_win(out_header);
        /* record fin */
        if (out_header.fin) {
            assert(sender_.stream_in().eof());
            sent_fin_ = true;
            abs_fin_seqno_ = 1 + sender_.stream_in().bytes_read() + 1 - 1;  // syn|data|fin
        }
        /* send */
        segments_out_.push(seg_out);
        /* clean queue */
        segs_out.pop();
    }
}

void TCPConnection::end_cleanly() {
    if (!linger_after_streams_finish_) {
        /* connection is done immediately */
        sender_.stream_in().end_input();
        receiver_.stream_out().end_input();
        active_ = false;
    } else {
        linger_begin_ = true;
    }
}

void TCPConnection::end_uncleanly() {
    sender_.stream_in().set_error();
    receiver_.stream_out().set_error();
    linger_begin_ = false;
    linger_after_streams_finish_ = false;
    active_ = false;
}

/**
 * Receiver has received `fin` and has reassembled all segments.
 */
bool TCPConnection::satisfy_prereq_1() { return receiver_.stream_out().eof(); }

/**
 * Application finished inputting and sender has sent
 * all the segments and fin (may be not acknowledged yet).
 */
bool TCPConnection::satisfy_prereq_2() { return sender_.stream_in().eof() && sent_fin_; }

/**
 * The peer acknowledged fin.
 */
bool TCPConnection::satisfy_prereq_3() { return fin_acked_; }

bool TCPConnection::satisfy_prereq_123() {
    return satisfy_prereq_1() && satisfy_prereq_2() && satisfy_prereq_3();
}
void TCPConnection::check_not_need_to_linger() {
    if (receiver_.stream_out().eof() && !sender_.stream_in().eof()) {
        linger_after_streams_finish_ = false;
    }
}

void TCPConnection::try_to_end_cleanly() {
    if (satisfy_prereq_123()) {
        end_cleanly();
    }
}
