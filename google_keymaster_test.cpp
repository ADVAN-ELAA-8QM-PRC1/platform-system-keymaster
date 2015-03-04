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

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <openssl/engine.h>

#include <keymaster/google_keymaster_utils.h>
#include <keymaster/keymaster_tags.h>
#include <keymaster/soft_keymaster_device.h>

#include "google_keymaster_test_utils.h"

using std::ifstream;
using std::istreambuf_iterator;
using std::string;
using std::vector;

int main(int argc, char** argv) {
    ERR_load_crypto_strings();
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    // Clean up stuff OpenSSL leaves around, so Valgrind doesn't complain.
    CRYPTO_cleanup_all_ex_data();
    ERR_remove_thread_state(NULL);
    ERR_free_strings();
    return result;
}

template <typename T> std::ostream& operator<<(std::ostream& os, const std::vector<T>& vec) {
    os << "{ ";
    bool first = true;
    for (T t : vec) {
        os << (first ? "" : ", ") << t;
        if (first)
            first = false;
    }
    os << " }";
    return os;
}

namespace keymaster {
namespace test {

/**
 * Utility class to make construction of AuthorizationSets easy, and readable.  Use like:
 *
 * ParamBuilder()
 *     .Option(TAG_ALGORITHM, KM_ALGORITHM_RSA)
 *     .Option(TAG_KEY_SIZE, 512)
 *     .Option(TAG_DIGEST, KM_DIGEST_NONE)
 *     .Option(TAG_PADDING, KM_PAD_NONE)
 *     .Option(TAG_SINGLE_USE_PER_BOOT, true)
 *     .build();
 *
 * In addition there are methods that add common sets of parameters, like RsaSigningKey().
 */
class ParamBuilder {
  public:
    template <typename TagType, typename ValueType>
    ParamBuilder& Option(TagType tag, ValueType value) {
        set.push_back(tag, value);
        return *this;
    }

    ParamBuilder& RsaKey(uint32_t key_size = 0, uint64_t public_exponent = 0) {
        Option(TAG_ALGORITHM, KM_ALGORITHM_RSA);
        if (key_size != 0)
            Option(TAG_KEY_SIZE, key_size);
        if (public_exponent != 0)
            Option(TAG_RSA_PUBLIC_EXPONENT, public_exponent);
        return *this;
    }

    ParamBuilder& EcdsaKey(uint32_t key_size = 0) {
        Option(TAG_ALGORITHM, KM_ALGORITHM_ECDSA);
        if (key_size != 0)
            Option(TAG_KEY_SIZE, key_size);
        return *this;
    }

    ParamBuilder& AesKey(uint32_t key_size) {
        Option(TAG_ALGORITHM, KM_ALGORITHM_AES);
        return Option(TAG_KEY_SIZE, key_size);
    }

    ParamBuilder& HmacKey(uint32_t key_size, keymaster_digest_t digest, uint32_t mac_length) {
        Option(TAG_ALGORITHM, KM_ALGORITHM_HMAC);
        Option(TAG_KEY_SIZE, key_size);
        SigningKey();
        Option(TAG_DIGEST, digest);
        return Option(TAG_MAC_LENGTH, mac_length);
    }

    ParamBuilder& RsaSigningKey(uint32_t key_size = 0, keymaster_digest_t digest = KM_DIGEST_NONE,
                                keymaster_padding_t padding = KM_PAD_NONE,
                                uint64_t public_exponent = 0) {
        RsaKey(key_size, public_exponent);
        SigningKey();
        Option(TAG_DIGEST, digest);
        return Option(TAG_PADDING, padding);
    }

    ParamBuilder& RsaEncryptionKey(uint32_t key_size = 0,
                                   keymaster_padding_t padding = KM_PAD_RSA_OAEP,
                                   uint64_t public_exponent = 0) {
        RsaKey(key_size, public_exponent);
        EncryptionKey();
        return Option(TAG_PADDING, padding);
    }

    ParamBuilder& EcdsaSigningKey(uint32_t key_size = 0) {
        EcdsaKey(key_size);
        return SigningKey();
    }

    ParamBuilder& AesEncryptionKey(uint32_t key_size) {
        AesKey(key_size);
        return EncryptionKey();
    }

    ParamBuilder& SigningKey() {
        Option(TAG_PURPOSE, KM_PURPOSE_SIGN);
        return Option(TAG_PURPOSE, KM_PURPOSE_VERIFY);
    }

    ParamBuilder& EncryptionKey() {
        Option(TAG_PURPOSE, KM_PURPOSE_ENCRYPT);
        return Option(TAG_PURPOSE, KM_PURPOSE_DECRYPT);
    }

    ParamBuilder& NoDigestOrPadding() {
        Option(TAG_DIGEST, KM_DIGEST_NONE);
        return Option(TAG_PADDING, KM_PAD_NONE);
    }

    ParamBuilder& OcbMode(uint32_t chunk_length, uint32_t mac_length) {
        Option(TAG_BLOCK_MODE, KM_MODE_OCB);
        Option(TAG_CHUNK_LENGTH, chunk_length);
        return Option(TAG_MAC_LENGTH, mac_length);
    }

    AuthorizationSet build() const { return set; }

  private:
    AuthorizationSet set;
};

StdoutLogger logger;

const uint64_t OP_HANDLE_SENTINEL = 0xFFFFFFFFFFFFFFFF;
class KeymasterTest : public testing::Test {
  protected:
    KeymasterTest() : out_params_(NULL), op_handle_(OP_HANDLE_SENTINEL), characteristics_(NULL) {
        blob_.key_material = NULL;
        RAND_seed("foobar", 6);
        blob_.key_material = 0;
    }

    ~KeymasterTest() {
        FreeCharacteristics();
        FreeKeyBlob();
    }

    keymaster1_device_t* device() {
        return reinterpret_cast<keymaster1_device_t*>(device_.hw_device());
    }

    keymaster_error_t GenerateKey(const ParamBuilder& builder) {
        AuthorizationSet params(builder.build());
        params.push_back(UserAuthParams());
        params.push_back(ClientParams());

        FreeKeyBlob();
        FreeCharacteristics();
        return device()->generate_key(device(), params.data(), params.size(), &blob_,
                                      &characteristics_);
    }

    keymaster_error_t ImportKey(const ParamBuilder& builder, keymaster_key_format_t format,
                                const string& key_material) {
        AuthorizationSet params(builder.build());
        params.push_back(UserAuthParams());
        params.push_back(ClientParams());

        FreeKeyBlob();
        FreeCharacteristics();
        return device()->import_key(device(), params.data(), params.size(), format,
                                    reinterpret_cast<const uint8_t*>(key_material.c_str()),
                                    key_material.length(), &blob_, &characteristics_);
    }

    AuthorizationSet UserAuthParams() {
        AuthorizationSet set;
        set.push_back(TAG_USER_ID, 7);
        set.push_back(TAG_USER_AUTH_ID, 8);
        set.push_back(TAG_AUTH_TIMEOUT, 300);
        return set;
    }

    AuthorizationSet ClientParams() {
        AuthorizationSet set;
        set.push_back(TAG_APPLICATION_ID, "app_id", 6);
        return set;
    }

    keymaster_error_t BeginOperation(keymaster_purpose_t purpose) {
        return device()->begin(device(), purpose, &blob_, client_params_,
                               array_length(client_params_), &out_params_, &out_params_count_,
                               &op_handle_);
    }

    keymaster_error_t UpdateOperation(const string& message, string* output,
                                      size_t* input_consumed) {
        uint8_t* out_tmp = NULL;
        size_t out_length;
        EXPECT_NE(op_handle_, OP_HANDLE_SENTINEL);
        keymaster_error_t error = device()->update(
            device(), op_handle_, NULL /* additional_params */, 0 /* additional_params_count */,
            reinterpret_cast<const uint8_t*>(message.c_str()), message.length(), input_consumed,
            &out_tmp, &out_length);
        if (out_tmp)
            output->append(reinterpret_cast<char*>(out_tmp), out_length);
        free(out_tmp);
        return error;
    }

    keymaster_error_t FinishOperation(string* output) { return FinishOperation("", output); }

    keymaster_error_t FinishOperation(const string& signature, string* output) {
        uint8_t* out_tmp = NULL;
        size_t out_length;
        keymaster_error_t error = device()->finish(
            device(), op_handle_, NULL /* additional_params */, 0 /* additional_params_count */,
            reinterpret_cast<const uint8_t*>(signature.c_str()), signature.length(), &out_tmp,
            &out_length);
        if (out_tmp)
            output->append(reinterpret_cast<char*>(out_tmp), out_length);
        free(out_tmp);
        return error;
    }

    template <typename T>
    bool ResponseContains(const vector<T>& expected, const T* values, size_t len) {
        return expected.size() == len &&
               std::is_permutation(values, values + len, expected.begin());
    }

    template <typename T> bool ResponseContains(T expected, const T* values, size_t len) {
        return (len == 1 && *values == expected);
    }

    keymaster_error_t AbortOperation() { return device()->abort(device(), op_handle_); }

    string ProcessMessage(keymaster_purpose_t purpose, const string& message) {
        EXPECT_EQ(KM_ERROR_OK, BeginOperation(purpose));

        string result;
        size_t input_consumed;
        EXPECT_EQ(KM_ERROR_OK, UpdateOperation(message, &result, &input_consumed));
        EXPECT_EQ(message.size(), input_consumed);
        EXPECT_EQ(KM_ERROR_OK, FinishOperation(&result));
        return result;
    }

