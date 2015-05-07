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

#include "openssl_err.h"

#include <openssl/asn1.h>
#include <openssl/cipher.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pkcs8.h>
#include <openssl/x509v3.h>

#include <hardware/keymaster_defs.h>
#include <keymaster/logger.h>

namespace keymaster {

static keymaster_error_t TranslateEvpError(int reason);
static keymaster_error_t TranslateASN1Error(int reason);
static keymaster_error_t TranslateCipherError(int reason);
static keymaster_error_t TranslatePKCS8Error(int reason);
static keymaster_error_t TranslateX509v3Error(int reason);

keymaster_error_t TranslateLastOpenSslError(bool log_message) {
    unsigned long error = ERR_peek_last_error();

    if (log_message) {
        LOG_D("%s", ERR_error_string(error, NULL));
    }

    int reason = ERR_GET_REASON(error);
    switch (ERR_GET_LIB(error)) {

    case ERR_LIB_EVP:
        return TranslateEvpError(reason);
    case ERR_LIB_ASN1:
        return TranslateASN1Error(reason);
    case ERR_LIB_CIPHER:
        return TranslateCipherError(reason);
    case ERR_LIB_PKCS8:
        return TranslatePKCS8Error(reason);
    case ERR_LIB_X509V3:
        return TranslateX509v3Error(reason);
    }

    LOG_E("Openssl error %d, %d", ERR_GET_LIB(error), reason);
    return KM_ERROR_UNKNOWN_ERROR;
}

keymaster_error_t TranslatePKCS8Error(int reason) {
    switch (reason) {
    case PKCS8_R_UNSUPPORTED_PRIVATE_KEY_ALGORITHM:
    case PKCS8_R_UNKNOWN_CIPHER:
        return KM_ERROR_UNSUPPORTED_ALGORITHM;

    case PKCS8_R_PRIVATE_KEY_ENCODE_ERROR:
    case PKCS8_R_PRIVATE_KEY_DECODE_ERROR:
        return KM_ERROR_INVALID_KEY_BLOB;

    case PKCS8_R_ENCODE_ERROR:
        return KM_ERROR_INVALID_ARGUMENT;

    default:
        return KM_ERROR_UNKNOWN_ERROR;
    }
}

keymaster_error_t TranslateCipherError(int reason) {
    switch (reason) {
    case CIPHER_R_DATA_NOT_MULTIPLE_OF_BLOCK_LENGTH:
    case CIPHER_R_WRONG_FINAL_BLOCK_LENGTH:
        return KM_ERROR_INVALID_INPUT_LENGTH;

    case CIPHER_R_UNSUPPORTED_KEY_SIZE:
    case CIPHER_R_BAD_KEY_LENGTH:
        return KM_ERROR_UNSUPPORTED_KEY_SIZE;

    case CIPHER_R_BAD_DECRYPT:
        return KM_ERROR_INVALID_ARGUMENT;

    case CIPHER_R_INVALID_KEY_LENGTH:
        return KM_ERROR_INVALID_KEY_BLOB;

    default:
        return KM_ERROR_UNKNOWN_ERROR;
    }
}

keymaster_error_t TranslateASN1Error(int reason) {
    switch (reason) {
    case ASN1_R_ENCODE_ERROR:
        return KM_ERROR_INVALID_ARGUMENT;

    default:
        return KM_ERROR_UNKNOWN_ERROR;
    }
}

keymaster_error_t TranslateX509v3Error(int reason) {
    switch (reason) {
    case X509V3_R_UNKNOWN_OPTION:
        return KM_ERROR_UNSUPPORTED_ALGORITHM;

    default:
        return KM_ERROR_UNKNOWN_ERROR;
    }
}

keymaster_error_t TranslateEvpError(int reason) {
    switch (reason) {

    case EVP_R_UNKNOWN_DIGEST:
        return KM_ERROR_UNSUPPORTED_DIGEST;

    case EVP_R_UNSUPPORTED_ALGORITHM:
    case EVP_R_OPERATON_NOT_INITIALIZED:
    case EVP_R_OPERATION_NOT_SUPPORTED_FOR_THIS_KEYTYPE:
        return KM_ERROR_UNSUPPORTED_ALGORITHM;

    case EVP_R_BUFFER_TOO_SMALL:
    case EVP_R_EXPECTING_AN_RSA_KEY:
    case EVP_R_EXPECTING_A_DH_KEY:
    case EVP_R_EXPECTING_A_DSA_KEY:
    case EVP_R_MISSING_PARAMETERS:
    case EVP_R_WRONG_PUBLIC_KEY_TYPE:
        return KM_ERROR_INVALID_KEY_BLOB;

    case EVP_R_DIFFERENT_PARAMETERS:
    case EVP_R_DECODE_ERROR:
        return KM_ERROR_INVALID_ARGUMENT;

    case EVP_R_DIFFERENT_KEY_TYPES:
        return KM_ERROR_INCOMPATIBLE_ALGORITHM;
    }

    return KM_ERROR_UNKNOWN_ERROR;
}

}  // namespace keymaster
