#ifndef SPONGE_LIBSPONGE_TCP_SENDER_HH
#define SPONGE_LIBSPONGE_TCP_SENDER_HH

#include "byte_stream.hh"
#include "tcp_config.hh"
#include "tcp_segment.hh"
#include "wrapping_integers.hh"

#include <cassert>
#include <functional>
#include <list>
#include <queue>

//! \brief The "sender" part of a TCP implementation.

//! Accepts a ByteStream, divides it up into segments and sends the
//! segments, keeps track of which segments are still in-flight,
//! maintains the Retransmission Timer, and retransmits in-flight
//! segments if the retransmission timer expires.
class TCPSender {
  private:
    class OutstandingSegment {
      public:
        TCPSegment seg_;
        uint64_t abs_seqno_;

      public:
        OutstandingSegment() = delete;
        explicit OutstandingSegment(const TCPSegment &seg, uint64_t abs_seqno)
            : seg_(seg), abs_seqno_(abs_seqno) {}
        uint64_t length_in_sequence_space() { return seg_.length_in_sequence_space(); };
        uint64_t abs_seqno_at_end() { return abs_seqno_ + length_in_sequence_space(); };
        const TCPSegment &segment() { return seg_; };
    };

    void push_outstanding_seg(const TCPSegment &seg);
    void pop_outstanding_seg();
    void connect();
    void send(const TCPSegment &seg);
    void resend(const TCPSegment &seg);
    uint64_t get_next_stream_index() {
        assert(next_seqno_ > 0);
        return next_seqno_ - 1;
    };
    uint64_t get_abs_seqno(WrappingInt32 seqno) { return unwrap(seqno, isn_, checkpoint_); }
    void begin_timing();
    void reset_timer();
    void update_timer_after_timeout();
    bool timeout() { return countdown_ == 0; }

    //! our initial sequence number, the number for our SYN.
    const WrappingInt32 isn_;

    //! outbound queue of segments that the TCPSender wants sent
    std::queue<TCPSegment> segments_out_{};

    //! retransmission timer for the connection
    unsigned int initial_retransmission_timeout_;
    unsigned int rto_;
    unsigned int countdown_;
    bool timing{false};

    //! outgoing stream of bytes that have not yet been sent
    ByteStream stream_;

    //! the (absolute) sequence number for the next byte to be sent
    uint64_t next_seqno_{0};

    bool sent_syn_{false};
    uint64_t window_begin_{0};
    uint16_t window_size_{1};
    bool actual_zero_window_size_{false};
    uint64_t bytes_in_flight_{0};
    std::list<OutstandingSegment> outstanding_segs_{};
    uint64_t checkpoint_{0};
    unsigned int consecutive_rx_{0};
    bool sent_all_{false};

  public:
    //! Initialize a TCPSender
    TCPSender(const size_t capacity = TCPConfig::DEFAULT_CAPACITY,
              const uint16_t retx_timeout = TCPConfig::TIMEOUT_DFLT,
              const std::optional<WrappingInt32> fixed_isn = {});

    //! \name "Input" interface for the writer
    //!@{
    ByteStream &stream_in() { return stream_; }
    const ByteStream &stream_in() const { return stream_; }
    //!@}

    //! \name Methods that can cause the TCPSender to send a segment
    //!@{

    //! \brief A new acknowledgment was received
    void ack_received(const WrappingInt32 ackno, const uint16_t window_size);

    //! \brief Generate an empty-payload segment (useful for creating empty ACK segments)
    void send_empty_segment();

    //! \brief create and send segments to fill as much of the window as possible
    void fill_window();

    //! \brief Notifies the TCPSender of the passage of time
    void tick(const size_t ms_since_last_tick);
    //!@}

    //! \name Accessors
    //!@{

    //! \brief How many sequence numbers are occupied by segments sent but not yet acknowledged?
    //! \note count is in "sequence space," i.e. SYN and FIN each count for one byte
    //! (see TCPSegment::length_in_sequence_space())
    size_t bytes_in_flight() const;

    //! \brief Number of consecutive retransmissions that have occurred in a row
    unsigned int consecutive_retransmissions() const;

    //! \brief TCPSegments that the TCPSender has enqueued for transmission.
    //! \note These must be dequeued and sent by the TCPConnection,
    //! which will need to fill in the fields that are set by the TCPReceiver
    //! (ackno and window size) before sending.
    std::queue<TCPSegment> &segments_out() { return segments_out_; }
    //!@}

    //! \name What is the next sequence number? (used for testing)
    //!@{

    //! \brief absolute seqno for the next byte to be sent
    uint64_t next_seqno_absolute() const { return next_seqno_; }

    //! \brief relative seqno for the next byte to be sent
    WrappingInt32 next_seqno() const { return wrap(next_seqno_, isn_); }
    //!@}
};

#endif  // SPONGE_LIBSPONGE_TCP_SENDER_HH