    string ProcessMessage(keymaster_purpose_t purpose, const string& message,
                          const string& signature) {
        EXPECT_EQ(KM_ERROR_OK, BeginOperation(purpose));

        string result;
        size_t input_consumed;
        EXPECT_EQ(KM_ERROR_OK, UpdateOperation(message, &result, &input_consumed));
        EXPECT_EQ(message.size(), input_consumed);
        EXPECT_EQ(KM_ERROR_OK, FinishOperation(signature, &result));
        return result;
    }

    void SignMessage(const string& message, string* signature) {
        SCOPED_TRACE("SignMessage");
        *signature = ProcessMessage(KM_PURPOSE_SIGN, message);
        EXPECT_GT(signature->size(), 0);
    }

    void VerifyMessage(const string& message, const string& signature) {
        SCOPED_TRACE("VerifyMessage");
        ProcessMessage(KM_PURPOSE_VERIFY, message, signature);
    }

    string EncryptMessage(const string& message) {
        SCOPED_TRACE("EncryptMessage");
        return ProcessMessage(KM_PURPOSE_ENCRYPT, message);
    }

    string DecryptMessage(const string& ciphertext) {
        SCOPED_TRACE("DecryptMessage");
        return ProcessMessage(KM_PURPOSE_DECRYPT, ciphertext);
    }

    keymaster_error_t GetCharacteristics() {
        FreeCharacteristics();
        return device()->get_key_characteristics(device(), &blob_, &client_id_, NULL /* app_data */,
                                                 &characteristics_);
    }

    keymaster_error_t ExportKey(keymaster_key_format_t format, string* export_data) {
        uint8_t* export_data_tmp;
        size_t export_data_length;

        keymaster_error_t error =
            device()->export_key(device(), format, &blob_, &client_id_, NULL /* app_data */,
                                 &export_data_tmp, &export_data_length);

        if (error != KM_ERROR_OK)
            return error;

        *export_data = string(reinterpret_cast<char*>(export_data_tmp), export_data_length);
        free(export_data_tmp);
        return error;
    }

    keymaster_error_t GetVersion(uint8_t* major, uint8_t* minor, uint8_t* subminor) {
        GetVersionRequest request;
        GetVersionResponse response;
        device_.GetVersion(request, &response);
        if (response.error != KM_ERROR_OK)
            return response.error;
        *major = response.major_ver;
        *minor = response.minor_ver;
        *subminor = response.subminor_ver;
        return response.error;
    }

    AuthorizationSet hw_enforced() {
        EXPECT_TRUE(characteristics_ != NULL);
        return AuthorizationSet(characteristics_->hw_enforced);
    }

    AuthorizationSet sw_enforced() {
        EXPECT_TRUE(characteristics_ != NULL);
        return AuthorizationSet(characteristics_->sw_enforced);
    }

    void FreeCharacteristics() {
        keymaster_free_characteristics(characteristics_);
        free(characteristics_);
        characteristics_ = NULL;
    }

    void FreeKeyBlob() {
        free(const_cast<uint8_t*>(blob_.key_material));
        blob_.key_material = NULL;
    }

    void corrupt_key_blob() {
        assert(blob_.key_material);
        uint8_t* tmp = const_cast<uint8_t*>(blob_.key_material);
        ++tmp[blob_.key_material_size / 2];
    }

  private:
    SoftKeymasterDevice device_;
    keymaster_blob_t client_id_ = {.data = reinterpret_cast<const uint8_t*>("app_id"),
                                   .data_length = 6};
    keymaster_key_param_t client_params_[1] = {
        Authorization(TAG_APPLICATION_ID, client_id_.data, client_id_.data_length)};

    keymaster_key_param_t* out_params_;
    size_t out_params_count_;
    uint64_t op_handle_;

