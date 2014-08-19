/*
 * Copyright (C) 2014 The Android Open Source Project
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

#include <gtest/gtest.h>

#include <openssl/engine.h>

#include "google_keymaster_test_utils.h"
#include "google_keymaster_utils.h"
#include "google_softkeymaster.h"
#include "keymaster_tags.h"

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    // Clean up stuff OpenSSL leaves around, so Valgrind doesn't complain.
    CRYPTO_cleanup_all_ex_data();
    ERR_free_strings();
    return result;
}

namespace keymaster {
namespace test {

class KeymasterTest : public testing::Test {
  protected:
    KeymasterTest() : device(5) { RAND_seed("foobar", 6); }
    ~KeymasterTest() {}

    GoogleSoftKeymaster device;
};

template <keymaster_tag_t Tag, typename KeymasterEnum>
bool contains(const AuthorizationSet& set, TypedEnumTag<KM_ENUM, Tag, KeymasterEnum> tag,
              KeymasterEnum val) {
    int pos = set.find(tag);
    return pos != -1 && set[pos].enumerated == val;
}

template <keymaster_tag_t Tag, typename KeymasterEnum>
bool contains(const AuthorizationSet& set, TypedEnumTag<KM_ENUM_REP, Tag, KeymasterEnum> tag,
              KeymasterEnum val) {
    int pos = -1;
    while ((pos = set.find(tag, pos)) != -1)
        if (set[pos].enumerated == val)
            return true;
    return false;
}

template <keymaster_tag_t Tag>
bool contains(const AuthorizationSet& set, TypedTag<KM_INT, Tag> tag, uint32_t val) {
    int pos = set.find(tag);
    return pos != -1 && set[pos].integer == val;
}

template <keymaster_tag_t Tag>
bool contains(const AuthorizationSet& set, TypedTag<KM_INT_REP, Tag> tag, uint32_t val) {
    int pos = -1;
    while ((pos = set.find(tag, pos)) != -1)
        if (set[pos].integer == val)
            return true;
    return false;
}

template <keymaster_tag_t Tag>
bool contains(const AuthorizationSet& set, TypedTag<KM_LONG, Tag> tag, uint64_t val) {
    int pos = set.find(tag);
    return pos != -1 && set[pos].long_integer == val;
}

template <keymaster_tag_t Tag>
bool contains(const AuthorizationSet& set, TypedTag<KM_BYTES, Tag> tag, const std::string& val) {
    int pos = set.find(tag);
    return pos != -1 &&
           std::string(reinterpret_cast<const char*>(set[pos].blob.data),
                       set[pos].blob.data_length) == val;
}

inline bool contains(const AuthorizationSet& set, keymaster_tag_t tag) {
    return set.find(tag) != -1;
}

typedef KeymasterTest CheckSupported;
TEST_F(CheckSupported, SupportedAlgorithms) {
    // Shouldn't blow up on NULL.
    device.SupportedAlgorithms(NULL);

    SupportedResponse<keymaster_algorithm_t> response;
    device.SupportedAlgorithms(&response);
    EXPECT_EQ(KM_ERROR_OK, response.error);
    EXPECT_EQ(1U, response.results_length);
    EXPECT_EQ(KM_ALGORITHM_RSA, response.results[0]);
}

TEST_F(CheckSupported, SupportedBlockModes) {
    // Shouldn't blow up on NULL.
    device.SupportedBlockModes(KM_ALGORITHM_RSA, NULL);

    SupportedResponse<keymaster_block_mode_t> response;
    device.SupportedBlockModes(KM_ALGORITHM_RSA, &response);
    EXPECT_EQ(KM_ERROR_OK, response.error);
    EXPECT_EQ(0U, response.results_length);

    device.SupportedBlockModes(KM_ALGORITHM_DSA, &response);
    EXPECT_EQ(KM_ERROR_UNSUPPORTED_ALGORITHM, response.error);
}

TEST_F(CheckSupported, SupportedPaddingModes) {
    // Shouldn't blow up on NULL.
    device.SupportedPaddingModes(KM_ALGORITHM_RSA, NULL);

    SupportedResponse<keymaster_padding_t> response;
    device.SupportedPaddingModes(KM_ALGORITHM_RSA, &response);
    EXPECT_EQ(KM_ERROR_OK, response.error);
    EXPECT_EQ(1U, response.results_length);
    EXPECT_EQ(KM_PAD_NONE, response.results[0]);

    device.SupportedPaddingModes(KM_ALGORITHM_DSA, &response);
    EXPECT_EQ(KM_ERROR_UNSUPPORTED_ALGORITHM, response.error);
}

TEST_F(CheckSupported, SupportedDigests) {
    // Shouldn't blow up on NULL.
    device.SupportedDigests(KM_ALGORITHM_RSA, NULL);

    SupportedResponse<keymaster_digest_t> response;
    device.SupportedDigests(KM_ALGORITHM_RSA, &response);
    EXPECT_EQ(KM_ERROR_OK, response.error);
    EXPECT_EQ(1U, response.results_length);
    EXPECT_EQ(KM_DIGEST_NONE, response.results[0]);

    device.SupportedDigests(KM_ALGORITHM_DSA, &response);
    EXPECT_EQ(KM_ERROR_UNSUPPORTED_ALGORITHM, response.error);
}

TEST_F(CheckSupported, SupportedImportFormats) {
    // Shouldn't blow up on NULL.
    device.SupportedImportFormats(KM_ALGORITHM_RSA, NULL);

    SupportedResponse<keymaster_key_format_t> response;
    device.SupportedImportFormats(KM_ALGORITHM_RSA, &response);
    EXPECT_EQ(KM_ERROR_OK, response.error);
    EXPECT_EQ(1U, response.results_length);
    EXPECT_EQ(KM_KEY_FORMAT_PKCS8, response.results[0]);

    device.SupportedImportFormats(KM_ALGORITHM_DSA, &response);
    EXPECT_EQ(KM_ERROR_UNSUPPORTED_ALGORITHM, response.error);
}

TEST_F(CheckSupported, SupportedExportFormats) {
    // Shouldn't blow up on NULL.
    device.SupportedExportFormats(KM_ALGORITHM_RSA, NULL);

    SupportedResponse<keymaster_key_format_t> response;
    device.SupportedExportFormats(KM_ALGORITHM_RSA, &response);
    EXPECT_EQ(KM_ERROR_OK, response.error);
    EXPECT_EQ(1U, response.results_length);
    EXPECT_EQ(KM_KEY_FORMAT_X509, response.results[0]);

    device.SupportedExportFormats(KM_ALGORITHM_DSA, &response);
    EXPECT_EQ(KM_ERROR_UNSUPPORTED_ALGORITHM, response.error);
}

typedef KeymasterTest NewKeyGeneration;
TEST_F(NewKeyGeneration, Rsa) {
    keymaster_key_param_t params[] = {
        Authorization(TAG_PURPOSE, KM_PURPOSE_SIGN),
        Authorization(TAG_PURPOSE, KM_PURPOSE_VERIFY),
        Authorization(TAG_ALGORITHM, KM_ALGORITHM_RSA),
        Authorization(TAG_KEY_SIZE, 256),
        Authorization(TAG_USER_ID, 7),
        Authorization(TAG_USER_AUTH_ID, 8),
        Authorization(TAG_APPLICATION_ID, "app_id", 6),
        Authorization(TAG_APPLICATION_DATA, "app_data", 8),
        Authorization(TAG_AUTH_TIMEOUT, 300),
    };
    GenerateKeyRequest req;
    req.key_description.Reinitialize(params, array_length(params));
    GenerateKeyResponse rsp;

    device.GenerateKey(req, &rsp);

    ASSERT_EQ(KM_ERROR_OK, rsp.error);
    EXPECT_EQ(0U, rsp.enforced.size());
    EXPECT_EQ(12U, rsp.enforced.SerializedSize());
    EXPECT_GT(rsp.unenforced.SerializedSize(), 12U);

    // Check specified tags are all present in unenforced characteristics
    EXPECT_TRUE(contains(rsp.unenforced, TAG_PURPOSE, KM_PURPOSE_SIGN));
    EXPECT_TRUE(contains(rsp.unenforced, TAG_PURPOSE, KM_PURPOSE_VERIFY));

    EXPECT_TRUE(contains(rsp.unenforced, TAG_ALGORITHM, KM_ALGORITHM_RSA));

    EXPECT_TRUE(contains(rsp.unenforced, TAG_USER_ID, 7));
    EXPECT_TRUE(contains(rsp.unenforced, TAG_USER_AUTH_ID, 8));
    EXPECT_TRUE(contains(rsp.unenforced, TAG_KEY_SIZE, 256));
    EXPECT_TRUE(contains(rsp.unenforced, TAG_AUTH_TIMEOUT, 300));

    // Verify that App ID, App data and ROT are NOT included.
    EXPECT_FALSE(contains(rsp.unenforced, TAG_ROOT_OF_TRUST));
    EXPECT_FALSE(contains(rsp.unenforced, TAG_APPLICATION_ID));
    EXPECT_FALSE(contains(rsp.unenforced, TAG_APPLICATION_DATA));

    // Just for giggles, check that some unexpected tags/values are NOT present.
    EXPECT_FALSE(contains(rsp.unenforced, TAG_PURPOSE, KM_PURPOSE_ENCRYPT));
    EXPECT_FALSE(contains(rsp.unenforced, TAG_PURPOSE, KM_PURPOSE_DECRYPT));
    EXPECT_FALSE(contains(rsp.unenforced, TAG_AUTH_TIMEOUT, 301));
    EXPECT_FALSE(contains(rsp.unenforced, TAG_RESCOPE_AUTH_TIMEOUT));

    // Now check that unspecified, defaulted tags are correct.
    EXPECT_TRUE(contains(rsp.unenforced, TAG_RSA_PUBLIC_EXPONENT, 65537));
    EXPECT_TRUE(contains(rsp.unenforced, TAG_ORIGIN, KM_ORIGIN_SOFTWARE));
    EXPECT_TRUE(contains(rsp.unenforced, KM_TAG_CREATION_DATETIME));
}

typedef KeymasterTest GetKeyCharacteristics;
TEST_F(GetKeyCharacteristics, SimpleRsa) {
    keymaster_key_param_t params[] = {
        Authorization(TAG_PURPOSE, KM_PURPOSE_SIGN),
        Authorization(TAG_PURPOSE, KM_PURPOSE_VERIFY),
        Authorization(TAG_ALGORITHM, KM_ALGORITHM_RSA),
        Authorization(TAG_KEY_SIZE, 256),
        Authorization(TAG_USER_ID, 7),
        Authorization(TAG_USER_AUTH_ID, 8),
        Authorization(TAG_APPLICATION_ID, "app_id", 6),
        Authorization(TAG_AUTH_TIMEOUT, 300),
    };

    GenerateKeyRequest gen_req;
    gen_req.key_description.Reinitialize(params, array_length(params));
    GenerateKeyResponse gen_rsp;

    device.GenerateKey(gen_req, &gen_rsp);
    ASSERT_EQ(KM_ERROR_OK, gen_rsp.error);

    GetKeyCharacteristicsRequest req;
    req.SetKeyMaterial(gen_rsp.key_blob);
    req.additional_params.push_back(TAG_APPLICATION_ID, "app_id", 6);

    GetKeyCharacteristicsResponse rsp;
    device.GetKeyCharacteristics(req, &rsp);
    ASSERT_EQ(KM_ERROR_OK, rsp.error);

    EXPECT_EQ(gen_rsp.enforced, rsp.enforced);
    EXPECT_EQ(gen_rsp.unenforced, rsp.unenforced);
}

class SigningOperationsTest : public KeymasterTest {
  protected:
    keymaster_key_blob_t* GenerateKey(keymaster_digest_t digest, keymaster_padding_t padding,
                                      uint32_t key_size) {
        keymaster_key_param_t params[] = {
            Authorization(TAG_PURPOSE, KM_PURPOSE_SIGN),
            Authorization(TAG_PURPOSE, KM_PURPOSE_VERIFY),
            Authorization(TAG_ALGORITHM, KM_ALGORITHM_RSA),
            Authorization(TAG_KEY_SIZE, key_size),
            Authorization(TAG_USER_ID, 7),
            Authorization(TAG_USER_AUTH_ID, 8),
            Authorization(TAG_APPLICATION_ID, "app_id", 6),
            Authorization(TAG_AUTH_TIMEOUT, 300),
        };
        GenerateKeyRequest generate_request;
        generate_request.key_description.Reinitialize(params, array_length(params));
        if (static_cast<int>(digest) != -1)
            generate_request.key_description.push_back(TAG_DIGEST, digest);
        if (static_cast<int>(padding) != -1)
            generate_request.key_description.push_back(TAG_PADDING, padding);
        device.GenerateKey(generate_request, &generate_response_);
        EXPECT_EQ(KM_ERROR_OK, generate_response_.error);

        // This is safe because generate_response_ lives as long as the test and will keep the key
        // blob around.
        return &generate_response_.key_blob;
    }

  private:
    GenerateKeyResponse generate_response_;
};

TEST_F(SigningOperationsTest, RsaSuccess) {
    keymaster_key_blob_t* key = GenerateKey(KM_DIGEST_NONE, KM_PAD_NONE, 256 /* key size */);
    ASSERT_TRUE(key != NULL);

    BeginOperationRequest begin_request;
    BeginOperationResponse begin_response;
    begin_request.SetKeyMaterial(*key);
    begin_request.purpose = KM_PURPOSE_SIGN;
    begin_request.additional_params.push_back(TAG_APPLICATION_ID, "app_id", 6);

    device.BeginOperation(begin_request, &begin_response);
    ASSERT_EQ(KM_ERROR_OK, begin_response.error);

    UpdateOperationRequest update_request;
    UpdateOperationResponse update_response;
    update_request.op_handle = begin_response.op_handle;
    update_request.input.Reinitialize("012345678901234567890123456789012", 32);
    EXPECT_EQ(32U, update_request.input.available_read());

    device.UpdateOperation(update_request, &update_response);
    ASSERT_EQ(KM_ERROR_OK, update_response.error);
    EXPECT_EQ(0U, update_response.output.available_read());

    FinishOperationRequest finish_request;
    finish_request.op_handle = begin_response.op_handle;
    FinishOperationResponse finish_response;
    device.FinishOperation(finish_request, &finish_response);
    ASSERT_EQ(KM_ERROR_OK, finish_response.error);
    EXPECT_GT(finish_response.output.available_read(), 0U);

    EXPECT_EQ(KM_ERROR_INVALID_OPERATION_HANDLE, device.AbortOperation(begin_response.op_handle));
}

