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

#ifndef SYSTEM_KEYMASTER_GOOGLE_KEYMASTER_MESSAGES_H_
#define SYSTEM_KEYMASTER_GOOGLE_KEYMASTER_MESSAGES_H_

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <keymaster/authorization_set.h>
#include <keymaster/google_keymaster_utils.h>

namespace keymaster {

// Commands
const uint32_t GENERATE_KEY = 0;
const uint32_t BEGIN_OPERATION = 1;
const uint32_t UPDATE_OPERATION = 2;
const uint32_t FINISH_OPERATION = 3;
const uint32_t ABORT_OPERATION = 4;
const uint32_t IMPORT_KEY = 5;
const uint32_t EXPORT_KEY = 6;
const uint32_t GET_VERSION = 7;

/**
 * Keymaster message versions are tied to keymaster versions.  We map the keymaster
 * major.minor.subminor version to a sequential "message version".
 *
 * Rather than encoding a version number into each message we rely on the client -- who initiates
 * all requests -- to check the version of the keymaster implementation with the GET_VERSION command
 * and to send only requests that the implementation can understand.  This means that only the
 * client side needs to manage version compatibility; the implementation can always expect/produce
 * messages of its format.
 *
 * Because message version selection is purely a client-side issue, all messages default to using
 * the latest version (MAX_MESSAGE_VERSION).  Client code must take care to check versions and pass
 * correct version values to message constructors.  The GoogleKeymaster implementation always uses
 * the default, latest.
 *
 * Note that this approach implies that GetVersionRequest and GetVersionResponse cannot be
 * versioned.
 */
const int32_t MAX_MESSAGE_VERSION = 1;
inline int32_t MessageVersion(uint8_t major_ver, uint8_t minor_ver, uint8_t subminor_ver) {
    uint32_t composite_ver = (major_ver << 16) | (minor_ver << 8) | subminor_ver;
    switch (composite_ver) {
    case 0x000:
        return 0;
    case 0x100:
        return 1;
    default:
        return -1;
    }
}

struct KeymasterMessage : public Serializable {
    KeymasterMessage(int32_t ver) : message_version(ver) { assert(ver >= 0); }
    uint32_t message_version;
};

/**
 * All responses include an error value, and if the error is not KM_ERROR_OK, return no additional
 * data.  This abstract class factors out the common serialization functionality for all of the
 * responses, so we only have to implement it once.  Inheritance for reuse is generally not a great
 * structure, but in this case it's the cleanest option.
 */
struct KeymasterResponse : public KeymasterMessage {
    KeymasterResponse(int32_t ver) : KeymasterMessage(ver), error(KM_ERROR_UNKNOWN_ERROR) {}

    size_t SerializedSize() const;
    uint8_t* Serialize(uint8_t* buf, const uint8_t* end) const;
    bool Deserialize(const uint8_t** buf_ptr, const uint8_t* end);

    virtual size_t NonErrorSerializedSize() const = 0;
    virtual uint8_t* NonErrorSerialize(uint8_t* buf, const uint8_t* end) const = 0;
    virtual bool NonErrorDeserialize(const uint8_t** buf_ptr, const uint8_t* end) = 0;

    keymaster_error_t error;
};

struct SupportedAlgorithmsResponse : public KeymasterResponse {
    SupportedAlgorithmsResponse(int32_t ver = MAX_MESSAGE_VERSION)
        : KeymasterResponse(ver), algorithms(NULL), algorithms_length(0) {}
    ~SupportedAlgorithmsResponse() { delete[] algorithms; }

    size_t NonErrorSerializedSize() const;
    uint8_t* NonErrorSerialize(uint8_t* buf, const uint8_t* end) const;
    bool NonErrorDeserialize(const uint8_t** buf_ptr, const uint8_t* end);