    keymaster_key_blob_t blob_;
    keymaster_key_characteristics_t* characteristics_;
};

typedef KeymasterTest CheckSupported;
TEST_F(CheckSupported, SupportedAlgorithms) {
    EXPECT_EQ(KM_ERROR_OUTPUT_PARAMETER_NULL,
              device()->get_supported_algorithms(device(), NULL, NULL));

    size_t len;
    keymaster_algorithm_t* algorithms;
    EXPECT_EQ(KM_ERROR_OK, device()->get_supported_algorithms(device(), &algorithms, &len));
    EXPECT_TRUE(ResponseContains(
        {KM_ALGORITHM_RSA, KM_ALGORITHM_ECDSA, KM_ALGORITHM_AES, KM_ALGORITHM_HMAC}, algorithms,
        len));
    free(algorithms);
}

TEST_F(CheckSupported, SupportedBlockModes) {
    EXPECT_EQ(KM_ERROR_OUTPUT_PARAMETER_NULL,
              device()->get_supported_block_modes(device(), KM_ALGORITHM_RSA, KM_PURPOSE_ENCRYPT,
                                                  NULL, NULL));

    size_t len;
    keymaster_block_mode_t* modes;
    EXPECT_EQ(KM_ERROR_OK, device()->get_supported_block_modes(device(), KM_ALGORITHM_RSA,
                                                               KM_PURPOSE_ENCRYPT, &modes, &len));
    EXPECT_EQ(0, len);
    free(modes);

    EXPECT_EQ(KM_ERROR_UNSUPPORTED_ALGORITHM,
              device()->get_supported_block_modes(device(), KM_ALGORITHM_DSA, KM_PURPOSE_ENCRYPT,
                                                  &modes, &len));

    EXPECT_EQ(KM_ERROR_UNSUPPORTED_PURPOSE,
              device()->get_supported_block_modes(device(), KM_ALGORITHM_ECDSA, KM_PURPOSE_ENCRYPT,
                                                  &modes, &len));

    EXPECT_EQ(KM_ERROR_OK, device()->get_supported_block_modes(device(), KM_ALGORITHM_AES,
                                                               KM_PURPOSE_ENCRYPT, &modes, &len));
    EXPECT_TRUE(ResponseContains({KM_MODE_OCB, KM_MODE_ECB, KM_MODE_CBC, KM_MODE_OFB, KM_MODE_CFB},
                                 modes, len));
    free(modes);
}

TEST_F(CheckSupported, SupportedPaddingModes) {
    EXPECT_EQ(KM_ERROR_OUTPUT_PARAMETER_NULL,
              device()->get_supported_padding_modes(device(), KM_ALGORITHM_RSA, KM_PURPOSE_ENCRYPT,
                                                    NULL, NULL));

    size_t len;
    keymaster_padding_t* modes;
    EXPECT_EQ(KM_ERROR_OK, device()->get_supported_padding_modes(device(), KM_ALGORITHM_RSA,
                                                                 KM_PURPOSE_SIGN, &modes, &len));
    EXPECT_TRUE(
        ResponseContains({KM_PAD_NONE, KM_PAD_RSA_PKCS1_1_5_SIGN, KM_PAD_RSA_PSS}, modes, len));
    free(modes);

    EXPECT_EQ(KM_ERROR_OK, device()->get_supported_padding_modes(device(), KM_ALGORITHM_RSA,
                                                                 KM_PURPOSE_ENCRYPT, &modes, &len));
    EXPECT_TRUE(ResponseContains({KM_PAD_RSA_OAEP, KM_PAD_RSA_PKCS1_1_5_ENCRYPT}, modes, len));
    free(modes);

    EXPECT_EQ(KM_ERROR_UNSUPPORTED_ALGORITHM,
              device()->get_supported_padding_modes(device(), KM_ALGORITHM_DSA, KM_PURPOSE_SIGN,
                                                    &modes, &len));

    EXPECT_EQ(KM_ERROR_OK, device()->get_supported_padding_modes(device(), KM_ALGORITHM_ECDSA,
                                                                 KM_PURPOSE_SIGN, &modes, &len));
    EXPECT_EQ(0, len);
    free(modes);

    EXPECT_EQ(KM_ERROR_UNSUPPORTED_PURPOSE,
              device()->get_supported_padding_modes(device(), KM_ALGORITHM_AES, KM_PURPOSE_SIGN,
                                                    &modes, &len));
}

TEST_F(CheckSupported, SupportedDigests) {
    EXPECT_EQ(
        KM_ERROR_OUTPUT_PARAMETER_NULL,
        device()->get_supported_digests(device(), KM_ALGORITHM_RSA, KM_PURPOSE_SIGN, NULL, NULL));

    size_t len;
    keymaster_digest_t* digests;
    EXPECT_EQ(KM_ERROR_OK, device()->get_supported_digests(device(), KM_ALGORITHM_RSA,
                                                           KM_PURPOSE_SIGN, &digests, &len));
    EXPECT_TRUE(ResponseContains({KM_DIGEST_NONE, KM_DIGEST_SHA_2_256}, digests, len));
    free(digests);

    EXPECT_EQ(KM_ERROR_UNSUPPORTED_ALGORITHM,
              device()->get_supported_digests(device(), KM_ALGORITHM_DSA, KM_PURPOSE_SIGN, &digests,
                                              &len));

    EXPECT_EQ(KM_ERROR_OK, device()->get_supported_digests(device(), KM_ALGORITHM_ECDSA,
                                                           KM_PURPOSE_SIGN, &digests, &len));
    EXPECT_EQ(0, len);
    free(digests);

    EXPECT_EQ(KM_ERROR_UNSUPPORTED_PURPOSE,
              device()->get_supported_digests(device(), KM_ALGORITHM_AES, KM_PURPOSE_SIGN, &digests,
                                              &len));

    EXPECT_EQ(KM_ERROR_OK, device()->get_supported_digests(device(), KM_ALGORITHM_HMAC,
                                                           KM_PURPOSE_SIGN, &digests, &len));
    EXPECT_TRUE(ResponseContains({KM_DIGEST_SHA_2_224, KM_DIGEST_SHA_2_256, KM_DIGEST_SHA_2_384,
                                  KM_DIGEST_SHA_2_512, KM_DIGEST_SHA1},
                                 digests, len));
    free(digests);
}

TEST_F(CheckSupported, SupportedImportFormats) {
    EXPECT_EQ(KM_ERROR_OUTPUT_PARAMETER_NULL,
              device()->get_supported_import_formats(device(), KM_ALGORITHM_RSA, NULL, NULL));

    size_t len;
    keymaster_key_format_t* formats;
    EXPECT_EQ(KM_ERROR_OK,
              device()->get_supported_import_formats(device(), KM_ALGORITHM_RSA, &formats, &len));
    EXPECT_TRUE(ResponseContains(KM_KEY_FORMAT_PKCS8, formats, len));
    free(formats);

    EXPECT_EQ(KM_ERROR_OK,
              device()->get_supported_import_formats(device(), KM_ALGORITHM_AES, &formats, &len));
    EXPECT_TRUE(ResponseContains(KM_KEY_FORMAT_RAW, formats, len));
    free(formats);

    EXPECT_EQ(KM_ERROR_OK,
              device()->get_supported_import_formats(device(), KM_ALGORITHM_HMAC, &formats, &len));
    EXPECT_TRUE(ResponseContains(KM_KEY_FORMAT_RAW, formats, len));
    free(formats);
}

TEST_F(CheckSupported, SupportedExportFormats) {
    EXPECT_EQ(KM_ERROR_OUTPUT_PARAMETER_NULL,
              device()->get_supported_export_formats(device(), KM_ALGORITHM_RSA, NULL, NULL));

    size_t len;
    keymaster_key_format_t* formats;
    EXPECT_EQ(KM_ERROR_OK,
              device()->get_supported_export_formats(device(), KM_ALGORITHM_RSA, &formats, &len));
    EXPECT_TRUE(ResponseContains(KM_KEY_FORMAT_X509, formats, len));
    free(formats);

    EXPECT_EQ(KM_ERROR_UNSUPPORTED_ALGORITHM,
              device()->get_supported_export_formats(device(), KM_ALGORITHM_DSA, &formats, &len));

    EXPECT_EQ(KM_ERROR_OK,
              device()->get_supported_export_formats(device(), KM_ALGORITHM_ECDSA, &formats, &len));
    EXPECT_TRUE(ResponseContains(KM_KEY_FORMAT_X509, formats, len));
    free(formats);

    EXPECT_EQ(KM_ERROR_OK,
              device()->get_supported_export_formats(device(), KM_ALGORITHM_AES, &formats, &len));
    EXPECT_EQ(0, len);
    free(formats);

    EXPECT_EQ(KM_ERROR_OK,
              device()->get_supported_export_formats(device(), KM_ALGORITHM_AES, &formats, &len));
    EXPECT_EQ(0, len);
    free(formats);

    EXPECT_EQ(KM_ERROR_OK,
              device()->get_supported_export_formats(device(), KM_ALGORITHM_HMAC, &formats, &len));
    EXPECT_EQ(0, len);
    free(formats);
}

class NewKeyGeneration : public KeymasterTest {
  protected:
    void CheckBaseParams() {
        EXPECT_EQ(0U, hw_enforced().size());
        EXPECT_EQ(12U, hw_enforced().SerializedSize());

        AuthorizationSet auths = sw_enforced();
        EXPECT_GT(auths.SerializedSize(), 12U);

        EXPECT_TRUE(contains(auths, TAG_PURPOSE, KM_PURPOSE_SIGN));
        EXPECT_TRUE(contains(auths, TAG_PURPOSE, KM_PURPOSE_VERIFY));
        EXPECT_TRUE(contains(auths, TAG_USER_ID, 7));
        EXPECT_TRUE(contains(auths, TAG_USER_AUTH_ID, 8));
        EXPECT_TRUE(contains(auths, TAG_AUTH_TIMEOUT, 300));

        // Verify that App ID, App data and ROT are NOT included.
        EXPECT_FALSE(contains(auths, TAG_ROOT_OF_TRUST));
        EXPECT_FALSE(contains(auths, TAG_APPLICATION_ID));
        EXPECT_FALSE(contains(auths, TAG_APPLICATION_DATA));

        // Just for giggles, check that some unexpected tags/values are NOT present.
        EXPECT_FALSE(contains(auths, TAG_PURPOSE, KM_PURPOSE_ENCRYPT));
        EXPECT_FALSE(contains(auths, TAG_PURPOSE, KM_PURPOSE_DECRYPT));
        EXPECT_FALSE(contains(auths, TAG_AUTH_TIMEOUT, 301));

        // Now check that unspecified, defaulted tags are correct.
        EXPECT_TRUE(contains(auths, TAG_ORIGIN, KM_ORIGIN_SOFTWARE));
        EXPECT_TRUE(contains(auths, KM_TAG_CREATION_DATETIME));
    }
};

TEST_F(NewKeyGeneration, Rsa) {
    ASSERT_EQ(KM_ERROR_OK,
              GenerateKey(ParamBuilder().RsaSigningKey(256, KM_DIGEST_NONE, KM_PAD_NONE, 3)));
    CheckBaseParams();

    // Check specified tags are all present in auths
    AuthorizationSet auths(sw_enforced());
    EXPECT_TRUE(contains(auths, TAG_ALGORITHM, KM_ALGORITHM_RSA));
    EXPECT_TRUE(contains(auths, TAG_KEY_SIZE, 256));
    EXPECT_TRUE(contains(auths, TAG_RSA_PUBLIC_EXPONENT, 3));
}

TEST_F(NewKeyGeneration, RsaDefaultSize) {
    // TODO(swillden): Remove support for defaulting RSA parameter size and pub exponent.
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().RsaSigningKey()));
    CheckBaseParams();

    // Check specified tags are all present in unenforced characteristics
    EXPECT_TRUE(contains(sw_enforced(), TAG_ALGORITHM, KM_ALGORITHM_RSA));

    // Now check that unspecified, defaulted tags are correct.
    EXPECT_TRUE(contains(sw_enforced(), TAG_RSA_PUBLIC_EXPONENT, 65537));
    EXPECT_TRUE(contains(sw_enforced(), TAG_KEY_SIZE, 2048));
}

TEST_F(NewKeyGeneration, Ecdsa) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().EcdsaSigningKey(224)));
    CheckBaseParams();

    // Check specified tags are all present in unenforced characteristics
    EXPECT_TRUE(contains(sw_enforced(), TAG_ALGORITHM, KM_ALGORITHM_ECDSA));
    EXPECT_TRUE(contains(sw_enforced(), TAG_KEY_SIZE, 224));
}

TEST_F(NewKeyGeneration, EcdsaDefaultSize) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().EcdsaSigningKey()));
    CheckBaseParams();

    // Check specified tags are all present in unenforced characteristics
    EXPECT_TRUE(contains(sw_enforced(), TAG_ALGORITHM, KM_ALGORITHM_ECDSA));

    // Now check that unspecified, defaulted tags are correct.
    EXPECT_TRUE(contains(sw_enforced(), TAG_KEY_SIZE, 224));
}

TEST_F(NewKeyGeneration, EcdsaInvalidSize) {
    ASSERT_EQ(KM_ERROR_UNSUPPORTED_KEY_SIZE, GenerateKey(ParamBuilder().EcdsaSigningKey(190)));
}

TEST_F(NewKeyGeneration, EcdsaAllValidSizes) {
    size_t valid_sizes[] = {224, 256, 384, 521};
    for (size_t size : valid_sizes) {
        EXPECT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().EcdsaSigningKey(size)))
            << "Failed to generate size: " << size;
    }
}

TEST_F(NewKeyGeneration, AesOcb) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().AesEncryptionKey(128).OcbMode(4096, 16)));
}

TEST_F(NewKeyGeneration, AesOcbInvalidKeySize) {
    ASSERT_EQ(KM_ERROR_UNSUPPORTED_KEY_SIZE,
              GenerateKey(ParamBuilder().AesEncryptionKey(136).OcbMode(4096, 16)));
}

TEST_F(NewKeyGeneration, AesOcbAllValidSizes) {
    size_t valid_sizes[] = {128, 192, 256};
    for (size_t size : valid_sizes) {
        EXPECT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().AesEncryptionKey(size)))
            << "Failed to generate size: " << size;
    }
}

TEST_F(NewKeyGeneration, HmacSha256) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().HmacKey(128, KM_DIGEST_SHA_2_256, 16)));
}

typedef KeymasterTest GetKeyCharacteristics;
TEST_F(GetKeyCharacteristics, SimpleRsa) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().RsaSigningKey(256)));
    AuthorizationSet original(sw_enforced());

    ASSERT_EQ(KM_ERROR_OK, GetCharacteristics());
    EXPECT_EQ(original, sw_enforced());
}

typedef KeymasterTest SigningOperationsTest;
TEST_F(SigningOperationsTest, RsaSuccess) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().RsaSigningKey(256)));
    string message = "12345678901234567890123456789012";
    string signature;
    SignMessage(message, &signature);
}

