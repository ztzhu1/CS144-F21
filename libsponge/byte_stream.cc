#include "byte_stream.hh"

#include <algorithm>
#include <cassert>
#include <cstring>

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in
// `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity) { buf_.init(capacity); }

size_t ByteStream::write(const string &data) {
    assert(!input_ended_);
    size_t count = 0;
    size_t data_size = data.size();
    size_t remaining_size = remaining_capacity();
    if (data_size <= remaining_size) {
        count = data_size;
    } else {
        count = remaining_size;
    }
    push_str(data, count);
    return count;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    assert(len <= buffer_size());
    return buf_.peek_front(len);
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    buf_.pop_front(len);
    bytes_read_ += len;
    if (buffer_empty() && input_ended_) {
        eof_ = true;
    }
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    // if (eof_) {
    //     return "";
    // }
    assert(len <= buffer_size());
    auto result = peek_output(len);
    pop_output(len);
    return result;
}

void ByteStream::end_input() {
    if (buffer_empty()) {
        eof_ = true;
    }
    input_ended_ = true;
}

bool ByteStream::input_ended() const { return input_ended_; }

size_t ByteStream::buffer_size() const { return buf_.size(); }

bool ByteStream::buffer_empty() const { return buf_.size() == 0; }

bool ByteStream::eof() const { return eof_; }

size_t ByteStream::bytes_written() const { return bytes_written_; }

size_t ByteStream::bytes_read() const { return bytes_read_; }

size_t ByteStream::remaining_capacity() const { return buf_.remaining_size(); }

/* ------- RingBuffer ------- */
RingBuffer::RingBuffer(size_t capacity) { init(capacity); }

RingBuffer::~RingBuffer() {
    assert(inner_data_);
    delete[] inner_data_;
}

void RingBuffer::init(const size_t capacity) {
    capacity_ = capacity;
    inner_data_ = new char[capacity];
}

void RingBuffer::push_back(const std::string &data, const size_t len) {
    assert(remaining_size() >= len);
    assert(data.size() >= len);
    auto data_ptr = const_cast<char *>(data.c_str());
    if (tail_ >= head_) {
        // it must be not full, or the assertion should have failed.
        size_t tail_remaining_size = capacity_ - tail_;
        if (tail_remaining_size >= len) {
            std::memcpy(
                static_cast<void *>(inner_data_ + tail_), static_cast<void *>(data_ptr), len);
        } else {
            std::memcpy(static_cast<void *>(inner_data_ + tail_),
                        static_cast<void *>(data_ptr),
                        tail_remaining_size);
            std::memcpy(static_cast<void *>(inner_data_),
                        static_cast<void *>(data_ptr + tail_remaining_size),
                        len - tail_remaining_size);
        }
    } else {
        std::memcpy(static_cast<void *>(inner_data_ + tail_), static_cast<void *>(data_ptr), len);
    }
    tail_ = (tail_ + len) % capacity_;
    if (tail_ == head_) {
        full_ = true;
    }
}

void RingBuffer::pop_front(const size_t len) {
    assert(size() >= len);
    head_ = (head_ + len) % capacity_;
    if (len > 0) {
        full_ = false;
    }
}

std::string RingBuffer::peek_front(const size_t len) const {
    assert(size() >= len);
    std::string result;
    if (tail_ >= head_ && !full_) {
        std::copy(inner_data_ + head_, inner_data_ + head_ + len, std::back_inserter(result));
    } else {
        size_t head_remaining_size = capacity_ - head_;
        if (head_remaining_size >= len) {
            std::copy(inner_data_ + head_, inner_data_ + head_ + len, std::back_inserter(result));
        } else {
            std::copy(inner_data_ + head_, inner_data_ + capacity_, std::back_inserter(result));
            std::copy(
                inner_data_, inner_data_ + len - head_remaining_size, std::back_inserter(result));
        }
    }
    return result;
}

/* ------- private ------- */
void ByteStream::push_str(const std::string &data, const size_t len) {
    assert(data.size() >= len);
    assert(remaining_capacity() >= len);
    buf_.push_back(data, len);
    bytes_written_ += len;
}
