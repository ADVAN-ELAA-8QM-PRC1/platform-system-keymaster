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

#ifndef SYSTEM_KEYMASTER_ANDROID_SOFT_KEYMASTER_H_
#define SYSTEM_KEYMASTER_ANDROID_SOFT_KEYMASTER_H_

#include <keymaster/android_keymaster.h>

#include <openssl/rand.h>

#include "openssl_err.h"

namespace keymaster {

class AndroidSoftKeymaster : public AndroidKeymaster {
  public:
    AndroidSoftKeymaster(size_t operation_table_size) : AndroidKeymaster(operation_table_size) {
        root_of_trust.tag = KM_TAG_ROOT_OF_TRUST;
        root_of_trust.blob.data = reinterpret_cast<const uint8_t*>("SW");
        root_of_trust.blob.data_length = 2;
    }
    bool is_enforced(keymaster_tag_t /* tag */) { return false; }
    bool is_hardware() { return false; }

    keymaster_error_t AddRngEntropy(const AddEntropyRequest& request) {
        RAND_add(request.random_data.peek_read(), request.random_data.available_read(),
                 0 /* Don't assume any entropy is added to the pool. */);
        return KM_ERROR_OK;
    }

  private:
    static uint8_t master_key_[];

    keymaster_key_blob_t MasterKey() {
        keymaster_key_blob_t blob;
        blob.key_material = master_key_;
        blob.key_material_size = 16;
        return blob;
    }

    void GenerateNonce(uint8_t* nonce, size_t length) {
        for (size_t i = 0; i < length; ++i)
            nonce[i] = 0;
    }

    keymaster_key_param_t RootOfTrustTag() { return root_of_trust; }

    keymaster_key_param_t root_of_trust;
};

uint8_t AndroidSoftKeymaster::master_key_[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

}  // namespace

#endif  // SYSTEM_KEYMASTER_ANDROID_SOFT_KEYMASTER_H_