TEST_F(SigningOperationsTest, RsaSha256DigestSuccess) {
    // Note that without padding, key size must exactly match digest size.
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().RsaSigningKey(256, KM_DIGEST_SHA_2_256)));
    string message(1024, 'a');
    string signature;
    SignMessage(message, &signature);
}

TEST_F(SigningOperationsTest, RsaPssSha256Success) {
    ASSERT_EQ(KM_ERROR_OK,
              GenerateKey(ParamBuilder().RsaSigningKey(512, KM_DIGEST_SHA_2_256, KM_PAD_RSA_PSS)));
    // Use large message, which won't work without digesting.
    string message(1024, 'a');
    string signature;
    SignMessage(message, &signature);
}

TEST_F(SigningOperationsTest, RsaPkcs1Sha256Success) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().RsaSigningKey(512, KM_DIGEST_SHA_2_256,
                                                                    KM_PAD_RSA_PKCS1_1_5_SIGN)));
    string message(1024, 'a');
    string signature;
    SignMessage(message, &signature);
}

TEST_F(SigningOperationsTest, RsaPssSha256TooSmallKey) {
    // Key must be at least 10 bytes larger than hash, to provide minimal random salt, so verify
    // that 9 bytes larger than hash won't work.
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().RsaSigningKey(
                               256 + 9 * 8, KM_DIGEST_SHA_2_256, KM_PAD_RSA_PSS)));
    string message(1024, 'a');
    string signature;

    EXPECT_EQ(KM_ERROR_OK, BeginOperation(KM_PURPOSE_SIGN));

    string result;
    size_t input_consumed;
    EXPECT_EQ(KM_ERROR_OK, UpdateOperation(message, &result, &input_consumed));
    EXPECT_EQ(message.size(), input_consumed);
    EXPECT_EQ(KM_ERROR_INCOMPATIBLE_DIGEST, FinishOperation(signature, &result));
}

TEST_F(SigningOperationsTest, EcdsaSuccess) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().EcdsaSigningKey(224)));
    string message = "123456789012345678901234567890123456789012345678";
    string signature;
    SignMessage(message, &signature);
}

TEST_F(SigningOperationsTest, RsaAbort) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().RsaSigningKey(256)));
    ASSERT_EQ(KM_ERROR_OK, BeginOperation(KM_PURPOSE_SIGN));
    EXPECT_EQ(KM_ERROR_OK, AbortOperation());
    // Another abort should fail
    EXPECT_EQ(KM_ERROR_INVALID_OPERATION_HANDLE, AbortOperation());
}

TEST_F(SigningOperationsTest, RsaUnsupportedDigest) {
    GenerateKey(
        ParamBuilder().RsaSigningKey(256, KM_DIGEST_MD5, KM_PAD_RSA_PSS /* supported padding */));
    ASSERT_EQ(KM_ERROR_UNSUPPORTED_DIGEST, BeginOperation(KM_PURPOSE_SIGN));
}

TEST_F(SigningOperationsTest, RsaUnsupportedPadding) {
    GenerateKey(ParamBuilder().RsaSigningKey(256, KM_DIGEST_SHA_2_256 /* supported digest */,
                                             KM_PAD_PKCS7));
    ASSERT_EQ(KM_ERROR_UNSUPPORTED_PADDING_MODE, BeginOperation(KM_PURPOSE_SIGN));
}

TEST_F(SigningOperationsTest, RsaNoDigest) {
    // Digest must be specified.
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().RsaKey(256).SigningKey().Option(
                               TAG_PADDING, KM_PAD_NONE)));
    ASSERT_EQ(KM_ERROR_UNSUPPORTED_DIGEST, BeginOperation(KM_PURPOSE_SIGN));
    // PSS requires a digest.
    GenerateKey(ParamBuilder().RsaSigningKey(256, KM_DIGEST_NONE, KM_PAD_RSA_PSS));
    ASSERT_EQ(KM_ERROR_INCOMPATIBLE_DIGEST, BeginOperation(KM_PURPOSE_SIGN));
}

TEST_F(SigningOperationsTest, RsaNoPadding) {
    // Padding must be specified
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().RsaKey(256).SigningKey().Option(
                               TAG_DIGEST, KM_DIGEST_NONE)));
    ASSERT_EQ(KM_ERROR_UNSUPPORTED_PADDING_MODE, BeginOperation(KM_PURPOSE_SIGN));
}

TEST_F(SigningOperationsTest, HmacSha1Success) {
    GenerateKey(ParamBuilder().HmacKey(128, KM_DIGEST_SHA1, 20));
    string message = "12345678901234567890123456789012";
    string signature;
    SignMessage(message, &signature);
    ASSERT_EQ(20, signature.size());
}

TEST_F(SigningOperationsTest, HmacSha224Success) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().HmacKey(128, KM_DIGEST_SHA_2_224, 28)));
    string message = "12345678901234567890123456789012";
    string signature;
    SignMessage(message, &signature);
    ASSERT_EQ(28, signature.size());
}

TEST_F(SigningOperationsTest, HmacSha256Success) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().HmacKey(128, KM_DIGEST_SHA_2_256, 32)));
    string message = "12345678901234567890123456789012";
    string signature;
    SignMessage(message, &signature);
    ASSERT_EQ(32, signature.size());
}

TEST_F(SigningOperationsTest, HmacSha384Success) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().HmacKey(128, KM_DIGEST_SHA_2_384, 48)));
    string message = "12345678901234567890123456789012";
    string signature;
    SignMessage(message, &signature);
    ASSERT_EQ(48, signature.size());
}

TEST_F(SigningOperationsTest, HmacSha512Success) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().HmacKey(128, KM_DIGEST_SHA_2_512, 64)));
    string message = "12345678901234567890123456789012";
    string signature;
    SignMessage(message, &signature);
    ASSERT_EQ(64, signature.size());
}

// TODO(swillden): Add HMACSHA{224|256|384|512} tests that validates against the test vectors from
//                 RFC4231.  Doing that requires being able to import keys, rather than just
//                 generate them randomly.

TEST_F(SigningOperationsTest, HmacSha256NoMacLength) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder()
                                           .Option(TAG_ALGORITHM, KM_ALGORITHM_HMAC)
                                           .Option(TAG_KEY_SIZE, 128)
                                           .SigningKey()
                                           .Option(TAG_DIGEST, KM_DIGEST_SHA_2_256)));
    EXPECT_EQ(KM_ERROR_UNSUPPORTED_MAC_LENGTH, BeginOperation(KM_PURPOSE_SIGN));
}

TEST_F(SigningOperationsTest, HmacSha256TooLargeMacLength) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().HmacKey(128, KM_DIGEST_SHA_2_256, 33)));
    ASSERT_EQ(KM_ERROR_UNSUPPORTED_MAC_LENGTH, BeginOperation(KM_PURPOSE_SIGN));
}

TEST_F(SigningOperationsTest, RsaTooShortMessage) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().RsaSigningKey(256)));
    ASSERT_EQ(KM_ERROR_OK, BeginOperation(KM_PURPOSE_SIGN));

    string message = "1234567890123456789012345678901";
    string result;
    size_t input_consumed;
    ASSERT_EQ(KM_ERROR_OK, UpdateOperation(message, &result, &input_consumed));
    EXPECT_EQ(0U, result.size());
    EXPECT_EQ(31U, input_consumed);

    string signature;
    ASSERT_EQ(KM_ERROR_UNKNOWN_ERROR, FinishOperation(&signature));
    EXPECT_EQ(0U, signature.length());
}

// TODO(swillden): Add more verification failure tests.

typedef KeymasterTest VerificationOperationsTest;
TEST_F(VerificationOperationsTest, RsaSuccess) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().RsaSigningKey(256)));
    string message = "12345678901234567890123456789012";
    string signature;
    SignMessage(message, &signature);
    VerifyMessage(message, signature);
}

TEST_F(VerificationOperationsTest, RsaSha256DigestSuccess) {
    // Note that without padding, key size must exactly match digest size.
    GenerateKey(ParamBuilder().RsaSigningKey(256, KM_DIGEST_SHA_2_256));
    string message(1024, 'a');
    string signature;
    SignMessage(message, &signature);
    VerifyMessage(message, signature);
}

TEST_F(VerificationOperationsTest, RsaSha256CorruptSignature) {
    GenerateKey(ParamBuilder().RsaSigningKey(256, KM_DIGEST_SHA_2_256));
    string message(1024, 'a');
    string signature;
    SignMessage(message, &signature);
    ++signature[signature.size() / 2];

    EXPECT_EQ(KM_ERROR_OK, BeginOperation(KM_PURPOSE_VERIFY));

    string result;
    size_t input_consumed;
    EXPECT_EQ(KM_ERROR_OK, UpdateOperation(message, &result, &input_consumed));
    EXPECT_EQ(message.size(), input_consumed);
    EXPECT_EQ(KM_ERROR_VERIFICATION_FAILED, FinishOperation(signature, &result));
}

TEST_F(VerificationOperationsTest, RsaPssSha256Success) {
    ASSERT_EQ(KM_ERROR_OK,
              GenerateKey(ParamBuilder().RsaSigningKey(512, KM_DIGEST_SHA_2_256, KM_PAD_RSA_PSS)));
    // Use large message, which won't work without digesting.
    string message(1024, 'a');
    string signature;
    SignMessage(message, &signature);
    VerifyMessage(message, signature);
}