    keymaster_algorithm_t* algorithms;
    size_t algorithms_length;
};

template <typename T> struct SupportedResponse : public KeymasterResponse {
    SupportedResponse(int32_t ver = MAX_MESSAGE_VERSION)
        : KeymasterResponse(ver), results(NULL), results_length(0) {}
    ~SupportedResponse() { delete[] results; }

    template <size_t N> void SetResults(const T(&arr)[N]) {
        SetResults(arr, N);
    }

    void SetResults(const T* arr, size_t n) {
        delete[] results;
        results_length = 0;
        results = dup_array(arr, n);
        if (results == NULL) {
            error = KM_ERROR_MEMORY_ALLOCATION_FAILED;
        } else {
            results_length = n;
            error = KM_ERROR_OK;
        }
    }

    size_t NonErrorSerializedSize() const {
        return sizeof(uint32_t) + results_length * sizeof(uint32_t);
    }
    uint8_t* NonErrorSerialize(uint8_t* buf, const uint8_t* end) const {
        return append_uint32_array_to_buf(buf, end, results, results_length);
    }
    bool NonErrorDeserialize(const uint8_t** buf_ptr, const uint8_t* end) {
        delete[] results;
        results = NULL;
        UniquePtr<T[]> tmp;
        if (!copy_uint32_array_from_buf(buf_ptr, end, &tmp, &results_length))
            return false;
        results = tmp.release();
        return true;
    }

    T* results;
    size_t results_length;
};

struct GenerateKeyRequest : public KeymasterMessage {
    GenerateKeyRequest(int32_t ver = MAX_MESSAGE_VERSION) : KeymasterMessage(ver) {}

    size_t SerializedSize() const { return key_description.SerializedSize(); }
    uint8_t* Serialize(uint8_t* buf, const uint8_t* end) const {
        return key_description.Serialize(buf, end);
    }
    bool Deserialize(const uint8_t** buf_ptr, const uint8_t* end) {
        return key_description.Deserialize(buf_ptr, end);
    }

    AuthorizationSet key_description;
};

struct GenerateKeyResponse : public KeymasterResponse {
    GenerateKeyResponse(int32_t ver = MAX_MESSAGE_VERSION) : KeymasterResponse(ver) {
        key_blob.key_material = NULL;
        key_blob.key_material_size = 0;
    }
    ~GenerateKeyResponse();

    size_t NonErrorSerializedSize() const;
    uint8_t* NonErrorSerialize(uint8_t* buf, const uint8_t* end) const;
    bool NonErrorDeserialize(const uint8_t** buf_ptr, const uint8_t* end);

    keymaster_key_blob_t key_blob;
    AuthorizationSet enforced;
    AuthorizationSet unenforced;
};

struct GetKeyCharacteristicsRequest : public KeymasterMessage {
    GetKeyCharacteristicsRequest(int32_t ver = MAX_MESSAGE_VERSION) : KeymasterMessage(ver) {
        key_blob.key_material = NULL;
    }
    ~GetKeyCharacteristicsRequest();

    void SetKeyMaterial(const void* key_material, size_t length);
    void SetKeyMaterial(const keymaster_key_blob_t& blob) {
        SetKeyMaterial(blob.key_material, blob.key_material_size);
    }

    size_t SerializedSize() const;
    uint8_t* Serialize(uint8_t* buf, const uint8_t* end) const;
    bool Deserialize(const uint8_t** buf_ptr, const uint8_t* end);

    keymaster_key_blob_t key_blob;
    AuthorizationSet additional_params;
};

struct GetKeyCharacteristicsResponse : public KeymasterResponse {
    GetKeyCharacteristicsResponse(int32_t ver = MAX_MESSAGE_VERSION) : KeymasterResponse(ver) {}
    size_t NonErrorSerializedSize() const;
    uint8_t* NonErrorSerialize(uint8_t* buf, const uint8_t* end) const;
    bool NonErrorDeserialize(const uint8_t** buf_ptr, const uint8_t* end);