TEST_F(SigningOperationsTest, RsaAbort) {
    keymaster_key_blob_t* key = GenerateKey(KM_DIGEST_NONE, KM_PAD_NONE, 256 /* key size */);
    ASSERT_TRUE(key != NULL);

    BeginOperationRequest begin_request;
    BeginOperationResponse begin_response;
    begin_request.SetKeyMaterial(*key);
    begin_request.purpose = KM_PURPOSE_SIGN;
    begin_request.additional_params.push_back(TAG_APPLICATION_ID, "app_id", 6);

    device.BeginOperation(begin_request, &begin_response);
    ASSERT_EQ(KM_ERROR_OK, begin_response.error);

    EXPECT_EQ(KM_ERROR_OK, device.AbortOperation(begin_response.op_handle));

    // Another abort should fail
    EXPECT_EQ(KM_ERROR_INVALID_OPERATION_HANDLE, device.AbortOperation(begin_response.op_handle));
}

TEST_F(SigningOperationsTest, RsaUnsupportedDigest) {
    keymaster_key_blob_t* key = GenerateKey(KM_DIGEST_SHA_2_256, KM_PAD_NONE, 256 /* key size */);
    ASSERT_TRUE(key != NULL);

    BeginOperationRequest begin_request;
    BeginOperationResponse begin_response;
    begin_request.purpose = KM_PURPOSE_SIGN;
    begin_request.SetKeyMaterial(*key);
    begin_request.additional_params.push_back(TAG_APPLICATION_ID, "app_id", 6);

    device.BeginOperation(begin_request, &begin_response);
    ASSERT_EQ(KM_ERROR_UNSUPPORTED_DIGEST, begin_response.error);

    EXPECT_EQ(KM_ERROR_INVALID_OPERATION_HANDLE, device.AbortOperation(begin_response.op_handle));
}

