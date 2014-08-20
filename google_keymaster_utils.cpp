/*
 * Copyright 2014 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "google_keymaster_utils.h"

namespace keymaster {

uint8_t* dup_buffer(const void* buf, size_t size) {
    uint8_t* retval = new uint8_t[size];
    if (retval != NULL)
        memcpy(retval, buf, size);
    return retval;
}

Buffer::~Buffer() {
    delete[] buffer_;
}

bool Buffer::reserve(size_t size) {
    if (available_write() < size) {
        size_t new_size = buffer_size_ + size - available_write();
        uint8_t* new_buffer = new uint8_t[new_size];
        if (!new_buffer)
            return false;
        memcpy(new_buffer, buffer_ + read_position_, available_read());
        buffer_ = new_buffer;
        buffer_size_ = new_size;
        write_position_ -= read_position_;
        read_position_ = 0;
    }
    return true;
}

bool Buffer::Reinitialize(size_t size) {
    delete[] buffer_;

    buffer_ = new uint8_t[size];
    if (buffer_ == NULL)
        return false;
    buffer_size_ = size;
    read_position_ = 0;
    write_position_ = 0;
    return true;
}

bool Buffer::Reinitialize(const void* data, size_t data_len) {
    delete[] buffer_;

    buffer_ = new uint8_t[data_len];
    if (buffer_ == NULL)
        return false;
    buffer_size_ = data_len;
    memcpy(buffer_, data, data_len);
    read_position_ = 0;
    write_position_ = buffer_size_;
    return true;
}

size_t Buffer::available_write() const {
    return buffer_size_ - write_position_;
}

size_t Buffer::available_read() const {
    return write_position_ - read_position_;
}

bool Buffer::write(const uint8_t* src, size_t write_length) {
    if (available_write() < write_length)
        return false;
    memcpy(buffer_ + write_position_, src, write_length);
    write_position_ += write_length;
    return true;
}

bool Buffer::read(uint8_t* dest, size_t read_length) {
    if (available_read() < read_length)
        return false;
    memcpy(dest, buffer_ + read_position_, read_length);
    read_position_ += read_length;
    return true;
}

size_t Buffer::SerializedSize() const {
    return sizeof(uint32_t) + available_read();
}

uint8_t* Buffer::Serialize(uint8_t* buf, const uint8_t* end) const {
    return append_size_and_data_to_buf(buf, end, peek_read(), available_read());
}

bool Buffer::Deserialize(const uint8_t** buf_ptr, const uint8_t* end) {
    delete[] buffer_;
    if (!copy_size_and_data_from_buf(buf_ptr, end, &buffer_size_, &buffer_))
        return false;
    read_position_ = 0;
    write_position_ = buffer_size_;
    return true;
}

int memcmp_s(const void* p1, const void* p2, size_t length) {
    const uint8_t* s1 = static_cast<const uint8_t*>(p1);
    const uint8_t* s2 = static_cast<const uint8_t*>(p2);
    uint8_t result = 0;
    while (length-- > 0)
        result |= *s1++ ^ *s2++;
    return result == 0 ? 0 : 1;
}

}  // namespace keymaster