    AuthorizationSet enforced;
    AuthorizationSet unenforced;
};

struct BeginOperationRequest : public KeymasterMessage {
    BeginOperationRequest(int32_t ver = MAX_MESSAGE_VERSION) : KeymasterMessage(ver) {
        key_blob.key_material = NULL;
    }
    ~BeginOperationRequest() { delete[] key_blob.key_material; }

    void SetKeyMaterial(const void* key_material, size_t length);
    void SetKeyMaterial(const keymaster_key_blob_t& blob) {
        SetKeyMaterial(blob.key_material, blob.key_material_size);
    }

    size_t SerializedSize() const;
    uint8_t* Serialize(uint8_t* buf, const uint8_t* end) const;
    bool Deserialize(const uint8_t** buf_ptr, const uint8_t* end);

    keymaster_purpose_t purpose;
    keymaster_key_blob_t key_blob;
    AuthorizationSet additional_params;
};

struct BeginOperationResponse : public KeymasterResponse {
    BeginOperationResponse(int32_t ver = MAX_MESSAGE_VERSION) : KeymasterResponse(ver) {}

    size_t NonErrorSerializedSize() const;
    uint8_t* NonErrorSerialize(uint8_t* buf, const uint8_t* end) const;
    bool NonErrorDeserialize(const uint8_t** buf_ptr, const uint8_t* end);

    keymaster_operation_handle_t op_handle;
    AuthorizationSet output_params;
};

struct UpdateOperationRequest : public KeymasterMessage {
    UpdateOperationRequest(int32_t ver = MAX_MESSAGE_VERSION) : KeymasterMessage(ver) {}

    size_t SerializedSize() const;
    uint8_t* Serialize(uint8_t* buf, const uint8_t* end) const;
    bool Deserialize(const uint8_t** buf_ptr, const uint8_t* end);

    keymaster_operation_handle_t op_handle;
    Buffer input;
    AuthorizationSet additional_params;
};

struct UpdateOperationResponse : public KeymasterResponse {
    UpdateOperationResponse(int32_t ver = MAX_MESSAGE_VERSION)
        : KeymasterResponse(ver), input_consumed(0) {}

    size_t NonErrorSerializedSize() const;
    uint8_t* NonErrorSerialize(uint8_t* buf, const uint8_t* end) const;
    bool NonErrorDeserialize(const uint8_t** buf_ptr, const uint8_t* end);

    Buffer output;
    size_t input_consumed;
};

struct FinishOperationRequest : public KeymasterMessage {
    FinishOperationRequest(int32_t ver = MAX_MESSAGE_VERSION) : KeymasterMessage(ver) {}

    size_t SerializedSize() const;
    uint8_t* Serialize(uint8_t* buf, const uint8_t* end) const;
    bool Deserialize(const uint8_t** buf_ptr, const uint8_t* end);

    keymaster_operation_handle_t op_handle;
    Buffer signature;
    AuthorizationSet additional_params;
};

struct FinishOperationResponse : public KeymasterResponse {
    FinishOperationResponse(int32_t ver = MAX_MESSAGE_VERSION) : KeymasterResponse(ver) {}

    size_t NonErrorSerializedSize() const;
    uint8_t* NonErrorSerialize(uint8_t* buf, const uint8_t* end) const;
    bool NonErrorDeserialize(const uint8_t** buf_ptr, const uint8_t* end);

    Buffer output;
};

struct AddEntropyRequest : public KeymasterMessage {
    AddEntropyRequest(int32_t ver = MAX_MESSAGE_VERSION) : KeymasterMessage(ver) {}

    size_t SerializedSize() const;
    uint8_t* Serialize(uint8_t* buf, const uint8_t* end) const;
    bool Deserialize(const uint8_t** buf_ptr, const uint8_t* end);

    Buffer random_data;
};

struct ImportKeyRequest : public KeymasterMessage {
    ImportKeyRequest(int32_t ver = MAX_MESSAGE_VERSION) : KeymasterMessage(ver), key_data(NULL) {}
    ~ImportKeyRequest() { delete[] key_data; }

