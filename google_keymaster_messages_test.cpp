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

#include <UniquePtr.h>

#include <gtest/gtest.h>

#include <keymaster/keymaster_tags.h>
#include <keymaster/google_keymaster_utils.h>

#include "google_keymaster_test_utils.h"
#include "google_softkeymaster.h"

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    return result;
}

namespace keymaster {
namespace test {

/**
 * Serialize and deserialize a message.
 */
template <typename Message>
Message* round_trip(int32_t ver, const Message& message, size_t expected_size) {
    size_t size = message.SerializedSize();
    EXPECT_EQ(expected_size, size);
    if (size == 0)
        return NULL;

    UniquePtr<uint8_t[]> buf(new uint8_t[size]);
    EXPECT_EQ(buf.get() + size, message.Serialize(buf.get(), buf.get() + size));

    Message* deserialized = new Message(ver);
    const uint8_t* p = buf.get();
    EXPECT_TRUE(deserialized->Deserialize(&p, p + size));
    EXPECT_EQ((ptrdiff_t)size, p - buf.get());
    return deserialized;
}

struct EmptyKeymasterResponse : public KeymasterResponse {
    EmptyKeymasterResponse(int32_t ver) : KeymasterResponse(ver) {}
    size_t NonErrorSerializedSize() const { return 1; }
    uint8_t* NonErrorSerialize(uint8_t* buf, const uint8_t* /* end */) const {
        *buf++ = 0;
        return buf;
    }
    bool NonErrorDeserialize(const uint8_t** buf_ptr, const uint8_t* end) {
        if (*buf_ptr >= end)
            return false;
        EXPECT_EQ(0, **buf_ptr);
        (*buf_ptr)++;
        return true;
    }
};

TEST(RoundTrip, EmptyKeymasterResponse) {
    for (int ver = 0; ver <= MAX_MESSAGE_VERSION; ++ver) {
        EmptyKeymasterResponse msg(ver);
        msg.error = KM_ERROR_OK;

        UniquePtr<EmptyKeymasterResponse> deserialized(round_trip(ver, msg, 5));
    }
}

TEST(RoundTrip, EmptyKeymasterResponseError) {
    for (int ver = 0; ver <= MAX_MESSAGE_VERSION; ++ver) {
        EmptyKeymasterResponse msg(ver);
        msg.error = KM_ERROR_MEMORY_ALLOCATION_FAILED;

        UniquePtr<EmptyKeymasterResponse> deserialized(round_trip(ver, msg, 4));
    }
}

TEST(RoundTrip, SupportedAlgorithmsResponse) {
    for (int ver = 0; ver <= MAX_MESSAGE_VERSION; ++ver) {
        SupportedAlgorithmsResponse rsp(ver);
        keymaster_algorithm_t algorithms[] = {KM_ALGORITHM_RSA, KM_ALGORITHM_DSA,
                                              KM_ALGORITHM_ECDSA};
        rsp.error = KM_ERROR_OK;
        rsp.algorithms = dup_array(algorithms);
        rsp.algorithms_length = array_length(algorithms);

        UniquePtr<SupportedAlgorithmsResponse> deserialized(round_trip(ver, rsp, 20));
        EXPECT_EQ(array_length(algorithms), deserialized->algorithms_length);
        EXPECT_EQ(0, memcmp(deserialized->algorithms, algorithms, array_size(algorithms)));
    }
}

TEST(RoundTrip, SupportedResponse) {
    for (int ver = 0; ver <= MAX_MESSAGE_VERSION; ++ver) {
        SupportedResponse<keymaster_digest_t> rsp(ver);
        keymaster_digest_t digests[] = {KM_DIGEST_NONE, KM_DIGEST_MD5, KM_DIGEST_SHA1};
        rsp.error = KM_ERROR_OK;
        rsp.SetResults(digests);

        UniquePtr<SupportedResponse<keymaster_digest_t>> deserialized(round_trip(ver, rsp, 20));
        EXPECT_EQ(array_length(digests), deserialized->results_length);
        EXPECT_EQ(0, memcmp(deserialized->results, digests, array_size(digests)));
    }
}

static keymaster_key_param_t params[] = {
    Authorization(TAG_PURPOSE, KM_PURPOSE_SIGN), Authorization(TAG_PURPOSE, KM_PURPOSE_VERIFY),
    Authorization(TAG_ALGORITHM, KM_ALGORITHM_RSA), Authorization(TAG_USER_ID, 7),
    Authorization(TAG_USER_AUTH_ID, 8), Authorization(TAG_APPLICATION_ID, "app_id", 6),
    Authorization(TAG_AUTH_TIMEOUT, 300),
};
uint8_t TEST_DATA[] = "a key blob";

TEST(RoundTrip, GenerateKeyRequest) {
    for (int ver = 0; ver <= MAX_MESSAGE_VERSION; ++ver) {
        GenerateKeyRequest req(ver);
        req.key_description.Reinitialize(params, array_length(params));
        UniquePtr<GenerateKeyRequest> deserialized(round_trip(ver, req, 78));
        EXPECT_EQ(deserialized->key_description, req.key_description);
    }
}

TEST(RoundTrip, GenerateKeyResponse) {
    for (int ver = 0; ver <= MAX_MESSAGE_VERSION; ++ver) {
        GenerateKeyResponse rsp(ver);
        rsp.error = KM_ERROR_OK;
        rsp.key_blob.key_material = dup_array(TEST_DATA);
        rsp.key_blob.key_material_size = array_length(TEST_DATA);
        rsp.enforced.Reinitialize(params, array_length(params));

        UniquePtr<GenerateKeyResponse> deserialized(round_trip(ver, rsp, 109));
        EXPECT_EQ(KM_ERROR_OK, deserialized->error);
        EXPECT_EQ(deserialized->enforced, rsp.enforced);
        EXPECT_EQ(deserialized->unenforced, rsp.unenforced);
    }
}

TEST(RoundTrip, GenerateKeyResponseTestError) {
    for (int ver = 0; ver <= MAX_MESSAGE_VERSION; ++ver) {
        GenerateKeyResponse rsp(ver);
        rsp.error = KM_ERROR_UNSUPPORTED_ALGORITHM;
        rsp.key_blob.key_material = dup_array(TEST_DATA);
        rsp.key_blob.key_material_size = array_length(TEST_DATA);
        rsp.enforced.Reinitialize(params, array_length(params));

        UniquePtr<GenerateKeyResponse> deserialized(round_trip(ver, rsp, 4));
        EXPECT_EQ(KM_ERROR_UNSUPPORTED_ALGORITHM, deserialized->error);
        EXPECT_EQ(0U, deserialized->enforced.size());
        EXPECT_EQ(0U, deserialized->unenforced.size());
        EXPECT_EQ(0U, deserialized->key_blob.key_material_size);
    }
}

TEST(RoundTrip, GetKeyCharacteristicsRequest) {
    for (int ver = 0; ver <= MAX_MESSAGE_VERSION; ++ver) {
        GetKeyCharacteristicsRequest req(ver);
        req.additional_params.Reinitialize(params, array_length(params));
        req.SetKeyMaterial("foo", 3);

        UniquePtr<GetKeyCharacteristicsRequest> deserialized(round_trip(ver, req, 85));
        EXPECT_EQ(7U, deserialized->additional_params.size());
        EXPECT_EQ(3U, deserialized->key_blob.key_material_size);
        EXPECT_EQ(0, memcmp(deserialized->key_blob.key_material, "foo", 3));
    }
}

TEST(RoundTrip, GetKeyCharacteristicsResponse) {
    for (int ver = 0; ver <= MAX_MESSAGE_VERSION; ++ver) {
        GetKeyCharacteristicsResponse msg(ver);
        msg.error = KM_ERROR_OK;
        msg.enforced.Reinitialize(params, array_length(params));
        msg.unenforced.Reinitialize(params, array_length(params));

        UniquePtr<GetKeyCharacteristicsResponse> deserialized(round_trip(ver, msg, 160));
        EXPECT_EQ(msg.enforced, deserialized->enforced);
        EXPECT_EQ(msg.unenforced, deserialized->unenforced);
    }
}

TEST(RoundTrip, BeginOperationRequest) {
    for (int ver = 0; ver <= MAX_MESSAGE_VERSION; ++ver) {
        BeginOperationRequest msg(ver);
        msg.purpose = KM_PURPOSE_SIGN;
        msg.SetKeyMaterial("foo", 3);
        msg.additional_params.Reinitialize(params, array_length(params));

        UniquePtr<BeginOperationRequest> deserialized(round_trip(ver, msg, 89));
        EXPECT_EQ(KM_PURPOSE_SIGN, deserialized->purpose);
        EXPECT_EQ(3U, deserialized->key_blob.key_material_size);
        EXPECT_EQ(0, memcmp(deserialized->key_blob.key_material, "foo", 3));
        EXPECT_EQ(msg.additional_params, deserialized->additional_params);
    }
}

TEST(RoundTrip, BeginOperationResponse) {
    for (int ver = 0; ver <= MAX_MESSAGE_VERSION; ++ver) {
        BeginOperationResponse msg(ver);
        msg.error = KM_ERROR_OK;
        msg.op_handle = 0xDEADBEEF;

        UniquePtr<BeginOperationResponse> deserialized(round_trip(ver, msg, 12));
        EXPECT_EQ(KM_ERROR_OK, deserialized->error);
        EXPECT_EQ(0xDEADBEEF, deserialized->op_handle);
    }
}

TEST(RoundTrip, BeginOperationResponseError) {
    for (int ver = 0; ver <= MAX_MESSAGE_VERSION; ++ver) {
        BeginOperationResponse msg(ver);
        msg.error = KM_ERROR_INVALID_OPERATION_HANDLE;
        msg.op_handle = 0xDEADBEEF;

        UniquePtr<BeginOperationResponse> deserialized(round_trip(ver, msg, 4));
        EXPECT_EQ(KM_ERROR_INVALID_OPERATION_HANDLE, deserialized->error);
    }
}

TEST(RoundTrip, UpdateOperationRequest) {
    for (int ver = 0; ver <= MAX_MESSAGE_VERSION; ++ver) {
        UpdateOperationRequest msg(ver);
        msg.op_handle = 0xDEADBEEF;
        msg.input.Reinitialize("foo", 3);

        UniquePtr<UpdateOperationRequest> deserialized(round_trip(ver, msg, 15));
        EXPECT_EQ(3U, deserialized->input.available_read());
        EXPECT_EQ(0, memcmp(deserialized->input.peek_read(), "foo", 3));
    }
}

TEST(RoundTrip, UpdateOperationResponse) {
    for (int ver = 0; ver <= MAX_MESSAGE_VERSION; ++ver) {
        UpdateOperationResponse msg(ver);
        msg.error = KM_ERROR_OK;
        msg.output.Reinitialize("foo", 3);
        msg.input_consumed = 99;

        UniquePtr<UpdateOperationResponse> deserialized;
        switch (ver) {
        case 0:
            deserialized.reset(round_trip(ver, msg, 11));
            break;
        case 1:
            deserialized.reset(round_trip(ver, msg, 15));
            break;
        default:
            FAIL();
        }
        EXPECT_EQ(KM_ERROR_OK, deserialized->error);
        EXPECT_EQ(3U, deserialized->output.available_read());
        EXPECT_EQ(0, memcmp(deserialized->output.peek_read(), "foo", 3));

        switch (ver) {
        case 0:
            EXPECT_EQ(0, deserialized->input_consumed);
            break;
        case 1:
            EXPECT_EQ(99, deserialized->input_consumed);
            break;
        default:
            FAIL();
        }
    }
}

TEST(RoundTrip, FinishOperationRequest) {
    for (int ver = 0; ver <= MAX_MESSAGE_VERSION; ++ver) {
        FinishOperationRequest msg(ver);
        msg.op_handle = 0xDEADBEEF;
        msg.signature.Reinitialize("bar", 3);

        UniquePtr<FinishOperationRequest> deserialized(round_trip(ver, msg, 15));
        EXPECT_EQ(0xDEADBEEF, deserialized->op_handle);
        EXPECT_EQ(3U, deserialized->signature.available_read());
        EXPECT_EQ(0, memcmp(deserialized->signature.peek_read(), "bar", 3));
    }
}

TEST(Round_Trip, FinishOperationResponse) {
    for (int ver = 0; ver <= MAX_MESSAGE_VERSION; ++ver) {
        FinishOperationResponse msg(ver);
        msg.error = KM_ERROR_OK;
        msg.output.Reinitialize("foo", 3);

        UniquePtr<FinishOperationResponse> deserialized(round_trip(ver, msg, 11));
        EXPECT_EQ(msg.error, deserialized->error);
        EXPECT_EQ(msg.output.available_read(), deserialized->output.available_read());
        EXPECT_EQ(0, memcmp(msg.output.peek_read(), deserialized->output.peek_read(),
                            msg.output.available_read()));
    }
}

TEST(RoundTrip, ImportKeyRequest) {
    for (int ver = 0; ver <= MAX_MESSAGE_VERSION; ++ver) {
        ImportKeyRequest msg(ver);
        msg.key_description.Reinitialize(params, array_length(params));
        msg.key_format = KM_KEY_FORMAT_X509;
        msg.SetKeyMaterial("foo", 3);

        UniquePtr<ImportKeyRequest> deserialized(round_trip(ver, msg, 89));
        EXPECT_EQ(msg.key_description, deserialized->key_description);
        EXPECT_EQ(msg.key_format, deserialized->key_format);
        EXPECT_EQ(msg.key_data_length, deserialized->key_data_length);
        EXPECT_EQ(0, memcmp(msg.key_data, deserialized->key_data, msg.key_data_length));
    }
}

TEST(RoundTrip, ImportKeyResponse) {
    for (int ver = 0; ver <= MAX_MESSAGE_VERSION; ++ver) {
        ImportKeyResponse msg(ver);
        msg.error = KM_ERROR_OK;
        msg.SetKeyMaterial("foo", 3);
        msg.enforced.Reinitialize(params, array_length(params));
        msg.unenforced.Reinitialize(params, array_length(params));

        UniquePtr<ImportKeyResponse> deserialized(round_trip(ver, msg, 167));
        EXPECT_EQ(msg.error, deserialized->error);
        EXPECT_EQ(msg.key_blob.key_material_size, deserialized->key_blob.key_material_size);
        EXPECT_EQ(0, memcmp(msg.key_blob.key_material, deserialized->key_blob.key_material,
                            msg.key_blob.key_material_size));
        EXPECT_EQ(msg.enforced, deserialized->enforced);
        EXPECT_EQ(msg.unenforced, deserialized->unenforced);
    }
}

TEST(RoundTrip, ExportKeyRequest) {
    for (int ver = 0; ver <= MAX_MESSAGE_VERSION; ++ver) {
        ExportKeyRequest msg(ver);
        msg.additional_params.Reinitialize(params, array_length(params));
        msg.key_format = KM_KEY_FORMAT_X509;
        msg.SetKeyMaterial("foo", 3);

        UniquePtr<ExportKeyRequest> deserialized(round_trip(ver, msg, 89));
        EXPECT_EQ(msg.additional_params, deserialized->additional_params);
        EXPECT_EQ(msg.key_format, deserialized->key_format);
        EXPECT_EQ(3U, deserialized->key_blob.key_material_size);
        EXPECT_EQ(0, memcmp("foo", deserialized->key_blob.key_material, 3));
    }
}

TEST(RoundTrip, ExportKeyResponse) {
    for (int ver = 0; ver <= MAX_MESSAGE_VERSION; ++ver) {
        ExportKeyResponse msg(ver);
        msg.error = KM_ERROR_OK;
        msg.SetKeyMaterial("foo", 3);

        UniquePtr<ExportKeyResponse> deserialized(round_trip(ver, msg, 11));
        EXPECT_EQ(3U, deserialized->key_data_length);
        EXPECT_EQ(0, memcmp("foo", deserialized->key_data, 3));
    }
}

TEST(RoundTrip, GetVersionRequest) {
    GetVersionRequest msg;

    size_t size = msg.SerializedSize();
    ASSERT_EQ(0, size);

    UniquePtr<uint8_t[]> buf(new uint8_t[size]);
    EXPECT_EQ(buf.get() + size, msg.Serialize(buf.get(), buf.get() + size));

    GetVersionRequest deserialized;
    const uint8_t* p = buf.get();
    EXPECT_TRUE(deserialized.Deserialize(&p, p + size));
    EXPECT_EQ((ptrdiff_t)size, p - buf.get());
}

TEST(RoundTrip, GetVersionResponse) {
    GetVersionResponse msg;
    msg.error = KM_ERROR_OK;
    msg.major_ver = 9;
    msg.minor_ver = 98;
    msg.subminor_ver = 38;

    size_t size = msg.SerializedSize();
    ASSERT_EQ(7, size);

    UniquePtr<uint8_t[]> buf(new uint8_t[size]);
    EXPECT_EQ(buf.get() + size, msg.Serialize(buf.get(), buf.get() + size));

    GetVersionResponse deserialized;
    const uint8_t* p = buf.get();
    EXPECT_TRUE(deserialized.Deserialize(&p, p + size));
    EXPECT_EQ((ptrdiff_t)size, p - buf.get());
    EXPECT_EQ(9, msg.major_ver);
    EXPECT_EQ(98, msg.minor_ver);
    EXPECT_EQ(38, msg.subminor_ver);
}

uint8_t msgbuf[] = {
    220, 88,  183, 255, 71,  1,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   173, 0,   0,   0,   228, 174, 98,  187, 191, 135, 253, 200, 51,  230, 114, 247, 151, 109,
    237, 79,  87,  32,  94,  5,   204, 46,  154, 30,  91,  6,   103, 148, 254, 129, 65,  171, 228,
    167, 224, 163, 9,   15,  206, 90,  58,  11,  205, 55,  211, 33,  87,  178, 149, 91,  28,  236,
    218, 112, 231, 34,  82,  82,  134, 103, 137, 115, 27,  156, 102, 159, 220, 226, 89,  42,  25,
    37,  9,   84,  239, 76,  161, 198, 72,  167, 163, 39,  91,  148, 191, 17,  191, 87,  169, 179,
    136, 10,  194, 154, 4,   40,  107, 109, 61,  161, 20,  176, 247, 13,  214, 106, 229, 45,  17,
    5,   60,  189, 64,  39,  166, 208, 14,  57,  25,  140, 148, 25,  177, 246, 189, 43,  181, 88,
    204, 29,  126, 224, 100, 143, 93,  60,  57,  249, 55,  0,   87,  83,  227, 224, 166, 59,  214,
    81,  144, 129, 58,  6,   57,  46,  254, 232, 41,  220, 209, 230, 167, 138, 158, 94,  180, 125,
    247, 26,  162, 116, 238, 202, 187, 100, 65,  13,  180, 44,  245, 159, 83,  161, 176, 58,  72,
    236, 109, 105, 160, 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   11,  0,   0,   0,   98,  0,   0,   0,   1,   0,   0,   32,  2,   0,   0,   0,   1,   0,
    0,   32,  3,   0,   0,   0,   2,   0,   0,   16,  1,   0,   0,   0,   3,   0,   0,   48,  0,
    1,   0,   0,   200, 0,   0,   80,  3,   0,   0,   0,   0,   0,   0,   0,   244, 1,   0,   112,
    1,   246, 1,   0,   112, 1,   189, 2,   0,   96,  144, 178, 236, 250, 255, 255, 255, 255, 145,
    1,   0,   96,  144, 226, 33,  60,  222, 2,   0,   0,   189, 2,   0,   96,  0,   0,   0,   0,
    0,   0,   0,   0,   190, 2,   0,   16,  1,   0,   0,   0,   12,  0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   110, 0,   0,   0,   0,   0,   0,   0,   11,  0,
    0,   0,   98,  0,   0,   0,   1,   0,   0,   32,  2,   0,   0,   0,   1,   0,   0,   32,  3,
    0,   0,   0,   2,   0,   0,   16,  1,   0,   0,   0,   3,   0,   0,   48,  0,   1,   0,   0,
    200, 0,   0,   80,  3,   0,   0,   0,   0,   0,   0,   0,   244, 1,   0,   112, 1,   246, 1,
    0,   112, 1,   189, 2,   0,   96,  144, 178, 236, 250, 255, 255, 255, 255, 145, 1,   0,   96,
    144, 226, 33,  60,  222, 2,   0,   0,   189, 2,   0,   96,  0,   0,   0,   0,   0,   0,   0,
    0,   190, 2,   0,   16,  1,   0,   0,   0,
};

/*
 * These tests don't have any assertions or expectations. They just try to parse garbage, to see if
 * the result will be a crash.  This is especially informative when run under Valgrind memcheck.
 */

template <typename Message> void parse_garbage() {
    for (int32_t ver = 0; ver <= MAX_MESSAGE_VERSION; ++ver) {
        Message msg(ver);
        const uint8_t* end = msgbuf + array_length(msgbuf);
        for (size_t i = 0; i < array_length(msgbuf); ++i) {
            const uint8_t* begin = msgbuf + i;
            const uint8_t* p = begin;
            msg.Deserialize(&p, end);
        }
    }
}

#define GARBAGE_TEST(Message)                                                                      \
    TEST(GarbageTest, Message) { parse_garbage<Message>(); }

GARBAGE_TEST(SupportedAlgorithmsResponse)
GARBAGE_TEST(GenerateKeyRequest);
GARBAGE_TEST(GenerateKeyResponse);
GARBAGE_TEST(GetKeyCharacteristicsRequest);
GARBAGE_TEST(GetKeyCharacteristicsResponse);
GARBAGE_TEST(BeginOperationRequest);
GARBAGE_TEST(BeginOperationResponse);
GARBAGE_TEST(UpdateOperationRequest);
GARBAGE_TEST(UpdateOperationResponse);
GARBAGE_TEST(FinishOperationRequest);
GARBAGE_TEST(FinishOperationResponse);
// GARBAGE_TEST(AddEntropyRequest);
GARBAGE_TEST(ImportKeyRequest);
GARBAGE_TEST(ImportKeyResponse);
GARBAGE_TEST(ExportKeyRequest);
GARBAGE_TEST(ExportKeyResponse);
// GARBAGE_TEST(RescopeRequest);
// GARBAGE_TEST(RescopeResponse);

// The macro doesn't work on this one.
TEST(GarbageTest, SupportedResponse) {
    parse_garbage<SupportedResponse<keymaster_digest_t>>();
}

}  // namespace test

}  // namespace keymaster