TEST_F(SigningOperationsTest, RsaUnsupportedPadding) {
    keymaster_key_blob_t* key = GenerateKey(KM_DIGEST_NONE, KM_PAD_RSA_OAEP, 256 /* key size */);
    ASSERT_TRUE(key != NULL);

    BeginOperationRequest begin_request;
    BeginOperationResponse begin_response;
    begin_request.purpose = KM_PURPOSE_SIGN;
    begin_request.SetKeyMaterial(*key);
    begin_request.additional_params.push_back(TAG_APPLICATION_ID, "app_id", 6);

    device.BeginOperation(begin_request, &begin_response);
    ASSERT_EQ(KM_ERROR_UNSUPPORTED_PADDING_MODE, begin_response.error);

    EXPECT_EQ(KM_ERROR_INVALID_OPERATION_HANDLE, device.AbortOperation(begin_response.op_handle));
}

TEST_F(SigningOperationsTest, RsaNoDigest) {
    keymaster_key_blob_t* key =
        GenerateKey(static_cast<keymaster_digest_t>(-1), KM_PAD_NONE, 256 /* key size */);
    ASSERT_TRUE(key != NULL);

    BeginOperationRequest begin_request;
    BeginOperationResponse begin_response;
    begin_request.purpose = KM_PURPOSE_SIGN;
    begin_request.SetKeyMaterial(*key);
    begin_request.additional_params.push_back(TAG_APPLICATION_ID, "app_id", 6);

    device.BeginOperation(begin_request, &begin_response);
    ASSERT_EQ(KM_ERROR_UNSUPPORTED_DIGEST, begin_response.error);

    EXPECT_EQ(KM_ERROR_INVALID_OPERATION_HANDLE, device.AbortOperation(begin_response.op_handle));
}