TEST_F(VerificationOperationsTest, RsaPssSha256CorruptSignature) {
    GenerateKey(ParamBuilder().RsaSigningKey(512, KM_DIGEST_SHA_2_256, KM_PAD_RSA_PSS));
    string message(1024, 'a');
    string signature;
    SignMessage(message, &signature);
    ++signature[signature.size() / 2];

    EXPECT_EQ(KM_ERROR_OK, BeginOperation(KM_PURPOSE_VERIFY));

    string result;
    size_t input_consumed;
    EXPECT_EQ(KM_ERROR_OK, UpdateOperation(message, &result, &input_consumed));
    EXPECT_EQ(message.size(), input_consumed);
    EXPECT_EQ(KM_ERROR_VERIFICATION_FAILED, FinishOperation(signature, &result));
}

TEST_F(VerificationOperationsTest, RsaPssSha256CorruptInput) {
    ASSERT_EQ(KM_ERROR_OK,
              GenerateKey(ParamBuilder().RsaSigningKey(512, KM_DIGEST_SHA_2_256, KM_PAD_RSA_PSS)));
    // Use large message, which won't work without digesting.
    string message(1024, 'a');
    string signature;
    SignMessage(message, &signature);
    ++message[message.size() / 2];

    EXPECT_EQ(KM_ERROR_OK, BeginOperation(KM_PURPOSE_VERIFY));

    string result;
    size_t input_consumed;
    EXPECT_EQ(KM_ERROR_OK, UpdateOperation(message, &result, &input_consumed));
    EXPECT_EQ(message.size(), input_consumed);
    EXPECT_EQ(KM_ERROR_VERIFICATION_FAILED, FinishOperation(signature, &result));
}

TEST_F(VerificationOperationsTest, RsaPkcs1Sha256Success) {
    GenerateKey(ParamBuilder().RsaSigningKey(512, KM_DIGEST_SHA_2_256, KM_PAD_RSA_PKCS1_1_5_SIGN));
    string message(1024, 'a');
    string signature;
    SignMessage(message, &signature);
    VerifyMessage(message, signature);
}

TEST_F(VerificationOperationsTest, RsaPkcs1Sha256CorruptSignature) {
    GenerateKey(ParamBuilder().RsaSigningKey(512, KM_DIGEST_SHA_2_256, KM_PAD_RSA_PKCS1_1_5_SIGN));
    string message(1024, 'a');
    string signature;
    SignMessage(message, &signature);
    ++signature[signature.size() / 2];

    EXPECT_EQ(KM_ERROR_OK, BeginOperation(KM_PURPOSE_VERIFY));

    string result;
    size_t input_consumed;
    EXPECT_EQ(KM_ERROR_OK, UpdateOperation(message, &result, &input_consumed));
    EXPECT_EQ(message.size(), input_consumed);
    EXPECT_EQ(KM_ERROR_VERIFICATION_FAILED, FinishOperation(signature, &result));
}

TEST_F(VerificationOperationsTest, RsaPkcs1Sha256CorruptInput) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().RsaSigningKey(512, KM_DIGEST_SHA_2_256,
                                                                    KM_PAD_RSA_PKCS1_1_5_SIGN)));
    // Use large message, which won't work without digesting.
    string message(1024, 'a');
    string signature;
    SignMessage(message, &signature);
    ++message[message.size() / 2];

    EXPECT_EQ(KM_ERROR_OK, BeginOperation(KM_PURPOSE_VERIFY));

    string result;
    size_t input_consumed;
    EXPECT_EQ(KM_ERROR_OK, UpdateOperation(message, &result, &input_consumed));
    EXPECT_EQ(message.size(), input_consumed);
    EXPECT_EQ(KM_ERROR_VERIFICATION_FAILED, FinishOperation(signature, &result));
}

template <typename T> vector<T> make_vector(const T* array, size_t len) {
    return vector<T>(array, array + len);
}

TEST_F(VerificationOperationsTest, RsaAllDigestAndPadCombinations) {
    // Get all supported digests and padding modes.
    size_t digests_len;
    keymaster_digest_t* digests;
    EXPECT_EQ(KM_ERROR_OK,
              device()->get_supported_digests(device(), KM_ALGORITHM_RSA, KM_PURPOSE_SIGN, &digests,
                                              &digests_len));

    size_t padding_modes_len;
    keymaster_padding_t* padding_modes;
    EXPECT_EQ(KM_ERROR_OK,
              device()->get_supported_padding_modes(device(), KM_ALGORITHM_RSA, KM_PURPOSE_SIGN,
                                                    &padding_modes, &padding_modes_len));

    // Try them.
    for (keymaster_padding_t padding_mode : make_vector(padding_modes, padding_modes_len)) {
        for (keymaster_digest_t digest : make_vector(digests, digests_len)) {
            // Compute key & message size that will work.
            size_t key_bits = 256;
            size_t message_len = 1000;
            switch (digest) {
            case KM_DIGEST_NONE:
                switch (padding_mode) {
                case KM_PAD_NONE:
                    // Match key size.
                    message_len = key_bits / 8;
                    break;
                case KM_PAD_RSA_PKCS1_1_5_SIGN:
                    message_len = key_bits / 8 - 11;
                    break;
                case KM_PAD_RSA_PSS:
                    // PSS requires a digest.
                    continue;
                default:
                    FAIL() << "Missing padding";
                    break;
                }
                break;

            case KM_DIGEST_SHA_2_256:
                switch (padding_mode) {
                case KM_PAD_NONE:
                    // Key size matches digest size
                    break;
                case KM_PAD_RSA_PKCS1_1_5_SIGN:
                    key_bits += 8 * 11;
                    break;
                case KM_PAD_RSA_PSS:
                    key_bits += 8 * 10;
                    break;
                default:
                    FAIL() << "Missing padding";
                    break;
                }
                break;
            default:
                FAIL() << "Missing digest";
            }

            GenerateKey(ParamBuilder().RsaSigningKey(key_bits, digest, padding_mode));
            string message(message_len, 'a');
            string signature;
            SignMessage(message, &signature);
            VerifyMessage(message, signature);
        }
    }

    free(padding_modes);
    free(digests);
}

TEST_F(VerificationOperationsTest, EcdsaSuccess) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().EcdsaSigningKey(256)));
    string message = "123456789012345678901234567890123456789012345678";
    string signature;
    SignMessage(message, &signature);
    VerifyMessage(message, signature);
}

TEST_F(VerificationOperationsTest, HmacSha1Success) {
    GenerateKey(ParamBuilder().HmacKey(128, KM_DIGEST_SHA1, 16));
    string message = "123456789012345678901234567890123456789012345678";
    string signature;
    SignMessage(message, &signature);
    VerifyMessage(message, signature);
}

TEST_F(VerificationOperationsTest, HmacSha224Success) {
    GenerateKey(ParamBuilder().HmacKey(128, KM_DIGEST_SHA_2_224, 16));
    string message = "123456789012345678901234567890123456789012345678";
    string signature;
    SignMessage(message, &signature);
    VerifyMessage(message, signature);
}

TEST_F(VerificationOperationsTest, HmacSha256Success) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().HmacKey(128, KM_DIGEST_SHA_2_256, 16)));
    string message = "123456789012345678901234567890123456789012345678";
    string signature;
    SignMessage(message, &signature);
    VerifyMessage(message, signature);
}

TEST_F(VerificationOperationsTest, HmacSha384Success) {
    GenerateKey(ParamBuilder().HmacKey(128, KM_DIGEST_SHA_2_384, 16));
    string message = "123456789012345678901234567890123456789012345678";
    string signature;
    SignMessage(message, &signature);
    VerifyMessage(message, signature);
}

TEST_F(VerificationOperationsTest, HmacSha512Success) {
    GenerateKey(ParamBuilder().HmacKey(128, KM_DIGEST_SHA_2_512, 16));
    string message = "123456789012345678901234567890123456789012345678";
    string signature;
    SignMessage(message, &signature);
    VerifyMessage(message, signature);
}

typedef VerificationOperationsTest ExportKeyTest;
TEST_F(ExportKeyTest, RsaSuccess) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().RsaSigningKey(256)));
    string export_data;
    ASSERT_EQ(KM_ERROR_OK, ExportKey(KM_KEY_FORMAT_X509, &export_data));
    EXPECT_GT(export_data.length(), 0);

    // TODO(swillden): Verify that the exported key is actually usable to verify signatures.
}

TEST_F(ExportKeyTest, EcdsaSuccess) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().EcdsaSigningKey(224)));
    string export_data;
    ASSERT_EQ(KM_ERROR_OK, ExportKey(KM_KEY_FORMAT_X509, &export_data));
    EXPECT_GT(export_data.length(), 0);

    // TODO(swillden): Verify that the exported key is actually usable to verify signatures.
}

TEST_F(ExportKeyTest, RsaUnsupportedKeyFormat) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().RsaSigningKey(256)));
    string export_data;
    ASSERT_EQ(KM_ERROR_UNSUPPORTED_KEY_FORMAT, ExportKey(KM_KEY_FORMAT_PKCS8, &export_data));
}

TEST_F(ExportKeyTest, RsaCorruptedKeyBlob) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().RsaSigningKey(256)));
    corrupt_key_blob();
    string export_data;
    ASSERT_EQ(KM_ERROR_INVALID_KEY_BLOB, ExportKey(KM_KEY_FORMAT_X509, &export_data));
}

