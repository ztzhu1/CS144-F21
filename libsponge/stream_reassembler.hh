#ifndef SPONGE_LIBSPONGE_STREAM_REASSEMBLER_HH
#define SPONGE_LIBSPONGE_STREAM_REASSEMBLER_HH

#include "byte_stream.hh"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

//! \brief A class that assembles a series of excerpts from a byte stream (possibly out of order,
//! possibly overlapping) into an in-order byte stream.
class StreamReassembler {
  private:
    // Your code here -- add private members as necessary.

    /**
     * `DataInfo` is not allowed to be empty unless `eof_` == true;
     */
    struct DataInfo {
        std::string data_{};
        size_t index_{0};

        DataInfo(DataInfo &) = delete;
        DataInfo(const DataInfo &) = delete;
        DataInfo(const DataInfo &&) = delete;
        const DataInfo &operator=(DataInfo &) = delete;
        const DataInfo &operator=(const DataInfo &) = delete;

        DataInfo(std::string_view data, size_t index) : data_{data}, index_(index) {}

        DataInfo(DataInfo &&that) {
            data_ = std::move(that.data_);
            index_ = that.index_;
        }

        size_t size() const { return data_.size(); }
        size_t begin() const { return index_; }
        size_t end() const { return index_ + data_.size(); }
    };

    struct Range {
        size_t begin{0};
        size_t end{0};
        size_t size() const { return end - begin; }
    };

    size_t bytes_in_memory() { return unasm_bytes_ + output_.buffer_size(); }
    size_t remaining_memory() { return capacity_ - bytes_in_memory(); }
    void insert_without_overlap(std::string_view data, size_t index);
    void reassemble();
    void check_eof();

    ByteStream output_;  //!< The reassembled in-order byte stream
    size_t capacity_;    //!< The maximum number of bytes
    size_t unasm_bytes_{0};
    size_t next_index_{0};  // `next_index_-1` is the last assembled byte.
    std::map<size_t, DataInfo> datas_{};
    bool eof_{false};
    size_t eof_index_{0};

  public:
    //! \brief Construct a `StreamReassembler` that will store up to `capacity` bytes.
    //! \note This capacity limits both the bytes that have been reassembled,
    //! and those that have not yet been reassembled.
    StreamReassembler(const size_t capacity);

    //! \brief Receive a substring and write any newly contiguous bytes into the stream.
    //!
    //! The StreamReassembler will stay within the memory limits of the `capacity`.
    //! Bytes that would exceed the capacity are silently discarded.
    //!
    //! \param data the substring
    //! \param index indicates the index (place in sequence) of the first byte in `data`
    //! \param eof the last byte of `data` will be the last byte in the entire stream
    void push_substring(const std::string &data, const uint64_t index, const bool eof);

    //! \name Access the reassembled byte stream
    //!@{
    const ByteStream &stream_out() const { return output_; }
    ByteStream &stream_out() { return output_; }
    //!@}

    //! The number of bytes in the substrings stored but not yet reassembled
    //!
    //! \note If the byte at a particular index has been pushed more than once, it
    //! should only be counted once for the purpose of this function.
    size_t unassembled_bytes() const;

    //! \brief Is the internal state empty (other than the output stream)?
    //! \returns `true` if no substrings are waiting to be assembled
    bool empty() const;
};

#endif  // SPONGE_LIBSPONGE_STREAM_REASSEMBLER_HH