TEST_F(SigningOperationsTest, RsaNoPadding) {
    keymaster_key_blob_t* key =
        GenerateKey(KM_DIGEST_NONE, static_cast<keymaster_padding_t>(-1), 256 /* key size */);
    ASSERT_TRUE(key != NULL);

    BeginOperationRequest begin_request;
    BeginOperationResponse begin_response;
    begin_request.purpose = KM_PURPOSE_SIGN;
    begin_request.SetKeyMaterial(*key);
    begin_request.additional_params.push_back(TAG_APPLICATION_ID, "app_id", 6);

    device.BeginOperation(begin_request, &begin_response);
    ASSERT_EQ(KM_ERROR_UNSUPPORTED_PADDING_MODE, begin_response.error);

    EXPECT_EQ(KM_ERROR_INVALID_OPERATION_HANDLE, device.AbortOperation(begin_response.op_handle));
}

TEST_F(SigningOperationsTest, RsaTooShortMessage) {
    keymaster_key_blob_t* key = GenerateKey(KM_DIGEST_NONE, KM_PAD_NONE, 256 /* key size */);
    ASSERT_TRUE(key != NULL);

    BeginOperationRequest begin_request;
    BeginOperationResponse begin_response;
    begin_request.SetKeyMaterial(*key);
    begin_request.purpose = KM_PURPOSE_SIGN;
    begin_request.additional_params.push_back(TAG_APPLICATION_ID, "app_id", 6);

    device.BeginOperation(begin_request, &begin_response);
    ASSERT_EQ(KM_ERROR_OK, begin_response.error);

    UpdateOperationRequest update_request;
    UpdateOperationResponse update_response;
    update_request.op_handle = begin_response.op_handle;
    update_request.input.Reinitialize("01234567890123456789012345678901", 31);
    EXPECT_EQ(31U, update_request.input.available_read());

    device.UpdateOperation(update_request, &update_response);
    ASSERT_EQ(KM_ERROR_OK, update_response.error);
    EXPECT_EQ(0U, update_response.output.available_read());

    FinishOperationRequest finish_request;
    finish_request.op_handle = begin_response.op_handle;
    FinishOperationResponse finish_response;
    device.FinishOperation(finish_request, &finish_response);
    ASSERT_EQ(KM_ERROR_INVALID_INPUT_LENGTH, finish_response.error);
    EXPECT_EQ(0U, finish_response.output.available_read());

    EXPECT_EQ(KM_ERROR_INVALID_OPERATION_HANDLE, device.AbortOperation(begin_response.op_handle));
}