TEST_F(ExportKeyTest, AesKeyExportFails) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().AesEncryptionKey(128)));
    string export_data;

    EXPECT_EQ(KM_ERROR_UNSUPPORTED_KEY_FORMAT, ExportKey(KM_KEY_FORMAT_X509, &export_data));
    EXPECT_EQ(KM_ERROR_UNSUPPORTED_KEY_FORMAT, ExportKey(KM_KEY_FORMAT_PKCS8, &export_data));
    EXPECT_EQ(KM_ERROR_UNSUPPORTED_KEY_FORMAT, ExportKey(KM_KEY_FORMAT_RAW, &export_data));
}

static string read_file(const string& file_name) {
    ifstream file_stream(file_name, std::ios::binary);
    istreambuf_iterator<char> file_begin(file_stream);
    istreambuf_iterator<char> file_end;
    return string(file_begin, file_end);
}

typedef VerificationOperationsTest ImportKeyTest;
TEST_F(ImportKeyTest, RsaSuccess) {
    string pk8_key = read_file("rsa_privkey_pk8.der");
    ASSERT_EQ(633U, pk8_key.size());

    ASSERT_EQ(KM_ERROR_OK, ImportKey(ParamBuilder().RsaSigningKey().NoDigestOrPadding(),
                                     KM_KEY_FORMAT_PKCS8, pk8_key));

    // Check values derived from the key.
    EXPECT_TRUE(contains(sw_enforced(), TAG_ALGORITHM, KM_ALGORITHM_RSA));
    EXPECT_TRUE(contains(sw_enforced(), TAG_KEY_SIZE, 1024));
    EXPECT_TRUE(contains(sw_enforced(), TAG_RSA_PUBLIC_EXPONENT, 65537U));

    // And values provided by GoogleKeymaster
    EXPECT_TRUE(contains(sw_enforced(), TAG_ORIGIN, KM_ORIGIN_IMPORTED));
    EXPECT_TRUE(contains(sw_enforced(), KM_TAG_CREATION_DATETIME));

    string message(1024 / 8, 'a');
    string signature;
    SignMessage(message, &signature);
    VerifyMessage(message, signature);
}

TEST_F(ImportKeyTest, RsaKeySizeMismatch) {
    string pk8_key = read_file("rsa_privkey_pk8.der");
    ASSERT_EQ(633U, pk8_key.size());
    ASSERT_EQ(KM_ERROR_IMPORT_PARAMETER_MISMATCH,
              ImportKey(ParamBuilder()
                            .RsaSigningKey(2048)  // Size doesn't match key
                            .NoDigestOrPadding(),
                        KM_KEY_FORMAT_PKCS8, pk8_key));
}

TEST_F(ImportKeyTest, RsaPublicExponenMismatch) {
    string pk8_key = read_file("rsa_privkey_pk8.der");
    ASSERT_EQ(633U, pk8_key.size());
    ASSERT_EQ(KM_ERROR_IMPORT_PARAMETER_MISMATCH,
              ImportKey(ParamBuilder()
                            .RsaSigningKey()
                            .Option(TAG_RSA_PUBLIC_EXPONENT, 3)  // Doesn't match key
                            .NoDigestOrPadding(),
                        KM_KEY_FORMAT_PKCS8, pk8_key));
}

TEST_F(ImportKeyTest, EcdsaSuccess) {
    string pk8_key = read_file("ec_privkey_pk8.der");
    ASSERT_EQ(138U, pk8_key.size());

    ASSERT_EQ(KM_ERROR_OK,
              ImportKey(ParamBuilder().EcdsaSigningKey(), KM_KEY_FORMAT_PKCS8, pk8_key));

    // Check values derived from the key.
    EXPECT_TRUE(contains(sw_enforced(), TAG_ALGORITHM, KM_ALGORITHM_ECDSA));
    EXPECT_TRUE(contains(sw_enforced(), TAG_KEY_SIZE, 256));

    // And values provided by GoogleKeymaster
    EXPECT_TRUE(contains(sw_enforced(), TAG_ORIGIN, KM_ORIGIN_IMPORTED));
    EXPECT_TRUE(contains(sw_enforced(), KM_TAG_CREATION_DATETIME));

    string message(1024 / 8, 'a');
    string signature;
    SignMessage(message, &signature);
    VerifyMessage(message, signature);
}

TEST_F(ImportKeyTest, EcdsaSizeSpecified) {
    string pk8_key = read_file("ec_privkey_pk8.der");
    ASSERT_EQ(138U, pk8_key.size());

    ASSERT_EQ(KM_ERROR_OK,
              ImportKey(ParamBuilder().EcdsaSigningKey(256), KM_KEY_FORMAT_PKCS8, pk8_key));

    // Check values derived from the key.
    EXPECT_TRUE(contains(sw_enforced(), TAG_ALGORITHM, KM_ALGORITHM_ECDSA));
    EXPECT_TRUE(contains(sw_enforced(), TAG_KEY_SIZE, 256));

    // And values provided by GoogleKeymaster
    EXPECT_TRUE(contains(sw_enforced(), TAG_ORIGIN, KM_ORIGIN_IMPORTED));
    EXPECT_TRUE(contains(sw_enforced(), KM_TAG_CREATION_DATETIME));

    string message(1024 / 8, 'a');
    string signature;
    SignMessage(message, &signature);
    VerifyMessage(message, signature);
}

TEST_F(ImportKeyTest, EcdsaSizeMismatch) {
    string pk8_key = read_file("ec_privkey_pk8.der");
    ASSERT_EQ(138U, pk8_key.size());
    ASSERT_EQ(KM_ERROR_IMPORT_PARAMETER_MISMATCH,
              ImportKey(ParamBuilder().EcdsaSigningKey(224),  // Size does not match key
                        KM_KEY_FORMAT_PKCS8, pk8_key));
}

typedef KeymasterTest VersionTest;
TEST_F(VersionTest, GetVersion) {
    uint8_t major, minor, subminor;
    ASSERT_EQ(KM_ERROR_OK, GetVersion(&major, &minor, &subminor));
    EXPECT_EQ(1, major);
    EXPECT_EQ(0, minor);
    EXPECT_EQ(0, subminor);
}

typedef KeymasterTest EncryptionOperationsTest;
TEST_F(EncryptionOperationsTest, RsaOaepSuccess) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().RsaEncryptionKey(512, KM_PAD_RSA_OAEP)));

    string message = "Hello World!";
    string ciphertext1 = EncryptMessage(string(message));
    EXPECT_EQ(512 / 8, ciphertext1.size());

    string ciphertext2 = EncryptMessage(string(message));
    EXPECT_EQ(512 / 8, ciphertext2.size());

    // OAEP randomizes padding so every result should be different.
    EXPECT_NE(ciphertext1, ciphertext2);
}

TEST_F(EncryptionOperationsTest, RsaOaepRoundTrip) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().RsaEncryptionKey(512, KM_PAD_RSA_OAEP)));
    string message = "Hello World!";
    string ciphertext = EncryptMessage(string(message));
    EXPECT_EQ(512 / 8, ciphertext.size());

    string plaintext = DecryptMessage(ciphertext);
    EXPECT_EQ(message, plaintext);
}

TEST_F(EncryptionOperationsTest, RsaOaepTooLarge) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().RsaEncryptionKey(512, KM_PAD_RSA_OAEP)));
    string message = "12345678901234567890123";
    string result;
    size_t input_consumed;

    EXPECT_EQ(KM_ERROR_OK, BeginOperation(KM_PURPOSE_ENCRYPT));
    EXPECT_EQ(KM_ERROR_OK, UpdateOperation(message, &result, &input_consumed));
    EXPECT_EQ(KM_ERROR_INVALID_INPUT_LENGTH, FinishOperation(&result));
    EXPECT_EQ(0, result.size());
}

TEST_F(EncryptionOperationsTest, RsaOaepCorruptedDecrypt) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().RsaEncryptionKey(512, KM_PAD_RSA_OAEP)));
    string message = "Hello World!";
    string ciphertext = EncryptMessage(string(message));
    EXPECT_EQ(512 / 8, ciphertext.size());

    // Corrupt the ciphertext
    ciphertext[512 / 8 / 2]++;

    string result;
    size_t input_consumed;
    EXPECT_EQ(KM_ERROR_OK, BeginOperation(KM_PURPOSE_DECRYPT));
    EXPECT_EQ(KM_ERROR_OK, UpdateOperation(ciphertext, &result, &input_consumed));
    EXPECT_EQ(KM_ERROR_UNKNOWN_ERROR, FinishOperation(&result));
    EXPECT_EQ(0, result.size());
}

TEST_F(EncryptionOperationsTest, RsaPkcs1Success) {
    ASSERT_EQ(KM_ERROR_OK,
              GenerateKey(ParamBuilder().RsaEncryptionKey(512, KM_PAD_RSA_PKCS1_1_5_ENCRYPT)));
    string message = "Hello World!";
    string ciphertext1 = EncryptMessage(string(message));
    EXPECT_EQ(512 / 8, ciphertext1.size());

    string ciphertext2 = EncryptMessage(string(message));
    EXPECT_EQ(512 / 8, ciphertext2.size());

    // PKCS1 v1.5 randomizes padding so every result should be different.
    EXPECT_NE(ciphertext1, ciphertext2);
}

