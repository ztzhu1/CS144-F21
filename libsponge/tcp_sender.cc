#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <iostream>
#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity,
                     const uint16_t retx_timeout,
                     const std::optional<WrappingInt32> fixed_isn)
    : isn_(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , initial_retransmission_timeout_{retx_timeout}
    , rto_{retx_timeout}
    , countdown_{retx_timeout}
    , stream_(capacity) {}

uint64_t TCPSender::bytes_in_flight() const { return bytes_in_flight_; }

void TCPSender::fill_window() {
    if (!sent_syn_) {
        /* send syn */
        assert(window_begin_ == 0);
        assert(window_size_ == 1);
        assert(next_seqno_ == 0);
        connect();
        return;
    }

    /* the data wanted by the window has been sent */
    if (sent_all_) {
        return;
    }
    if (window_begin_ + window_size_ <= next_seqno_) {
        return;
    }
    /* send data */
    uint64_t remaining_window_size = window_begin_ + window_size_ - next_seqno_;
    assert(remaining_window_size > 0);

    if (stream_.eof()) {
        TCPSegment seg;
        auto &header = seg.header();
        header.seqno = next_seqno();
        header.fin = true;
        send(seg);
        ++next_seqno_;
        sent_all_ = true;
        return;
    }

    while (remaining_window_size > 0 && !stream_.buffer_empty() && !sent_all_) {
        /* init */
        TCPSegment seg;
        auto &header = seg.header();
        auto &payload = seg.payload();
        /* read */
        auto max_read_size = std::min(remaining_window_size, TCPConfig::MAX_PAYLOAD_SIZE);
        auto read_size = std::min(max_read_size, stream_.buffer_size());
        string data = stream_.read(read_size);
        /* if eof and there is extra space for eof */
        bool send_eof = false;
        if (stream_.eof() && read_size < remaining_window_size) {
            send_eof = true;
        }
        /* set payload */
        payload = Buffer(std::move(data));
        /* set header */
        header.seqno = next_seqno();
        if (send_eof) {
            header.fin = true;
            sent_all_ = true;
        }
        /* send */
        send(seg);
        /* update meta data */
        remaining_window_size -= (read_size + send_eof);
        next_seqno_ += (read_size + send_eof);
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    /* unwrap */
    size_t abs_ackno = get_abs_seqno(ackno);
    if (abs_ackno > next_seqno_) {
        /* impossible */
        return;
    }
    /* update window */
    window_begin_ = abs_ackno;
    actual_zero_window_size_ = window_size == 0;
    window_size_ = actual_zero_window_size_ ? 1 : window_size;
    /* remove fully acknowledged segments from `outstanding_segs_` */
    while (!outstanding_segs_.empty()) {
        auto &first_seg = outstanding_segs_.front();
        if (first_seg.abs_seqno_at_end() <= abs_ackno) {
            pop_outstanding_seg();
        } else {
            break;
        }
    }
    /* reset timer */
    if (abs_ackno > 1 && abs_ackno > checkpoint_) {
        // Receiver has assembled some data, instead of just
        // receiving syn.
        reset_timer();
        if (!outstanding_segs_.empty()) {
            begin_timing();
        }
    }

    /* update */
    if (abs_ackno > checkpoint_) {
        checkpoint_ = abs_ackno;
    }
    if (outstanding_segs_.empty()) {
        assert(bytes_in_flight_ == 0);
        timing = false;
    }

    // fill_window();
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    if (!timing) {
        assert(outstanding_segs_.empty());
        return;
    }
    assert(!outstanding_segs_.empty());
    assert(!timeout());

    /* not timeout */
    if (countdown_ > ms_since_last_tick) {
        countdown_ -= ms_since_last_tick;
        return;
    }

    /* timeout */
    countdown_ = 0;
    resend(outstanding_segs_.front().segment());
    update_timer_after_timeout();
}

unsigned int TCPSender::consecutive_retransmissions() const { return consecutive_rx_; }

void TCPSender::send_empty_segment() {
    TCPSegment seg;
    seg.header().seqno = wrap(next_seqno_, isn_);
    segments_out_.push(seg);
}

/* -------- private -------- */
void TCPSender::push_outstanding_seg(const TCPSegment &seg) {
    outstanding_segs_.emplace_back(seg, next_seqno_);
    bytes_in_flight_ += seg.length_in_sequence_space();
}

void TCPSender::pop_outstanding_seg() {
    if (outstanding_segs_.empty()) {
        assert(bytes_in_flight_ == 0);
        return;
    }
    bytes_in_flight_ -= outstanding_segs_.front().length_in_sequence_space();
    outstanding_segs_.pop_front();
}

void TCPSender::connect() {
    assert(!sent_syn_);
    assert(window_begin_ == 0);
    assert(window_size_ == 1);
    assert(next_seqno_ == 0);

    TCPSegment seg;
    auto &header = seg.header();
    header.syn = true;
    header.seqno = isn_;

    send(seg);

    sent_syn_ = true;
    next_seqno_ = 1;
}

void TCPSender::send(const TCPSegment &seg) {
    assert(seg.length_in_sequence_space() > 0);
    if (!timing) {
        begin_timing();
    }
    segments_out_.push(seg);
    push_outstanding_seg(seg);
}

void TCPSender::resend(const TCPSegment &seg) {
    assert(seg.length_in_sequence_space() > 0);
    if (!timing) {
        begin_timing();
    }
    segments_out_.push(seg);
}

void TCPSender::begin_timing() {
    countdown_ = rto_;
    timing = true;
    consecutive_rx_ = 0;
}

void TCPSender::reset_timer() {
    rto_ = initial_retransmission_timeout_;
    countdown_ = rto_;
    timing = false;
    consecutive_rx_ = 0;
}

void TCPSender::update_timer_after_timeout() {
    ++consecutive_rx_;
    if (!actual_zero_window_size_) {
        rto_ *= 2;
    }
    countdown_ = rto_;
}