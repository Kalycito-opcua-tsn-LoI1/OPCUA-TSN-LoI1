/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#define _XOPEN_SOURCE 500
#include <stdlib.h>
#include <stdio.h>

#include "ua_types.h"
#include "ua_server.h"
#include "ua_types_generated.h"
#include "ua_types_generated_handling.h"
#include "ua_types_encoding_binary.h"
#include "ua_util.h"
#include "check.h"

/* Define types to a dummy value if they are not available (e.g. not built with
 * NS0 full) */
#ifndef UA_TYPES_UNION
#define UA_TYPES_UNION UA_TYPES_COUNT
#endif
#ifndef UA_TYPES_HISTORYREADDETAILS
#define UA_TYPES_HISTORYREADDETAILS UA_TYPES_COUNT
#endif
#ifndef UA_TYPES_NOTIFICATIONDATA
#define UA_TYPES_NOTIFICATIONDATA UA_TYPES_COUNT
#endif
#ifndef UA_TYPES_MONITORINGFILTER
#define UA_TYPES_MONITORINGFILTER UA_TYPES_COUNT
#endif
#ifndef UA_TYPES_MONITORINGFILTERRESULT
#define UA_TYPES_MONITORINGFILTERRESULT UA_TYPES_COUNT
#endif
#ifndef UA_TYPES_DATASETREADERMESSAGEDATATYPE
#define UA_TYPES_DATASETREADERMESSAGEDATATYPE UA_TYPES_COUNT
#endif
#ifndef UA_TYPES_WRITERGROUPTRANSPORTDATATYPE
#define UA_TYPES_WRITERGROUPTRANSPORTDATATYPE UA_TYPES_COUNT
#endif
#ifndef UA_TYPES_CONNECTIONTRANSPORTDATATYPE
#define UA_TYPES_CONNECTIONTRANSPORTDATATYPE UA_TYPES_COUNT
#endif
#ifndef UA_TYPES_WRITERGROUPMESSAGEDATATYPE
#define UA_TYPES_WRITERGROUPMESSAGEDATATYPE UA_TYPES_COUNT
#endif
#ifndef UA_TYPES_READERGROUPTRANSPORTDATATYPE
#define UA_TYPES_READERGROUPTRANSPORTDATATYPE UA_TYPES_COUNT
#endif
#ifndef UA_TYPES_PUBLISHEDDATASETSOURCEDATATYPE
#define UA_TYPES_PUBLISHEDDATASETSOURCEDATATYPE UA_TYPES_COUNT
#endif
#ifndef UA_TYPES_DATASETREADERTRANSPORTDATATYPE
#define UA_TYPES_DATASETREADERTRANSPORTDATATYPE UA_TYPES_COUNT
#endif
#ifndef UA_TYPES_DATASETWRITERTRANSPORTDATATYPE
#define UA_TYPES_DATASETWRITERTRANSPORTDATATYPE UA_TYPES_COUNT
#endif
#ifndef UA_TYPES_SUBSCRIBEDDATASETDATATYPE
#define UA_TYPES_SUBSCRIBEDDATASETDATATYPE UA_TYPES_COUNT
#endif
#ifndef UA_TYPES_READERGROUPMESSAGEDATATYPE
#define UA_TYPES_READERGROUPMESSAGEDATATYPE UA_TYPES_COUNT
#endif
#ifndef UA_TYPES_DATASETWRITERMESSAGEDATATYPE
#define UA_TYPES_DATASETWRITERMESSAGEDATATYPE UA_TYPES_COUNT
#endif

START_TEST(newAndEmptyObjectShallBeDeleted) {
    // given
    void *obj = UA_new(&UA_TYPES[_i]);
    // then
    ck_assert_ptr_ne(obj, NULL);
    // finally
    UA_delete(obj, &UA_TYPES[_i]);
}
END_TEST

START_TEST(arrayCopyShallMakeADeepCopy) {
    // given
    UA_String a1[3];
    a1[0] = (UA_String){1, (UA_Byte*)"a"};
    a1[1] = (UA_String){2, (UA_Byte*)"bb"};
    a1[2] = (UA_String){3, (UA_Byte*)"ccc"};
    // when
    UA_String *a2;
    UA_Int32 retval = UA_Array_copy((const void *)a1, 3, (void **)&a2, &UA_TYPES[UA_TYPES_STRING]);
    // then
    ck_assert_int_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert_int_eq(a1[0].length, 1);
    ck_assert_int_eq(a1[1].length, 2);
    ck_assert_int_eq(a1[2].length, 3);
    ck_assert_int_eq(a1[0].length, a2[0].length);
    ck_assert_int_eq(a1[1].length, a2[1].length);
    ck_assert_int_eq(a1[2].length, a2[2].length);
    ck_assert_ptr_ne(a1[0].data, a2[0].data);
    ck_assert_ptr_ne(a1[1].data, a2[1].data);
    ck_assert_ptr_ne(a1[2].data, a2[2].data);
    ck_assert_int_eq(a1[0].data[0], a2[0].data[0]);
    ck_assert_int_eq(a1[1].data[0], a2[1].data[0]);
    ck_assert_int_eq(a1[2].data[0], a2[2].data[0]);
    // finally
    UA_Array_delete((void *)a2, 3, &UA_TYPES[UA_TYPES_STRING]);
}
END_TEST