    void SetKeyMaterial(const void* key_material, size_t length);

    size_t SerializedSize() const;
    uint8_t* Serialize(uint8_t* buf, const uint8_t* end) const;
    bool Deserialize(const uint8_t** buf_ptr, const uint8_t* end);

    AuthorizationSet key_description;
    keymaster_key_format_t key_format;
    uint8_t* key_data;
    size_t key_data_length;
};

struct ImportKeyResponse : public KeymasterResponse {
    ImportKeyResponse(int32_t ver = MAX_MESSAGE_VERSION) : KeymasterResponse(ver) {
        key_blob.key_material = NULL;
    }
    ~ImportKeyResponse() { delete[] key_blob.key_material; }

    void SetKeyMaterial(const void* key_material, size_t length);
    void SetKeyMaterial(const keymaster_key_blob_t& blob) {
        SetKeyMaterial(blob.key_material, blob.key_material_size);
    }

    size_t NonErrorSerializedSize() const;
    uint8_t* NonErrorSerialize(uint8_t* buf, const uint8_t* end) const;
    bool NonErrorDeserialize(const uint8_t** buf_ptr, const uint8_t* end);

    keymaster_key_blob_t key_blob;
    AuthorizationSet enforced;
    AuthorizationSet unenforced;
};

struct ExportKeyRequest : public KeymasterMessage {
    ExportKeyRequest(int32_t ver = MAX_MESSAGE_VERSION) : KeymasterMessage(ver) {
        key_blob.key_material = NULL;
    }
    ~ExportKeyRequest() { delete[] key_blob.key_material; }

    void SetKeyMaterial(const void* key_material, size_t length);
    void SetKeyMaterial(const keymaster_key_blob_t& blob) {
        SetKeyMaterial(blob.key_material, blob.key_material_size);
    }

    size_t SerializedSize() const;
    uint8_t* Serialize(uint8_t* buf, const uint8_t* end) const;
    bool Deserialize(const uint8_t** buf_ptr, const uint8_t* end);

    AuthorizationSet additional_params;
    keymaster_key_format_t key_format;
    keymaster_key_blob_t key_blob;
};

struct ExportKeyResponse : public KeymasterResponse {
    ExportKeyResponse(int32_t ver = MAX_MESSAGE_VERSION) : KeymasterResponse(ver), key_data(NULL) {}
    ~ExportKeyResponse() { delete[] key_data; }

    void SetKeyMaterial(const void* key_material, size_t length);

    size_t NonErrorSerializedSize() const;
    uint8_t* NonErrorSerialize(uint8_t* buf, const uint8_t* end) const;
    bool NonErrorDeserialize(const uint8_t** buf_ptr, const uint8_t* end);

    uint8_t* key_data;
    size_t key_data_length;
};

struct GetVersionRequest : public KeymasterMessage {
    GetVersionRequest() : KeymasterMessage(0 /* not versionable */) {}

    size_t SerializedSize() const { return 0; }
    uint8_t* Serialize(uint8_t* buf, const uint8_t*) const { return buf; }
    bool Deserialize(const uint8_t**, const uint8_t*) { return true; };
};

struct GetVersionResponse : public KeymasterResponse {
    GetVersionResponse()
        : KeymasterResponse(0 /* not versionable */), major_ver(0), minor_ver(0), subminor_ver(0) {}

    size_t NonErrorSerializedSize() const;
    uint8_t* NonErrorSerialize(uint8_t* buf, const uint8_t* end) const;
    bool NonErrorDeserialize(const uint8_t** buf_ptr, const uint8_t* end);

    uint8_t major_ver;
    uint8_t minor_ver;
    uint8_t subminor_ver;
};

// The structs below are trivial because they're not implemented yet.
struct RescopeRequest : public KeymasterMessage {};
struct RescopeResponse : public KeymasterResponse {};

}  // namespace keymaster

#endif  // SYSTEM_KEYMASTER_GOOGLE_KEYMASTER_MESSAGES_H_
