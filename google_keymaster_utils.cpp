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

Buffer::~Buffer() {
    delete[] buffer_;
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

}  // namespace keymaster
