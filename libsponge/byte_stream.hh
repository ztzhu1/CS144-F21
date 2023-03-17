#ifndef SPONGE_LIBSPONGE_BYTE_STREAM_HH
#define SPONGE_LIBSPONGE_BYTE_STREAM_HH

#include <string>

class ByteStream;

/**
 * This class doesn't keep the "Modern C++" style
 * which is required in lab document. I want to
 * take it as a practice to use `new[]` and `delete[]`.
 */
class RingBuffer {
    friend class ByteStream;

  private:
    size_t capacity_{0};
    size_t head_{0};
    size_t tail_{0};
    char *inner_data_{nullptr};
    bool full_{false};

    void init(const size_t capacity);

  public:
    RingBuffer() = default;
    explicit RingBuffer(size_t capacity);
    RingBuffer(const RingBuffer &) = delete;
    RingBuffer(const RingBuffer &&) = delete;
    ~RingBuffer();
    const RingBuffer &operator=(const RingBuffer &) = delete;

    size_t size() const { return full_ ? capacity_ : (tail_ - head_ + capacity_) % capacity_; }
    size_t capacity() const { return capacity_; }
    size_t remaining_size() const { return capacity_ - size(); }
    void push_back(const std::string &data, const size_t len);
    std::string peek_front(const size_t len) const;
    void pop_front(const size_t len);
};

//! \brief An in-order byte stream.

//! Bytes are written on the "input" side and read from the "output"
//! side.  The byte stream is finite: the writer can end the input,
//! and then no more bytes can be written.
class ByteStream {
  private:
    // Your code here -- add private members as necessary.

    // Hint: This doesn't need to be a sophisticated data structure at
    // all, but if any of your tests are taking longer than a second,
    // that's a sign that you probably want to keep exploring
    // different approaches.

    void push_str(const std::string &data, const size_t len);

    bool _error{};  //!< Flag indicating that the stream suffered an error.
    size_t bytes_written_{0};
    size_t bytes_read_{0};
    RingBuffer buf_{};
    bool input_ended_{false};
    bool eof_{false};

  public:
    //! Construct a stream with room for `capacity` bytes.
    ByteStream(const size_t capacity);

    //! \name "Input" interface for the writer
    //!@{

    //! Write a string of bytes into the stream. Write as many
    //! as will fit, and return how many were written.
    //! \returns the number of bytes accepted into the stream
    size_t write(const std::string &data);

    //! \returns the number of additional bytes that the stream has space for
    size_t remaining_capacity() const;

    //! Signal that the byte stream has reached its ending
    void end_input();

    //! Indicate that the stream suffered an error.
    void set_error() { _error = true; }
    //!@}

    //! \name "Output" interface for the reader
    //!@{

    //! Peek at next "len" bytes of the stream
    //! \returns a string
    std::string peek_output(const size_t len) const;

    //! Remove bytes from the buffer
    void pop_output(const size_t len);

    //! Read (i.e., copy and then pop) the next "len" bytes of the stream
    //! \returns a string
    std::string read(const size_t len);

    //! \returns `true` if the stream input has ended
    bool input_ended() const;

    //! \returns `true` if the stream has suffered an error
    bool error() const { return _error; }

    //! \returns the maximum amount that can currently be read from the stream
    size_t buffer_size() const;

    //! \returns `true` if the buffer is empty
    bool buffer_empty() const;

    //! \returns `true` if the output has reached the ending
    bool eof() const;
    //!@}

    //! \name General accounting
    //!@{

    //! Total number of bytes written
    size_t bytes_written() const;

    //! Total number of bytes popped
    size_t bytes_read() const;
    //!@}
};

#endif  // SPONGE_LIBSPONGE_BYTE_STREAM_HH
