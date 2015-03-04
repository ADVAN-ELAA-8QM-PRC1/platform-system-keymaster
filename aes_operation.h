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

class AesOcbOperation : public AeadModeOperation {
  public:
    static const size_t NONCE_LENGTH = 12;

    AesOcbOperation(keymaster_purpose_t purpose, const uint8_t* key, size_t key_size,
                    size_t chunk_length, size_t tag_length, keymaster_blob_t additional_data)
        : AeadModeOperation(purpose, key, key_size, chunk_length, tag_length, NONCE_LENGTH,
                            additional_data) {}

    virtual keymaster_error_t Abort() {
        /* All cleanup is in the dtor */
        return KM_ERROR_OK;
    }

  protected:
    ae_ctx* ctx() { return ctx_.get(); }

  private:
    virtual keymaster_error_t Initialize(uint8_t* key, size_t key_size, size_t nonce_length,
                                         size_t tag_length);
    virtual keymaster_error_t EncryptChunk(const uint8_t* nonce, size_t nonce_length,
                                           size_t tag_length,
                                           const keymaster_blob_t additional_data, uint8_t* chunk,
                                           size_t chunk_size, Buffer* output);
    virtual keymaster_error_t DecryptChunk(const uint8_t* nonce, size_t nonce_length,
                                           const uint8_t* tag, size_t tag_length,
                                           const keymaster_blob_t additional_data, uint8_t* chunk,
                                           size_t chunk_size, Buffer* output);
    AeCtx ctx_;
};

class AesEvpOperation : public Operation {
  public:
    AesEvpOperation(keymaster_purpose_t purpose, keymaster_block_mode_t block_mode,
                    keymaster_padding_t padding, const uint8_t* key, size_t key_size);
    ~AesEvpOperation();

    virtual keymaster_error_t Begin(const AuthorizationSet& input_params,
                                    AuthorizationSet* output_params);
    virtual keymaster_error_t Update(const AuthorizationSet& additional_params, const Buffer& input,
                                     Buffer* output, size_t* input_consumed);
    virtual keymaster_error_t Finish(const AuthorizationSet& additional_params,
                                     const Buffer& /* signature */, Buffer* output);
    virtual keymaster_error_t Abort();

    virtual int evp_encrypt_mode() = 0;

  private:
    keymaster_error_t InitializeCipher();
    bool need_iv() const;

    EVP_CIPHER_CTX ctx_;
    const size_t key_size_;
    const keymaster_block_mode_t block_mode_;
    const keymaster_padding_t padding_;
    bool cipher_initialized_;
    uint8_t iv_buffered_;
    uint8_t iv_[AES_BLOCK_SIZE];
    uint8_t key_[SymmetricKey::MAX_KEY_SIZE];
};

class AesEvpEncryptOperation : public AesEvpOperation {
  public:
    AesEvpEncryptOperation(keymaster_block_mode_t block_mode, keymaster_padding_t padding,
                           const uint8_t* key, size_t key_size)
        : AesEvpOperation(KM_PURPOSE_ENCRYPT, block_mode, padding, key, key_size) {}
    int evp_encrypt_mode() { return 1; }
};

class AesEvpDecryptOperation : public AesEvpOperation {
  public:
    AesEvpDecryptOperation(keymaster_block_mode_t block_mode, keymaster_padding_t padding,
                           const uint8_t* key, size_t key_size)
        : AesEvpOperation(KM_PURPOSE_DECRYPT, block_mode, padding, key, key_size) {}

    int evp_encrypt_mode() { return 0; }
};

}  // namespace keymaster

#endif  // SYSTEM_KEYMASTER_AES_OPERATION_H_
