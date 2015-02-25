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

#include <assert.h>
#include <string.h>

#include <cstddef>

#include <openssl/rand.h>
#include <openssl/x509.h>

#include <UniquePtr.h>

#include <keymaster/google_keymaster.h>
#include <keymaster/google_keymaster_utils.h>

#include "ae.h"
#include "key.h"
#include "operation.h"
#include "unencrypted_key_blob.h"

namespace keymaster {

const uint8_t MAJOR_VER = 1;
const uint8_t MINOR_VER = 0;
const uint8_t SUBMINOR_VER = 0;

GoogleKeymaster::GoogleKeymaster(size_t operation_table_size, Logger* logger)
    : operation_table_(new OpTableEntry[operation_table_size]),
      operation_table_size_(operation_table_size), logger_(logger) {
    if (operation_table_.get() == NULL)
        operation_table_size_ = 0;
}

GoogleKeymaster::~GoogleKeymaster() {
    for (size_t i = 0; i < operation_table_size_; ++i)
        if (operation_table_[i].operation != NULL)
            delete operation_table_[i].operation;
}

struct AE_CTX_Delete {
    void operator()(ae_ctx* ctx) const { ae_free(ctx); }
};
typedef UniquePtr<ae_ctx, AE_CTX_Delete> Unique_ae_ctx;

// TODO(swillden): Unify support analysis.  Right now, we have per-keytype methods that determine if
// specific modes, padding, etc. are supported for that key type, and GoogleKeymaster also has
// methods that return the same information.  They'll get out of sync.  Best to put the knowledge in
// the keytypes and provide some mechanism for GoogleKeymaster to query the keytypes for the
// information.
//
// UPDATE: This TODO has been completed for supported algorithms.  It still needs to be done for
// modes, padding, etc.  This will be done with a registry of operation factories.

template <typename T>
bool check_supported(keymaster_algorithm_t algorithm, SupportedResponse<T>* response) {
    if (KeyFactoryRegistry::Get(algorithm) == NULL) {
        response->error = KM_ERROR_UNSUPPORTED_ALGORITHM;
        return false;
    }
    return true;
}

void GoogleKeymaster::GetVersion(const GetVersionRequest&, GetVersionResponse* rsp) {
    if (rsp == NULL)
        return;

    rsp->major_ver = MAJOR_VER;
    rsp->minor_ver = MINOR_VER;
    rsp->subminor_ver = SUBMINOR_VER;
    rsp->error = KM_ERROR_OK;
}

void GoogleKeymaster::SupportedAlgorithms(
    SupportedResponse<keymaster_algorithm_t>* response) const {
    if (response == NULL)
        return;

    size_t factory_count = 0;
    const KeyFactory** factories = KeyFactoryRegistry::GetAll(&factory_count);
    assert(factories != NULL);
    assert(factory_count > 0);

    UniquePtr<keymaster_algorithm_t[]> algorithms(new keymaster_algorithm_t[factory_count]);
    if (!algorithms.get()) {
        response->error = KM_ERROR_MEMORY_ALLOCATION_FAILED;
        return;
    }

    for (size_t i = 0; i < factory_count; ++i)
        algorithms[i] = factories[i]->registry_key();

    response->results = algorithms.release();
    response->results_length = factory_count;
    response->error = KM_ERROR_OK;
}

static OperationFactory* GetOperationFactory(keymaster_algorithm_t algorithm,
                                             keymaster_purpose_t purpose,
                                             keymaster_error_t* error) {
    assert(error);
    if (error)
        *error = KM_ERROR_OK;

    OperationFactory* factory =
        OperationFactoryRegistry::Get(OperationFactory::KeyType(algorithm, purpose));
    if (factory == NULL && error)
        *error = KM_ERROR_UNSUPPORTED_PURPOSE;

    return factory;
}

template <typename T>
void GetSupported(keymaster_algorithm_t algorithm, keymaster_purpose_t purpose,
                  const T* (OperationFactory::*get_supported_method)(size_t* count) const,
                  SupportedResponse<T>* response) {
    if (response == NULL || !check_supported(algorithm, response))
        return;

    OperationFactory* factory = GetOperationFactory(algorithm, purpose, &response->error);
    if (!factory) {
        response->error = KM_ERROR_UNSUPPORTED_PURPOSE;
        return;
    }

    size_t count;
    const T* supported = (factory->*get_supported_method)(&count);
    response->SetResults(supported, count);
}

void GoogleKeymaster::SupportedBlockModes(
    keymaster_algorithm_t algorithm, keymaster_purpose_t purpose,
    SupportedResponse<keymaster_block_mode_t>* response) const {
    GetSupported(algorithm, purpose, &OperationFactory::SupportedBlockModes, response);
}

void GoogleKeymaster::SupportedPaddingModes(
    keymaster_algorithm_t algorithm, keymaster_purpose_t purpose,
    SupportedResponse<keymaster_padding_t>* response) const {
    GetSupported(algorithm, purpose, &OperationFactory::SupportedPaddingModes, response);
}

void GoogleKeymaster::SupportedDigests(keymaster_algorithm_t algorithm, keymaster_purpose_t purpose,
                                       SupportedResponse<keymaster_digest_t>* response) const {
    GetSupported(algorithm, purpose, &OperationFactory::SupportedDigests, response);
}

void GoogleKeymaster::SupportedImportFormats(
    keymaster_algorithm_t algorithm, SupportedResponse<keymaster_key_format_t>* response) const {
    if (response == NULL || !check_supported(algorithm, response))
        return;

    size_t count;
    const keymaster_key_format_t* formats =
        KeyFactoryRegistry::Get(algorithm)->SupportedImportFormats(&count);
    response->SetResults(formats, count);
}

void GoogleKeymaster::SupportedExportFormats(
    keymaster_algorithm_t algorithm, SupportedResponse<keymaster_key_format_t>* response) const {
    if (response == NULL || !check_supported(algorithm, response))
        return;

    size_t count;
    const keymaster_key_format_t* formats =
        KeyFactoryRegistry::Get(algorithm)->SupportedExportFormats(&count);
    response->SetResults(formats, count);
}

void GoogleKeymaster::GenerateKey(const GenerateKeyRequest& request,
                                  GenerateKeyResponse* response) {
    if (response == NULL)
        return;

    keymaster_algorithm_t algorithm;
    KeyFactory* factory = 0;
    UniquePtr<Key> key;
    if (!request.key_description.GetTagValue(TAG_ALGORITHM, &algorithm) ||
        !(factory = KeyFactoryRegistry::Get(algorithm)))
        response->error = KM_ERROR_UNSUPPORTED_ALGORITHM;
    else
        key.reset(factory->GenerateKey(request.key_description, logger(), &response->error));

    if (response->error != KM_ERROR_OK)
        return;

    response->error = SerializeKey(key.get(), origin(), &response->key_blob, &response->enforced,
                                   &response->unenforced);
}

void GoogleKeymaster::GetKeyCharacteristics(const GetKeyCharacteristicsRequest& request,
                                            GetKeyCharacteristicsResponse* response) {
    if (response == NULL)
        return;
    response->error = KM_ERROR_UNKNOWN_ERROR;

    UniquePtr<KeyBlob> blob(
        LoadKeyBlob(request.key_blob, request.additional_params, &(response->error)));
    if (blob.get() == NULL)
        return;

    response->enforced.Reinitialize(blob->enforced());
    response->unenforced.Reinitialize(blob->unenforced());
    response->error = KM_ERROR_OK;
}

void GoogleKeymaster::BeginOperation(const BeginOperationRequest& request,
                                     BeginOperationResponse* response) {
    if (response == NULL)
        return;
    response->op_handle = 0;

    keymaster_algorithm_t algorithm;
    UniquePtr<Key> key(
        LoadKey(request.key_blob, request.additional_params, &algorithm, &response->error));
    if (key.get() == NULL)
        return;

    OperationFactory::KeyType op_type(algorithm, request.purpose);
    OperationFactory* factory = OperationFactoryRegistry::Get(op_type);
    if (!factory) {
        response->error = KM_ERROR_UNSUPPORTED_PURPOSE;
        return;
    }

    UniquePtr<Operation> operation(factory->CreateOperation(*key, logger(), &response->error));
    if (operation.get() == NULL)
        return;

    response->output_params.Clear();
    response->error = operation->Begin(request.additional_params, &response->output_params);
    if (response->error != KM_ERROR_OK)
        return;

    response->error = AddOperation(operation.release(), &response->op_handle);
}

void GoogleKeymaster::UpdateOperation(const UpdateOperationRequest& request,
                                      UpdateOperationResponse* response) {
    if (response == NULL)
        return;

    OpTableEntry* entry = FindOperation(request.op_handle);
    if (entry == NULL) {
        response->error = KM_ERROR_INVALID_OPERATION_HANDLE;
        return;
    }

    response->error = entry->operation->Update(request.additional_params, request.input,
                                               &response->output, &response->input_consumed);
    if (response->error != KM_ERROR_OK) {
        // Any error invalidates the operation.
        DeleteOperation(entry);
    }
}

void GoogleKeymaster::FinishOperation(const FinishOperationRequest& request,
                                      FinishOperationResponse* response) {
    if (response == NULL)
        return;

    OpTableEntry* entry = FindOperation(request.op_handle);
    if (entry == NULL) {
        response->error = KM_ERROR_INVALID_OPERATION_HANDLE;
        return;
    }

    response->error =
        entry->operation->Finish(request.additional_params, request.signature, &response->output);
    DeleteOperation(entry);
}

keymaster_error_t GoogleKeymaster::AbortOperation(const keymaster_operation_handle_t op_handle) {
    OpTableEntry* entry = FindOperation(op_handle);
    if (entry == NULL)
        return KM_ERROR_INVALID_OPERATION_HANDLE;
    keymaster_error_t error = entry->operation->Abort();
    DeleteOperation(entry);
    if (error != KM_ERROR_OK)
        return error;
    return KM_ERROR_OK;
}

void GoogleKeymaster::ExportKey(const ExportKeyRequest& request, ExportKeyResponse* response) {
    if (response == NULL)
        return;

    keymaster_algorithm_t algorithm;
    UniquePtr<Key> to_export(
        LoadKey(request.key_blob, request.additional_params, &algorithm, &response->error));
    if (to_export.get() == NULL)
        return;

    UniquePtr<uint8_t[]> out_key;
    size_t size;
    response->error = to_export->formatted_key_material(request.key_format, &out_key, &size);
    if (response->error == KM_ERROR_OK) {
        response->key_data = out_key.release();
        response->key_data_length = size;
    }
}

void GoogleKeymaster::ImportKey(const ImportKeyRequest& request, ImportKeyResponse* response) {
    if (response == NULL)
        return;

    keymaster_algorithm_t algorithm;
    KeyFactory* factory = 0;
    UniquePtr<Key> key;
    if (!request.key_description.GetTagValue(TAG_ALGORITHM, &algorithm) ||
        !(factory = KeyFactoryRegistry::Get(algorithm)))
        response->error = KM_ERROR_UNSUPPORTED_ALGORITHM;
    else
        key.reset(factory->ImportKey(request.key_description, request.key_format, request.key_data,
                                     request.key_data_length, logger(), &response->error));

    if (response->error != KM_ERROR_OK)
        return;

    response->error = SerializeKey(key.get(), KM_ORIGIN_IMPORTED, &response->key_blob,
                                   &response->enforced, &response->unenforced);
}

keymaster_error_t GoogleKeymaster::SerializeKey(const Key* key, keymaster_key_origin_t origin,
                                                keymaster_key_blob_t* keymaster_blob,
                                                AuthorizationSet* enforced,
                                                AuthorizationSet* unenforced) {
    keymaster_error_t error;

    error = SetAuthorizations(key->authorizations(), origin, enforced, unenforced);
    if (error != KM_ERROR_OK)
        return error;

    AuthorizationSet hidden_auths;
    error = BuildHiddenAuthorizations(key->authorizations(), &hidden_auths);
    if (error != KM_ERROR_OK)
        return error;

    UniquePtr<uint8_t[]> key_material;
    size_t key_material_size;
    error = key->key_material(&key_material, &key_material_size);
    if (error != KM_ERROR_OK)
        return error;

    uint8_t nonce[KeyBlob::NONCE_LENGTH];
    GenerateNonce(nonce, array_size(nonce));

    keymaster_key_blob_t master_key = MasterKey();
    UniquePtr<KeyBlob> blob(new UnencryptedKeyBlob(
        *enforced, *unenforced, hidden_auths, key_material.get(), key_material_size,
        master_key.key_material, master_key.key_material_size, nonce));
    if (blob.get() == NULL)
        return KM_ERROR_MEMORY_ALLOCATION_FAILED;
    if (blob->error() != KM_ERROR_OK)
        return blob->error();

    size_t size = blob->SerializedSize();
    UniquePtr<uint8_t[]> blob_bytes(new uint8_t[size]);
    if (blob_bytes.get() == NULL)
        return KM_ERROR_MEMORY_ALLOCATION_FAILED;

    blob->Serialize(blob_bytes.get(), blob_bytes.get() + size);
    keymaster_blob->key_material_size = size;
    keymaster_blob->key_material = blob_bytes.release();

    return KM_ERROR_OK;
}

Key* GoogleKeymaster::LoadKey(const keymaster_key_blob_t& key,
                              const AuthorizationSet& client_params,
                              keymaster_algorithm_t* algorithm, keymaster_error_t* error) {
    UniquePtr<UnencryptedKeyBlob> blob(LoadKeyBlob(key, client_params, error));
    if (*error != KM_ERROR_OK)
        return NULL;

    *algorithm = blob->algorithm();

    KeyFactory* factory = 0;
    if ((factory = KeyFactoryRegistry::Get(*algorithm)))
        return factory->LoadKey(*blob, logger(), error);
    *error = KM_ERROR_UNSUPPORTED_ALGORITHM;
    return NULL;
}

UnencryptedKeyBlob* GoogleKeymaster::LoadKeyBlob(const keymaster_key_blob_t& key,
                                                 const AuthorizationSet& client_params,
                                                 keymaster_error_t* error) {
    AuthorizationSet hidden;
    BuildHiddenAuthorizations(client_params, &hidden);
    keymaster_key_blob_t master_key = MasterKey();
    UniquePtr<UnencryptedKeyBlob> blob(
        new UnencryptedKeyBlob(key, hidden, master_key.key_material, master_key.key_material_size));
    if (blob.get() == NULL) {
        *error = KM_ERROR_MEMORY_ALLOCATION_FAILED;
        return NULL;
    } else if (blob->error() != KM_ERROR_OK) {
        *error = blob->error();
        return NULL;
    }
    *error = KM_ERROR_OK;
    return blob.release();
}

static keymaster_error_t TranslateAuthorizationSetError(AuthorizationSet::Error err) {
    switch (err) {
    case AuthorizationSet::OK:
        return KM_ERROR_OK;
    case AuthorizationSet::ALLOCATION_FAILURE:
        return KM_ERROR_MEMORY_ALLOCATION_FAILED;
    case AuthorizationSet::MALFORMED_DATA:
        return KM_ERROR_UNKNOWN_ERROR;
    }
    return KM_ERROR_OK;
}

keymaster_error_t GoogleKeymaster::SetAuthorizations(const AuthorizationSet& key_description,
                                                     keymaster_key_origin_t origin,
                                                     AuthorizationSet* enforced,
                                                     AuthorizationSet* unenforced) {
    enforced->Clear();
    unenforced->Clear();
    for (size_t i = 0; i < key_description.size(); ++i) {
        switch (key_description[i].tag) {
        // These cannot be specified by the client.
        case KM_TAG_ROOT_OF_TRUST:
        case KM_TAG_ORIGIN:
            return KM_ERROR_INVALID_TAG;

        // These don't work.
        case KM_TAG_ROLLBACK_RESISTANT:
            return KM_ERROR_UNSUPPORTED_TAG;

        // These are hidden.
        case KM_TAG_APPLICATION_ID:
        case KM_TAG_APPLICATION_DATA:
            break;

        // Everything else we just copy into the appropriate set.
        default:
            AddAuthorization(key_description[i], enforced, unenforced);
            break;
        }
    }

    AddAuthorization(Authorization(TAG_CREATION_DATETIME, java_time(time(NULL))), enforced,
                     unenforced);
    AddAuthorization(Authorization(TAG_ORIGIN, origin), enforced, unenforced);

    if (enforced->is_valid() != AuthorizationSet::OK)
        return TranslateAuthorizationSetError(enforced->is_valid());

    return TranslateAuthorizationSetError(unenforced->is_valid());
}

keymaster_error_t GoogleKeymaster::BuildHiddenAuthorizations(const AuthorizationSet& input_set,
                                                             AuthorizationSet* hidden) {
    keymaster_blob_t entry;
    if (input_set.GetTagValue(TAG_APPLICATION_ID, &entry))
        hidden->push_back(TAG_APPLICATION_ID, entry.data, entry.data_length);
    if (input_set.GetTagValue(TAG_APPLICATION_DATA, &entry))
        hidden->push_back(TAG_APPLICATION_DATA, entry.data, entry.data_length);
    hidden->push_back(RootOfTrustTag());

    return TranslateAuthorizationSetError(hidden->is_valid());
}

void GoogleKeymaster::AddAuthorization(const keymaster_key_param_t& auth,
                                       AuthorizationSet* enforced, AuthorizationSet* unenforced) {
    if (is_enforced(auth.tag))
        enforced->push_back(auth);
    else
        unenforced->push_back(auth);
}

keymaster_error_t GoogleKeymaster::AddOperation(Operation* operation,
                                                keymaster_operation_handle_t* op_handle) {
    UniquePtr<Operation> op(operation);
    if (RAND_bytes(reinterpret_cast<uint8_t*>(op_handle), sizeof(*op_handle)) == 0)
        return KM_ERROR_UNKNOWN_ERROR;
    if (*op_handle == 0) {
        // Statistically this is vanishingly unlikely, which means if it ever happens in practice,
        // it indicates a broken RNG.
        return KM_ERROR_UNKNOWN_ERROR;
    }
    for (size_t i = 0; i < operation_table_size_; ++i) {
        if (operation_table_[i].operation == NULL) {
            operation_table_[i].operation = op.release();
            operation_table_[i].handle = *op_handle;
            return KM_ERROR_OK;
        }
    }
    return KM_ERROR_TOO_MANY_OPERATIONS;
}

GoogleKeymaster::OpTableEntry*
GoogleKeymaster::FindOperation(keymaster_operation_handle_t op_handle) {
    if (op_handle == 0)
        return NULL;

    for (size_t i = 0; i < operation_table_size_; ++i) {
        if (operation_table_[i].handle == op_handle)
            return operation_table_.get() + i;
    }
    return NULL;
}

void GoogleKeymaster::DeleteOperation(OpTableEntry* entry) {
    delete entry->operation;
    entry->operation = NULL;
    entry->handle = 0;
}

}  // namespace keymaster