class VerificationOperationsTest : public KeymasterTest {
  protected:
    VerificationOperationsTest() {
        generate_response_.error = KM_ERROR_UNKNOWN_ERROR;
        finish_response_.error = KM_ERROR_UNKNOWN_ERROR;
    }

    void GenerateKey(keymaster_digest_t digest, keymaster_padding_t padding, uint32_t key_size) {
        keymaster_key_param_t params[] = {
            Authorization(TAG_PURPOSE, KM_PURPOSE_SIGN),
            Authorization(TAG_PURPOSE, KM_PURPOSE_VERIFY),
            Authorization(TAG_ALGORITHM, KM_ALGORITHM_RSA),
            Authorization(TAG_KEY_SIZE, key_size),
            Authorization(TAG_USER_ID, 7),
            Authorization(TAG_USER_AUTH_ID, 8),
            Authorization(TAG_APPLICATION_ID, "app_id", 6),
            Authorization(TAG_AUTH_TIMEOUT, 300),
        };
        GenerateKeyRequest generate_request;
        generate_request.key_description.Reinitialize(params, array_length(params));
        if (static_cast<int>(digest) != -1)
            generate_request.key_description.push_back(TAG_DIGEST, digest);
        if (static_cast<int>(padding) != -1)
            generate_request.key_description.push_back(TAG_PADDING, padding);
        device.GenerateKey(generate_request, &generate_response_);
        EXPECT_EQ(KM_ERROR_OK, generate_response_.error);

        BeginOperationRequest begin_request;
        BeginOperationResponse begin_response;
        begin_request.SetKeyMaterial(generate_response_.key_blob);
        begin_request.purpose = KM_PURPOSE_SIGN;
        begin_request.additional_params.push_back(TAG_APPLICATION_ID, "app_id", 6);

        device.BeginOperation(begin_request, &begin_response);
        ASSERT_EQ(KM_ERROR_OK, begin_response.error);

        UpdateOperationRequest update_request;
        UpdateOperationResponse update_response;
        update_request.op_handle = begin_response.op_handle;
        update_request.input.Reinitialize("012345678901234567890123456789012", 32);
        EXPECT_EQ(32U, update_request.input.available_read());

        device.UpdateOperation(update_request, &update_response);
        ASSERT_EQ(KM_ERROR_OK, update_response.error);
        EXPECT_EQ(0U, update_response.output.available_read());

        FinishOperationRequest finish_request;
        finish_request.op_handle = begin_response.op_handle;
        device.FinishOperation(finish_request, &finish_response_);
        ASSERT_EQ(KM_ERROR_OK, finish_response_.error);
        EXPECT_GT(finish_response_.output.available_read(), 0U);
    }