START_TEST(encodeShallYieldDecode) {
    /* floating point types may change the representaton due to several possible NaN values. */
    if(_i != UA_TYPES_FLOAT || _i != UA_TYPES_DOUBLE ||
       _i != UA_TYPES_CREATESESSIONREQUEST || _i != UA_TYPES_CREATESESSIONRESPONSE ||
       _i != UA_TYPES_VARIABLEATTRIBUTES || _i != UA_TYPES_READREQUEST ||
       _i != UA_TYPES_MONITORINGPARAMETERS || _i != UA_TYPES_MONITOREDITEMCREATERESULT ||
       _i != UA_TYPES_CREATESUBSCRIPTIONREQUEST || _i != UA_TYPES_CREATESUBSCRIPTIONRESPONSE)
        return;

    // given
    UA_ByteString msg1, msg2;
    void *obj1 = UA_new(&UA_TYPES[_i]);
    UA_StatusCode retval = UA_ByteString_allocBuffer(&msg1, 65000); // fixed buf size
    ck_assert_int_eq(retval, UA_STATUSCODE_GOOD);
    UA_Byte *pos = msg1.data;
    const UA_Byte *end = &msg1.data[msg1.length];
    retval = UA_encodeBinary(obj1, &UA_TYPES[_i],
                             &pos, &end, NULL, NULL);
    if(retval != UA_STATUSCODE_GOOD) {
        UA_delete(obj1, &UA_TYPES[_i]);
        UA_ByteString_deleteMembers(&msg1);
        return;
    }

    // when
    void *obj2 = UA_new(&UA_TYPES[_i]);
    size_t offset = 0;
    retval = UA_decodeBinary(&msg1, &offset, obj2, &UA_TYPES[_i], 0, NULL);
    ck_assert_msg(retval == UA_STATUSCODE_GOOD, "could not decode idx=%d,nodeid=%i",
                  _i, UA_TYPES[_i].typeId.identifier.numeric);
    ck_assert(!memcmp(obj1, obj2, UA_TYPES[_i].memSize)); // bit identical decoding
    retval = UA_ByteString_allocBuffer(&msg2, 65000);
    ck_assert_int_eq(retval, UA_STATUSCODE_GOOD);
    pos = msg2.data;
    end = &msg2.data[msg2.length];
    retval = UA_encodeBinary(obj2, &UA_TYPES[_i], &pos, &end, NULL, NULL);
    ck_assert_int_eq(retval, UA_STATUSCODE_GOOD);

    // then
    msg1.length = offset;
    msg2.length = offset;
    ck_assert_msg(UA_ByteString_equal(&msg1, &msg2) == true,
                  "messages differ idx=%d,nodeid=%i", _i,
                  UA_TYPES[_i].typeId.identifier.numeric);

    // finally
    UA_delete(obj1, &UA_TYPES[_i]);
    UA_delete(obj2, &UA_TYPES[_i]);
    UA_ByteString_deleteMembers(&msg1);
    UA_ByteString_deleteMembers(&msg2);
}
END_TEST