TEST_F(EncryptionOperationsTest, RsaPkcs1RoundTrip) {
    ASSERT_EQ(KM_ERROR_OK,
              GenerateKey(ParamBuilder().RsaEncryptionKey(512, KM_PAD_RSA_PKCS1_1_5_ENCRYPT)));
    string message = "Hello World!";
    string ciphertext = EncryptMessage(string(message));
    EXPECT_EQ(512 / 8, ciphertext.size());

    string plaintext = DecryptMessage(ciphertext);
    EXPECT_EQ(message, plaintext);
}

TEST_F(EncryptionOperationsTest, RsaPkcs1TooLarge) {
    ASSERT_EQ(KM_ERROR_OK,
              GenerateKey(ParamBuilder().RsaEncryptionKey(512, KM_PAD_RSA_PKCS1_1_5_ENCRYPT)));
    string message = "12345678901234567890123456789012345678901234567890123";
    string result;
    size_t input_consumed;

    EXPECT_EQ(KM_ERROR_OK, BeginOperation(KM_PURPOSE_ENCRYPT));
    EXPECT_EQ(KM_ERROR_OK, UpdateOperation(message, &result, &input_consumed));
    EXPECT_EQ(KM_ERROR_INVALID_INPUT_LENGTH, FinishOperation(&result));
    EXPECT_EQ(0, result.size());
}

TEST_F(EncryptionOperationsTest, RsaPkcs1CorruptedDecrypt) {
    ASSERT_EQ(KM_ERROR_OK,
              GenerateKey(ParamBuilder().RsaEncryptionKey(512, KM_PAD_RSA_PKCS1_1_5_ENCRYPT)));
    string message = "Hello World!";
    string ciphertext = EncryptMessage(string(message));
    EXPECT_EQ(512 / 8, ciphertext.size());

    // Corrupt the ciphertext
    ciphertext[512 / 8 / 2]++;

    string result;
    size_t input_consumed;
    EXPECT_EQ(KM_ERROR_OK, BeginOperation(KM_PURPOSE_DECRYPT));
    EXPECT_EQ(KM_ERROR_OK, UpdateOperation(ciphertext, &result, &input_consumed));
    EXPECT_EQ(KM_ERROR_UNKNOWN_ERROR, FinishOperation(&result));
    EXPECT_EQ(0, result.size());
}

TEST_F(EncryptionOperationsTest, AesOcbSuccess) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().AesEncryptionKey(128).OcbMode(4096, 16)));
    string message = "Hello World!";
    string ciphertext1 = EncryptMessage(string(message));
    EXPECT_EQ(12 /* nonce */ + message.size() + 16 /* tag */, ciphertext1.size());

    string ciphertext2 = EncryptMessage(string(message));
    EXPECT_EQ(12 /* nonce */ + message.size() + 16 /* tag */, ciphertext2.size());

    // OCB uses a random nonce, so every output should be different
    EXPECT_NE(ciphertext1, ciphertext2);
}

TEST_F(EncryptionOperationsTest, AesOcbRoundTripSuccess) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().AesEncryptionKey(128).OcbMode(4096, 16)));
    string message = "Hello World!";
    string ciphertext = EncryptMessage(message);
    EXPECT_EQ(12 /* nonce */ + message.length() + 16 /* tag */, ciphertext.size());

    string plaintext = DecryptMessage(ciphertext);
    EXPECT_EQ(message, plaintext);
}

TEST_F(EncryptionOperationsTest, AesOcbRoundTripCorrupted) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().AesEncryptionKey(128).OcbMode(4096, 16)));
    string message = "Hello World!";
    string ciphertext = EncryptMessage(string(message));
    EXPECT_EQ(12 /* nonce */ + message.size() + 16 /* tag */, ciphertext.size());

    ciphertext[ciphertext.size() / 2]++;

    EXPECT_EQ(KM_ERROR_OK, BeginOperation(KM_PURPOSE_DECRYPT));

    string result;
    size_t input_consumed;
    EXPECT_EQ(KM_ERROR_OK, UpdateOperation(ciphertext, &result, &input_consumed));
    EXPECT_EQ(ciphertext.length(), input_consumed);
    EXPECT_EQ(KM_ERROR_VERIFICATION_FAILED, FinishOperation(&result));
}

TEST_F(EncryptionOperationsTest, AesDecryptGarbage) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().AesEncryptionKey(128).OcbMode(4096, 16)));
    string ciphertext(128, 'a');
    EXPECT_EQ(KM_ERROR_OK, BeginOperation(KM_PURPOSE_DECRYPT));

    string result;
    size_t input_consumed;
    EXPECT_EQ(KM_ERROR_OK, UpdateOperation(ciphertext, &result, &input_consumed));
    EXPECT_EQ(ciphertext.length(), input_consumed);
    EXPECT_EQ(KM_ERROR_VERIFICATION_FAILED, FinishOperation(&result));
}

TEST_F(EncryptionOperationsTest, AesDecryptTooShort) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().AesEncryptionKey(128).OcbMode(4096, 16)));

    // Try decrypting garbage ciphertext that is too short to be valid (< nonce + tag).
    string ciphertext(12 + 15, 'a');
    EXPECT_EQ(KM_ERROR_OK, BeginOperation(KM_PURPOSE_DECRYPT));

    string result;
    size_t input_consumed;
    EXPECT_EQ(KM_ERROR_OK, UpdateOperation(ciphertext, &result, &input_consumed));
    EXPECT_EQ(ciphertext.length(), input_consumed);
    EXPECT_EQ(KM_ERROR_INVALID_INPUT_LENGTH, FinishOperation(&result));
}

TEST_F(EncryptionOperationsTest, AesOcbRoundTripEmptySuccess) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().AesEncryptionKey(128).OcbMode(4096, 16)));
    string message = "";
    string ciphertext = EncryptMessage(string(message));
    EXPECT_EQ(12 /* nonce */ + message.size() + 16 /* tag */, ciphertext.size());

    string plaintext = DecryptMessage(ciphertext);
    EXPECT_EQ(message, plaintext);
}

TEST_F(EncryptionOperationsTest, AesOcbRoundTripEmptyCorrupted) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().AesEncryptionKey(128).OcbMode(4096, 16)));
    string message = "";
    string ciphertext = EncryptMessage(string(message));
    EXPECT_EQ(12 /* nonce */ + message.size() + 16 /* tag */, ciphertext.size());

    ciphertext[ciphertext.size() / 2]++;

    EXPECT_EQ(KM_ERROR_OK, BeginOperation(KM_PURPOSE_DECRYPT));

    string result;
    size_t input_consumed;
    EXPECT_EQ(KM_ERROR_OK, UpdateOperation(ciphertext, &result, &input_consumed));
    EXPECT_EQ(ciphertext.length(), input_consumed);
    EXPECT_EQ(KM_ERROR_VERIFICATION_FAILED, FinishOperation(&result));
}

TEST_F(EncryptionOperationsTest, AesOcbFullChunk) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().AesEncryptionKey(128).OcbMode(4096, 16)));
    string message(4096, 'a');
    string ciphertext = EncryptMessage(message);
    EXPECT_EQ(12 /* nonce */ + message.length() + 16 /* tag */, ciphertext.size());

    string plaintext = DecryptMessage(ciphertext);
    EXPECT_EQ(message, plaintext);
}

TEST_F(EncryptionOperationsTest, AesOcbVariousChunkLengths) {
    for (unsigned chunk_length = 1; chunk_length <= 128; ++chunk_length) {
        ASSERT_EQ(KM_ERROR_OK,
                  GenerateKey(ParamBuilder().AesEncryptionKey(128).OcbMode(chunk_length, 16)));
        string message(128, 'a');
        string ciphertext = EncryptMessage(message);
        int expected_tag_count = (message.length() + chunk_length - 1) / chunk_length;
        EXPECT_EQ(12 /* nonce */ + message.length() + 16 * expected_tag_count, ciphertext.size())
            << "Unexpected ciphertext size for chunk length " << chunk_length
            << " expected tag count was " << expected_tag_count
            << " but actual tag count was probably "
            << (ciphertext.size() - message.length() - 12) / 16;

        string plaintext = DecryptMessage(ciphertext);
        EXPECT_EQ(message, plaintext);
    }
}

TEST_F(EncryptionOperationsTest, AesOcbAbort) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().AesEncryptionKey(128).OcbMode(4096, 16)));
    string message = "Hello";

    EXPECT_EQ(KM_ERROR_OK, BeginOperation(KM_PURPOSE_ENCRYPT));

    string result;
    size_t input_consumed;
    EXPECT_EQ(KM_ERROR_OK, UpdateOperation(message, &result, &input_consumed));
    EXPECT_EQ(message.length(), input_consumed);
    EXPECT_EQ(KM_ERROR_OK, AbortOperation());
}

TEST_F(EncryptionOperationsTest, AesOcbNoChunkLength) {
    EXPECT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder()
                                           .AesEncryptionKey(128)
                                           .Option(TAG_BLOCK_MODE, KM_MODE_OCB)
                                           .Option(TAG_MAC_LENGTH, 16)));
    EXPECT_EQ(KM_ERROR_INVALID_ARGUMENT, BeginOperation(KM_PURPOSE_ENCRYPT));
}

TEST_F(EncryptionOperationsTest, AesOcbPaddingUnsupported) {
    ASSERT_EQ(KM_ERROR_OK,
              GenerateKey(ParamBuilder().AesEncryptionKey(128).OcbMode(4096, 16).Option(
                  TAG_PADDING, KM_PAD_ZERO)));
    EXPECT_EQ(KM_ERROR_UNSUPPORTED_PADDING_MODE, BeginOperation(KM_PURPOSE_ENCRYPT));
}

