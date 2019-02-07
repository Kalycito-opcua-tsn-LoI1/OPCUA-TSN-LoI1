/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ua_types.h"
#include "ua_server.h"
#include "ua_client.h"
#include "client/ua_client_internal.h"
#include "ua_config_default.h"
#include "ua_client_highlevel.h"
#include "ua_network_tcp.h"
#include "testing_clock.h"
#include "testing_networklayers.h"
#include "check.h"
#include "thread_wrapper.h"

UA_Server *server;
UA_ServerConfig *config;
UA_Boolean *running;
THREAD_HANDLE server_thread;

THREAD_CALLBACK(serverloop) {
    while(*running)
        UA_Server_run_iterate(server, true);
    return 0;
}

static void setup(void) {
    running = UA_Boolean_new();
    *running = true;
    config = UA_ServerConfig_new_default();
    server = UA_Server_new(config);
    UA_Server_run_startup(server);
    THREAD_CREATE(server_thread, serverloop);
    UA_realSleep(100);
}

static void teardown(void) {
    *running = false;
    THREAD_JOIN(server_thread);
    UA_Server_run_shutdown(server);
    UA_Boolean_delete(running);
    UA_Server_delete(server);
    UA_ServerConfig_delete(config);
}

START_TEST(SecureChannel_timeout_max) {
    UA_Client *client = UA_Client_new(UA_ClientConfig_default);
    UA_StatusCode retval = UA_Client_connect(client, "opc.tcp://localhost:4840");
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    UA_fakeSleep(UA_ClientConfig_default.secureChannelLifeTime);

    UA_Variant val;
    UA_NodeId nodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER_SERVERSTATUS_STATE);
    retval = UA_Client_readValueAttribute(client, nodeId, &val);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    UA_Variant_deleteMembers(&val);

    UA_Client_disconnect(client);
    UA_Client_delete(client);
}
END_TEST

/* Send the next message after the securechannel timed out */
START_TEST(SecureChannel_timeout_fail) {
    UA_Client *client = UA_Client_new(UA_ClientConfig_default);
    UA_StatusCode retval = UA_Client_connect(client, "opc.tcp://localhost:4840");
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    UA_fakeSleep(UA_ClientConfig_default.secureChannelLifeTime+1);
    UA_realSleep(50 + 1); // UA_MAXTIMEOUT+1 wait to be sure UA_Server_run_iterate can be completely executed

    UA_Variant val;
    UA_Variant_init(&val);
    UA_NodeId nodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER_SERVERSTATUS_STATE);
    retval = UA_Client_readValueAttribute(client, nodeId, &val);
    ck_assert(retval != UA_STATUSCODE_GOOD);

    UA_Variant_deleteMembers(&val);

    UA_Client_disconnect(client);
    UA_Client_delete(client);
}
END_TEST

/* Send an async message and receive the response when the securechannel timed out */
START_TEST(SecureChannel_networkfail) {
    UA_Client *client = UA_Client_new(UA_ClientConfig_default);
    UA_StatusCode retval = UA_Client_connect(client, "opc.tcp://localhost:4840");
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    UA_ReadRequest rq;
    UA_ReadRequest_init(&rq);
    UA_ReadValueId rvi;
    UA_ReadValueId_init(&rvi);
    rvi.attributeId = UA_ATTRIBUTEID_VALUE;
    rvi.nodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER_SERVERSTATUS_STATE);
    rq.nodesToRead = &rvi;
    rq.nodesToReadSize = 1;

    /* Forward the clock after recv in the client */
    UA_Client_recv = client->connection.recv;
    client->connection.recv = UA_Client_recvTesting;
    UA_Client_recvSleepDuration = UA_ClientConfig_default.secureChannelLifeTime+1;

    UA_Variant val;
    UA_Variant_init(&val);
    UA_NodeId nodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER_SERVERSTATUS_STATE);
    retval = UA_Client_readValueAttribute(client, nodeId, &val);
    ck_assert_msg(retval == UA_STATUSCODE_BADSECURECHANNELCLOSED);

    UA_Client_disconnect(client);
    UA_Client_delete(client);
}
END_TEST

START_TEST(SecureChannel_reconnect) {
    UA_Client *client = UA_Client_new(UA_ClientConfig_default);
    UA_StatusCode retval = UA_Client_connect(client, "opc.tcp://localhost:4840");
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    
    client->state = UA_CLIENTSTATE_CONNECTED;

    retval = UA_Client_disconnect(client);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    UA_fakeSleep(UA_ClientConfig_default.secureChannelLifeTime+1);
    UA_realSleep(50 + 1);

    retval = UA_Client_connect(client, "opc.tcp://localhost:4840");
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    UA_Client_delete(client);
}
END_TEST

START_TEST(SecureChannel_cableunplugged) {
    UA_Client *client = UA_Client_new(UA_ClientConfig_default);
    UA_StatusCode retval = UA_Client_connect(client, "opc.tcp://localhost:4840");
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    UA_Variant val;
    UA_Variant_init(&val);
    UA_NodeId nodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER_SERVERSTATUS_STATE);
    retval = UA_Client_readValueAttribute(client, nodeId, &val);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    UA_Variant_deleteMembers(&val);

    UA_Client_recv = client->connection.recv;
    client->connection.recv = UA_Client_recvTesting;

    /* Simulate network cable unplugged (no response from server) */
    UA_Client_recvTesting_result = UA_STATUSCODE_GOODNONCRITICALTIMEOUT;

    UA_Variant_init(&val);
    retval = UA_Client_readValueAttribute(client, nodeId, &val);
    ck_assert_uint_eq(retval, UA_STATUSCODE_BADCONNECTIONCLOSED);

    ck_assert_msg(UA_Client_getState(client) == UA_CLIENTSTATE_DISCONNECTED);

    UA_Client_recvTesting_result = UA_STATUSCODE_GOOD;

    UA_Client_delete(client);
}
END_TEST

int main(void) {
    TCase *tc_sc = tcase_create("Client SecureChannel");
    tcase_add_checked_fixture(tc_sc, setup, teardown);
    tcase_add_test(tc_sc, SecureChannel_timeout_max);
    tcase_add_test(tc_sc, SecureChannel_timeout_fail);
    tcase_add_test(tc_sc, SecureChannel_networkfail);
    tcase_add_test(tc_sc, SecureChannel_reconnect);
    tcase_add_test(tc_sc, SecureChannel_cableunplugged);

    Suite *s = suite_create("Client");
    suite_add_tcase(s, tc_sc);

    SRunner *sr = srunner_create(s);
    srunner_set_fork_status(sr, CK_NOFORK);
    srunner_run_all(sr,CK_NORMAL);
    int number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
