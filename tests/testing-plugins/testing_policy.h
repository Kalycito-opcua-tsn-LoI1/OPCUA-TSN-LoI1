/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef OPEN62541_TESTING_POLICY_H
#define OPEN62541_TESTING_POLICY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ua_plugin_securitypolicy.h"
#include "ua_plugin_log.h"

typedef struct funcs_called {
    bool asym_enc;
    bool asym_dec;

    bool sym_enc;
    bool sym_dec;

    bool asym_sign;
    bool asym_verify;

    bool sym_sign;
    bool sym_verify;

    bool newContext;
    bool deleteContext;

    bool makeCertificateThumbprint;
    bool generateKey;
    bool generateNonce;

    bool setLocalSymEncryptingKey;
    bool setLocalSymSigningKey;
    bool setLocalSymIv;
    bool setRemoteSymEncryptingKey;
    bool setRemoteSymSigningKey;
    bool setRemoteSymIv;
} funcs_called;

typedef struct key_sizes {
    size_t sym_enc_blockSize;
    size_t sym_sig_keyLen;
    size_t sym_sig_size;
    size_t sym_enc_keyLen;

    size_t asym_rmt_sig_size;
    size_t asym_lcl_sig_size;
    size_t asym_rmt_ptext_blocksize;
    size_t asym_rmt_blocksize;
    size_t asym_rmt_enc_key_size;
    size_t asym_lcl_enc_key_size;
} key_sizes;

UA_StatusCode UA_EXPORT
TestingPolicy(UA_SecurityPolicy *policy, UA_ByteString localCertificate,
              funcs_called *fCalled, const key_sizes *kSizes);

#ifdef __cplusplus
}
#endif

#endif //OPEN62541_TESTING_POLICY_H
