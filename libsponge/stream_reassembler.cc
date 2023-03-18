#include "stream_reassembler.hh"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <iterator>
#include <vector>

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity)
    : output_(capacity), capacity_(capacity) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    size_t data_size = data.size();
    size_t data_end = index + data_size;

    if (eof) {
        if (eof_) {
            assert(eof_index_ == data_end);
        } else {
            eof_ = true;
            eof_index_ = data_end;
        }
        check_eof();
    }

    if (data_size == 0) {
        /* The data slice doesn't contain any information. */
        return;
    }
    if (data_end <= next_index_) {
        /* The whole data slice is already assembled.
            We can safely return. */
        return;
    }

    /* Some bytes in slice is assembled.
        We just need to store the other part. */
    size_t begin = std::max(index, next_index_);
    size_t len = data_end - begin;
    insert_without_overlap(data.substr(begin - index, len), begin);
    reassemble();
}

size_t StreamReassembler::unassembled_bytes() const { return unasm_bytes_; }

bool StreamReassembler::empty() const { return unasm_bytes_ == 0; }

/* ------- private ------- */

/**
 * Invariant: data.size() > 0
 */
void StreamReassembler::insert_without_overlap(std::string_view data, size_t index) {
    assert(index >= next_index_);
    const size_t data_size = data.size();
    const size_t begin = index;
    const size_t end = begin + data_size;
    std::vector<Range> unneeded_ranges;

    /* get overlap area */
    for (const auto &[existed_begin, data_info] : datas_) {
        const size_t existed_end = data_info.end();
        if (existed_end <= begin) {
            // non overlap
            continue;
        }
        // no overlap anymore
        if (existed_begin >= end) {
            break;
        }
        // there is overlap
        size_t left = std::max(begin, existed_begin);
        size_t right = std::min(end, existed_end);
        unneeded_ranges.emplace_back(Range{left, right});
    }

    /* get needed area */
    size_t insert_size = 0;
    std::vector<Range> needed_ranges;
    size_t prev_end = index;
    for (const auto &unneeded_range : unneeded_ranges) {
        if (unneeded_range.begin > prev_end) {
            Range range{prev_end, unneeded_range.begin};
            if (range.size() != 0) {
                insert_size += range.size();
                needed_ranges.emplace_back(std::move(range));
            }
        }
        prev_end = unneeded_range.end;
    }
    if (prev_end < end) {
        Range range{prev_end, end};
        insert_size += range.size();
        needed_ranges.emplace_back(std::move(range));
    }

    /* insert */
    for (const auto &needed_range : needed_ranges) {
        auto slice_begin = needed_range.begin - index;
        auto slice_size = needed_range.size();
        assert(slice_size != 0);
        datas_.emplace(needed_range.begin, DataInfo{data.substr(slice_begin, slice_size), needed_range.begin});
    }

    /* If the space is not enough, we need to discard the exceeded part. */
    size_t remaining_mem = remaining_memory();
    if (insert_size > remaining_mem) {
        size_t exceeded_size = insert_size - remaining_mem;
        auto rit = datas_.rbegin();
        while (rit != datas_.rend()) {
            size_t rit_data_size = rit->second.size();
            if (rit_data_size > exceeded_size) {
                insert_size -= exceeded_size;
                rit->second.data_ = rit->second.data_.substr(0, rit_data_size - exceeded_size);
                exceeded_size -= exceeded_size;
                break;
            }
            insert_size -= rit_data_size;
            exceeded_size -= rit_data_size;
            rit = decltype(rit){datas_.erase(std::next(rit).base())};
            if (exceeded_size == 0) {
                break;
            }
        }
        assert(exceeded_size == 0);
    }
    assert(insert_size <= remaining_mem);

    unasm_bytes_ += insert_size;
    assert(bytes_in_memory() <= capacity_);
}

void StreamReassembler::reassemble() {
    check_eof();
    if (datas_.empty()) {
        return;
    }
    auto it = datas_.begin();
    assert(it->first >= next_index_);
    while (it != datas_.end() && it->first == next_index_) {
        size_t buf_remaining_capacity = output_.remaining_capacity();
        size_t slice_size = it->second.size();
        if (slice_size <= buf_remaining_capacity) {
            output_.write(it->second.data_);
            unasm_bytes_ -= slice_size;
            next_index_ += slice_size;
            check_eof();
            it = datas_.erase(it);
        } else {
            output_.write(it->second.data_.substr(0, buf_remaining_capacity));
            auto index = it->second.index_;
            auto new_index = index + buf_remaining_capacity;
            it->second.index_ = new_index;
            auto node = datas_.extract(index);
            node.key() = new_index;
            datas_.insert(std::move(node));

            unasm_bytes_ -= buf_remaining_capacity;
            next_index_ += buf_remaining_capacity;
            // buffer is full
            break;
        }
    }
}

void StreamReassembler::check_eof() {
    if (eof_ && next_index_ == eof_index_) {
        output_.end_input();
    }
}