TEST_F(EncryptionOperationsTest, AesOcbInvalidMacLength) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().AesEncryptionKey(128).OcbMode(4096, 17)));
    EXPECT_EQ(KM_ERROR_INVALID_ARGUMENT, BeginOperation(KM_PURPOSE_ENCRYPT));
}

TEST_F(EncryptionOperationsTest, AesEcbRoundTripSuccess) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().AesEncryptionKey(128).Option(TAG_BLOCK_MODE,
                                                                                   KM_MODE_ECB)));
    // Two-block message.
    string message = "12345678901234567890123456789012";
    string ciphertext1 = EncryptMessage(message);
    EXPECT_EQ(message.size(), ciphertext1.size());

    string ciphertext2 = EncryptMessage(string(message));
    EXPECT_EQ(message.size(), ciphertext2.size());

    // ECB is deterministic.
    EXPECT_EQ(ciphertext1, ciphertext2);

    string plaintext = DecryptMessage(ciphertext1);
    EXPECT_EQ(message, plaintext);
}

TEST_F(EncryptionOperationsTest, AesEcbNoPaddingWrongInputSize) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().AesEncryptionKey(128).Option(TAG_BLOCK_MODE,
                                                                                   KM_MODE_ECB)));
    // Message is slightly shorter than two blocks.
    string message = "1234567890123456789012345678901";

    EXPECT_EQ(KM_ERROR_OK, BeginOperation(KM_PURPOSE_ENCRYPT));
    string ciphertext;
    size_t input_consumed;
    EXPECT_EQ(KM_ERROR_OK, UpdateOperation(message, &ciphertext, &input_consumed));
    EXPECT_EQ(message.size(), input_consumed);
    EXPECT_EQ(KM_ERROR_INVALID_INPUT_LENGTH, FinishOperation(&ciphertext));
}

TEST_F(EncryptionOperationsTest, AesEcbPkcs7Padding) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder()
                                           .AesEncryptionKey(128)
                                           .Option(TAG_BLOCK_MODE, KM_MODE_ECB)
                                           .Option(TAG_PADDING, KM_PAD_PKCS7)));

    // Try various message lengths; all should work.
    for (int i = 0; i < 32; ++i) {
        string message(i, 'a');
        string ciphertext = EncryptMessage(message);
        EXPECT_EQ(i + 16 - (i % 16), ciphertext.size());
        string plaintext = DecryptMessage(ciphertext);
        EXPECT_EQ(message, plaintext);
    }
}

TEST_F(EncryptionOperationsTest, AesEcbPkcs7PaddingCorrupted) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder()
                                           .AesEncryptionKey(128)
                                           .Option(TAG_BLOCK_MODE, KM_MODE_ECB)
                                           .Option(TAG_PADDING, KM_PAD_PKCS7)));

    string message = "a";
    string ciphertext = EncryptMessage(message);
    EXPECT_EQ(16, ciphertext.size());
    EXPECT_NE(ciphertext, message);
    ++ciphertext[ciphertext.size() / 2];

    EXPECT_EQ(KM_ERROR_OK, BeginOperation(KM_PURPOSE_DECRYPT));
    string plaintext;
    size_t input_consumed;
    EXPECT_EQ(KM_ERROR_OK, UpdateOperation(ciphertext, &plaintext, &input_consumed));
    EXPECT_EQ(ciphertext.size(), input_consumed);
    EXPECT_EQ(KM_ERROR_INVALID_ARGUMENT, FinishOperation(&plaintext));
}

TEST_F(EncryptionOperationsTest, AesCbcRoundTripSuccess) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().AesEncryptionKey(128).Option(TAG_BLOCK_MODE,
                                                                                   KM_MODE_CBC)));
    // Two-block message.
    string message = "12345678901234567890123456789012";
    string ciphertext1 = EncryptMessage(message);
    EXPECT_EQ(message.size() + 16, ciphertext1.size());

    string ciphertext2 = EncryptMessage(string(message));
    EXPECT_EQ(message.size() + 16, ciphertext2.size());

    // CBC uses random IVs, so ciphertexts shouldn't match.
    EXPECT_NE(ciphertext1, ciphertext2);

    string plaintext = DecryptMessage(ciphertext1);
    EXPECT_EQ(message, plaintext);
}

TEST_F(EncryptionOperationsTest, AesCbcIncrementalNoPadding) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().AesEncryptionKey(128).Option(TAG_BLOCK_MODE,
                                                                                   KM_MODE_CBC)));

    int increment = 15;
    string message(240, 'a');
    EXPECT_EQ(KM_ERROR_OK, BeginOperation(KM_PURPOSE_ENCRYPT));
    string ciphertext;
    size_t input_consumed;
    for (size_t i = 0; i < message.size(); i += increment)
        EXPECT_EQ(KM_ERROR_OK,
                  UpdateOperation(message.substr(i, increment), &ciphertext, &input_consumed));
    EXPECT_EQ(KM_ERROR_OK, FinishOperation(&ciphertext));
    EXPECT_EQ(message.size() + 16, ciphertext.size());

    EXPECT_EQ(KM_ERROR_OK, BeginOperation(KM_PURPOSE_DECRYPT));
    string plaintext;
    for (size_t i = 0; i < ciphertext.size(); i += increment)
        EXPECT_EQ(KM_ERROR_OK,
                  UpdateOperation(ciphertext.substr(i, increment), &plaintext, &input_consumed));
    EXPECT_EQ(KM_ERROR_OK, FinishOperation(&plaintext));
    EXPECT_EQ(ciphertext.size() - 16, plaintext.size());
    EXPECT_EQ(message, plaintext);
}

TEST_F(EncryptionOperationsTest, AesCbcPkcs7Padding) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder()
                                           .AesEncryptionKey(128)
                                           .Option(TAG_BLOCK_MODE, KM_MODE_CBC)
                                           .Option(TAG_PADDING, KM_PAD_PKCS7)));

    // Try various message lengths; all should work.
    for (int i = 0; i < 32; ++i) {
        string message(i, 'a');
        string ciphertext = EncryptMessage(message);
        EXPECT_EQ(i + 32 - (i % 16), ciphertext.size());
        string plaintext = DecryptMessage(ciphertext);
        EXPECT_EQ(message, plaintext);
    }
}

TEST_F(EncryptionOperationsTest, AesCfbRoundTripSuccess) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().AesEncryptionKey(128).Option(TAG_BLOCK_MODE,
                                                                                   KM_MODE_CFB)));
    // Two-block message.
    string message = "12345678901234567890123456789012";
    string ciphertext1 = EncryptMessage(message);
    EXPECT_EQ(message.size() + 16, ciphertext1.size());

    string ciphertext2 = EncryptMessage(string(message));
    EXPECT_EQ(message.size() + 16, ciphertext2.size());

    // CBC uses random IVs, so ciphertexts shouldn't match.
    EXPECT_NE(ciphertext1, ciphertext2);

    string plaintext = DecryptMessage(ciphertext1);
    EXPECT_EQ(message, plaintext);
}

TEST_F(EncryptionOperationsTest, AesCfbIncrementalNoPadding) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder()
                                           .AesEncryptionKey(128)
                                           .Option(TAG_BLOCK_MODE, KM_MODE_CFB)
                                           .Option(TAG_PADDING, KM_PAD_PKCS7)));

    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().AesEncryptionKey(128).Option(TAG_BLOCK_MODE,
                                                                                   KM_MODE_CBC)));

    int increment = 15;
    string message(240, 'a');
    EXPECT_EQ(KM_ERROR_OK, BeginOperation(KM_PURPOSE_ENCRYPT));
    string ciphertext;
    size_t input_consumed;
    for (size_t i = 0; i < message.size(); i += increment)
        EXPECT_EQ(KM_ERROR_OK,
                  UpdateOperation(message.substr(i, increment), &ciphertext, &input_consumed));
    EXPECT_EQ(KM_ERROR_OK, FinishOperation(&ciphertext));
    EXPECT_EQ(message.size() + 16, ciphertext.size());

    EXPECT_EQ(KM_ERROR_OK, BeginOperation(KM_PURPOSE_DECRYPT));
    string plaintext;
    for (size_t i = 0; i < ciphertext.size(); i += increment)
        EXPECT_EQ(KM_ERROR_OK,
                  UpdateOperation(ciphertext.substr(i, increment), &plaintext, &input_consumed));
    EXPECT_EQ(KM_ERROR_OK, FinishOperation(&plaintext));
    EXPECT_EQ(ciphertext.size() - 16, plaintext.size());
    EXPECT_EQ(message, plaintext);
}

TEST_F(EncryptionOperationsTest, AesCfbPkcs7Padding) {
    ASSERT_EQ(KM_ERROR_OK, GenerateKey(ParamBuilder().AesEncryptionKey(128).Option(TAG_BLOCK_MODE,
                                                                                   KM_MODE_CFB)));

    // Try various message lengths; all should work.
    for (int i = 0; i < 32; ++i) {
        string message(i, 'a');
        string ciphertext = EncryptMessage(message);
        EXPECT_EQ(i + 16, ciphertext.size());
        string plaintext = DecryptMessage(ciphertext);
        EXPECT_EQ(message, plaintext);
    }
}

}  // namespace test
}  // namespace keymaster
