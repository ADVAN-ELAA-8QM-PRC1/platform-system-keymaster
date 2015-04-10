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

#ifndef SYSTEM_KEYMASTER_AES_OPERATION_H_
#define SYSTEM_KEYMASTER_AES_OPERATION_H_

#include <openssl/evp.h>

#include "aead_mode_operation.h"
#include "ocb_utils.h"
#include "operation.h"

namespace keymaster {

class AesEvpOperation : public Operation {
  public:
    AesEvpOperation(keymaster_purpose_t purpose, keymaster_block_mode_t block_mode,
                    keymaster_padding_t padding, bool caller_iv, const uint8_t* key,
                    size_t key_size);
    ~AesEvpOperation();

    virtual keymaster_error_t Begin(const AuthorizationSet& input_params,
                                    AuthorizationSet* output_params);
    virtual keymaster_error_t Update(const AuthorizationSet& additional_params, const Buffer& input,
                                     Buffer* output, size_t* input_consumed);
    virtual keymaster_error_t Finish(const AuthorizationSet& additional_params,
                                     const Buffer& signature, Buffer* output);
    virtual keymaster_error_t Abort();

    virtual int evp_encrypt_mode() = 0;

  private:
    keymaster_error_t InitializeCipher();
    keymaster_error_t GetIv(const AuthorizationSet& input_params);
    bool need_iv() const;

    EVP_CIPHER_CTX ctx_;
    const size_t key_size_;
    const keymaster_block_mode_t block_mode_;
    const keymaster_padding_t padding_;
    const bool caller_iv_;
    UniquePtr<uint8_t[]> iv_;
    uint8_t key_[SymmetricKey::MAX_KEY_SIZE];
};

class AesEvpEncryptOperation : public AesEvpOperation {
  public:
    AesEvpEncryptOperation(keymaster_block_mode_t block_mode, keymaster_padding_t padding,
                           bool caller_iv, const uint8_t* key, size_t key_size)
        : AesEvpOperation(KM_PURPOSE_ENCRYPT, block_mode, padding, caller_iv, key, key_size) {}
    int evp_encrypt_mode() { return 1; }
};

class AesEvpDecryptOperation : public AesEvpOperation {
  public:
    AesEvpDecryptOperation(keymaster_block_mode_t block_mode, keymaster_padding_t padding,
                           const uint8_t* key, size_t key_size)
        : AesEvpOperation(KM_PURPOSE_DECRYPT, block_mode, padding,
                          false /* caller_iv -- don't care */, key, key_size) {}

    int evp_encrypt_mode() { return 0; }
};

}  // namespace keymaster

#endif  // SYSTEM_KEYMASTER_AES_OPERATION_H_