START_TEST(decodeShallFailWithTruncatedBufferButSurvive) {
    //Skip test for void*
    if (
#ifdef UA_ENABLE_DISCOVERY
        _i == UA_TYPES_DISCOVERYCONFIGURATION ||
#endif
        _i == UA_TYPES_FILTEROPERAND ||
        _i == UA_TYPES_UNION ||
        _i == UA_TYPES_HISTORYREADDETAILS ||
        _i == UA_TYPES_NOTIFICATIONDATA ||
        _i == UA_TYPES_MONITORINGFILTER ||
        _i == UA_TYPES_MONITORINGFILTERRESULT ||
        _i == UA_TYPES_DATASETREADERMESSAGEDATATYPE ||
        _i == UA_TYPES_WRITERGROUPTRANSPORTDATATYPE ||
        _i == UA_TYPES_CONNECTIONTRANSPORTDATATYPE ||
        _i == UA_TYPES_WRITERGROUPMESSAGEDATATYPE ||
        _i == UA_TYPES_READERGROUPTRANSPORTDATATYPE ||
        _i == UA_TYPES_PUBLISHEDDATASETSOURCEDATATYPE ||
        _i == UA_TYPES_DATASETREADERTRANSPORTDATATYPE ||
        _i == UA_TYPES_DATASETWRITERTRANSPORTDATATYPE ||
        _i == UA_TYPES_SUBSCRIBEDDATASETDATATYPE ||
        _i == UA_TYPES_READERGROUPMESSAGEDATATYPE ||
        _i == UA_TYPES_DATASETWRITERMESSAGEDATATYPE)
        return;
    // given
    UA_ByteString msg1;
    void *obj1 = UA_new(&UA_TYPES[_i]);
    UA_StatusCode retval = UA_ByteString_allocBuffer(&msg1, 65000); // fixed buf size
    UA_Byte *pos = msg1.data;
    const UA_Byte *end = &msg1.data[msg1.length];
    retval |= UA_encodeBinary(obj1, &UA_TYPES[_i], &pos, &end, NULL, NULL);
    UA_delete(obj1, &UA_TYPES[_i]);
    if(retval != UA_STATUSCODE_GOOD) {
        UA_ByteString_deleteMembers(&msg1);
        return; // e.g. variants cannot be encoded after an init without failing (no datatype set)
    }

    size_t half = (uintptr_t)(pos - msg1.data) / 2;
    msg1.length = half;

    // when
    void *obj2 = UA_new(&UA_TYPES[_i]);
    size_t offset = 0;
    retval = UA_decodeBinary(&msg1, &offset, obj2, &UA_TYPES[_i], 0, NULL);
    ck_assert_int_ne(retval, UA_STATUSCODE_GOOD);
    UA_delete(obj2, &UA_TYPES[_i]);
    UA_ByteString_deleteMembers(&msg1);
}
END_TEST

#define RANDOM_TESTS 1000

START_TEST(decodeScalarBasicTypeFromRandomBufferShallSucceed) {
    // given
    void *obj1 = NULL;
    UA_ByteString msg1;
    UA_Int32 retval = UA_STATUSCODE_GOOD;
    UA_Int32 buflen = 256;
    retval = UA_ByteString_allocBuffer(&msg1, buflen); // fixed size
#ifdef _WIN32
    srand(42);
#else
    srandom(42);
#endif
    for(int n = 0;n < RANDOM_TESTS;n++) {
        for(UA_Int32 i = 0;i < buflen;i++) {
#ifdef _WIN32
            UA_UInt32 rnd;
            rnd = rand();
            msg1.data[i] = rnd;
#else
            msg1.data[i] = (UA_Byte)random();  // when
#endif
        }
        size_t pos = 0;
        obj1 = UA_new(&UA_TYPES[_i]);
        retval |= UA_decodeBinary(&msg1, &pos, obj1, &UA_TYPES[_i], 0, NULL);
        //then
        ck_assert_msg(retval == UA_STATUSCODE_GOOD,
                      "Decoding %d from random buffer",
                      UA_TYPES[_i].typeId.identifier.numeric);
        // finally
        UA_delete(obj1, &UA_TYPES[_i]);
    }
    UA_ByteString_deleteMembers(&msg1);
}
END_TEST

START_TEST(decodeComplexTypeFromRandomBufferShallSurvive) {
    // given
    UA_ByteString msg1;
    UA_Int32 retval = UA_STATUSCODE_GOOD;
    UA_Int32 buflen = 256;
    retval = UA_ByteString_allocBuffer(&msg1, buflen); // fixed size
#ifdef _WIN32
    srand(42);
#else
    srandom(42);
#endif
    // when
    for(int n = 0;n < RANDOM_TESTS;n++) {
        for(UA_Int32 i = 0;i < buflen;i++) {
#ifdef _WIN32
            UA_UInt32 rnd;
            rnd = rand();
            msg1.data[i] = rnd;
#else
            msg1.data[i] = (UA_Byte)random();  // when
#endif
        }
        size_t pos = 0;
        void *obj1 = UA_new(&UA_TYPES[_i]);
        retval |= UA_decodeBinary(&msg1, &pos, obj1, &UA_TYPES[_i], 0, NULL);
        UA_delete(obj1, &UA_TYPES[_i]);
    }

    // finally
    UA_ByteString_deleteMembers(&msg1);
}
END_TEST