    keymaster_key_blob_t* key_blob() {
        if (generate_response_.error == KM_ERROR_OK)
            return &generate_response_.key_blob;
        return NULL;
    }

    Buffer* signature() {
        if (finish_response_.error == KM_ERROR_OK)
            return &finish_response_.output;
        return NULL;
    }

  private:
    GenerateKeyResponse generate_response_;
    FinishOperationResponse finish_response_;
};

TEST_F(VerificationOperationsTest, RsaSuccess) {
    GenerateKey(KM_DIGEST_NONE, KM_PAD_NONE, 256 /* key size */);
    ASSERT_TRUE(key_blob() != NULL);
    ASSERT_TRUE(signature() != NULL);

    BeginOperationRequest begin_request;
    BeginOperationResponse begin_response;
    begin_request.SetKeyMaterial(*key_blob());
    begin_request.purpose = KM_PURPOSE_VERIFY;
    begin_request.additional_params.push_back(TAG_APPLICATION_ID, "app_id", 6);

    device.BeginOperation(begin_request, &begin_response);
    ASSERT_EQ(KM_ERROR_OK, begin_response.error);

    UpdateOperationRequest update_request;
    UpdateOperationResponse update_response;
    update_request.op_handle = begin_response.op_handle;
    update_request.input.Reinitialize("012345678901234567890123456789012", 32);
    EXPECT_EQ(32U, update_request.input.available_read());

    device.UpdateOperation(update_request, &update_response);
    ASSERT_EQ(KM_ERROR_OK, update_response.error);
    EXPECT_EQ(0U, update_response.output.available_read());

    FinishOperationRequest finish_request;
    finish_request.op_handle = begin_response.op_handle;
    finish_request.signature.Reinitialize(*signature());
    FinishOperationResponse finish_response;
    device.FinishOperation(finish_request, &finish_response);
    ASSERT_EQ(KM_ERROR_OK, finish_response.error);
    EXPECT_EQ(0U, finish_response.output.available_read());

    EXPECT_EQ(KM_ERROR_INVALID_OPERATION_HANDLE, device.AbortOperation(begin_response.op_handle));
}

}  // namespace test
}  // namespace keymaster