START_TEST(calcSizeBinaryShallBeCorrect) {
    /* Empty variants (with no type defined) cannot be encoded. This is
     * intentional. Discovery configuration is just a base class and void * */
    if(_i == UA_TYPES_VARIANT ||
       _i == UA_TYPES_VARIABLEATTRIBUTES ||
       _i == UA_TYPES_VARIABLETYPEATTRIBUTES ||
       _i == UA_TYPES_FILTEROPERAND ||
#ifdef UA_ENABLE_DISCOVERY
       _i == UA_TYPES_DISCOVERYCONFIGURATION ||
#endif
       _i == UA_TYPES_UNION ||
       _i == UA_TYPES_HISTORYREADDETAILS ||
       _i == UA_TYPES_NOTIFICATIONDATA ||
       _i == UA_TYPES_MONITORINGFILTER ||
        _i == UA_TYPES_MONITORINGFILTERRESULT ||
        _i == UA_TYPES_DATASETREADERMESSAGEDATATYPE ||
        _i == UA_TYPES_WRITERGROUPTRANSPORTDATATYPE ||
        _i == UA_TYPES_CONNECTIONTRANSPORTDATATYPE ||
        _i == UA_TYPES_WRITERGROUPMESSAGEDATATYPE ||
        _i == UA_TYPES_READERGROUPTRANSPORTDATATYPE ||
        _i == UA_TYPES_PUBLISHEDDATASETSOURCEDATATYPE ||
        _i == UA_TYPES_DATASETREADERTRANSPORTDATATYPE ||
        _i == UA_TYPES_DATASETWRITERTRANSPORTDATATYPE ||
        _i == UA_TYPES_SUBSCRIBEDDATASETDATATYPE ||
        _i == UA_TYPES_READERGROUPMESSAGEDATATYPE ||
        _i == UA_TYPES_DATASETWRITERMESSAGEDATATYPE)
        return;
    void *obj = UA_new(&UA_TYPES[_i]);
    size_t predicted_size = UA_calcSizeBinary(obj, &UA_TYPES[_i]);
    ck_assert_int_ne(predicted_size, 0);
    UA_ByteString msg;
    UA_StatusCode retval = UA_ByteString_allocBuffer(&msg, predicted_size);
    ck_assert_int_eq(retval, UA_STATUSCODE_GOOD);
    UA_Byte *pos = msg.data;
    const UA_Byte *end = &msg.data[msg.length];
    retval = UA_encodeBinary(obj, &UA_TYPES[_i], &pos, &end, NULL, NULL);
    if(retval)
        printf("%i\n",_i);
    ck_assert_int_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert_int_eq((uintptr_t)(pos - msg.data), predicted_size);
    UA_delete(obj, &UA_TYPES[_i]);
    UA_ByteString_deleteMembers(&msg);
}
END_TEST

int main(void) {
    int number_failed = 0;
    SRunner *sr;

    Suite *s  = suite_create("testMemoryHandling");
    TCase *tc = tcase_create("Empty Objects");
    tcase_add_loop_test(tc, newAndEmptyObjectShallBeDeleted, UA_TYPES_BOOLEAN, UA_TYPES_COUNT - 1);
    tcase_add_test(tc, arrayCopyShallMakeADeepCopy);
    tcase_add_loop_test(tc, encodeShallYieldDecode, UA_TYPES_BOOLEAN, UA_TYPES_COUNT - 1);
    suite_add_tcase(s, tc);
    tc = tcase_create("Truncated Buffers");
    tcase_add_loop_test(tc, decodeShallFailWithTruncatedBufferButSurvive,
                        UA_TYPES_BOOLEAN, UA_TYPES_COUNT - 1);
    suite_add_tcase(s, tc);

    tc = tcase_create("Fuzzing with Random Buffers");
    tcase_add_loop_test(tc, decodeScalarBasicTypeFromRandomBufferShallSucceed,
                        UA_TYPES_BOOLEAN, UA_TYPES_DOUBLE);
    tcase_add_loop_test(tc, decodeComplexTypeFromRandomBufferShallSurvive,
                        UA_TYPES_NODEID, UA_TYPES_COUNT - 1);
    suite_add_tcase(s, tc);

    tc = tcase_create("Test calcSizeBinary");
    tcase_add_loop_test(tc, calcSizeBinaryShallBeCorrect, UA_TYPES_BOOLEAN, UA_TYPES_COUNT - 1);
    suite_add_tcase(s, tc);

    sr = srunner_create(s);
    srunner_set_fork_status(sr, CK_NOFORK);
    srunner_run_all (sr, CK_NORMAL);
    number_failed += srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
