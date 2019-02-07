/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <stdio.h>
#include <stdlib.h>

#include "ua_types.h"
#include "ua_server.h"
#include "ua_server_internal.h"
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
UA_ServerNetworkLayer nl;
THREAD_HANDLE server_thread;

static void
addVariable(size_t size) {
    /* Define the attribute of the myInteger variable node */
    UA_VariableAttributes attr = UA_VariableAttributes_default;
    UA_Int32* array = (UA_Int32*)UA_malloc(size * sizeof(UA_Int32));
    memset(array, 0, size * sizeof(UA_Int32));
    UA_Variant_setArray(&attr.value, array, size, &UA_TYPES[UA_TYPES_INT32]);

    char name[] = "my.variable";
    attr.description = UA_LOCALIZEDTEXT("en-US", name);
    attr.displayName = UA_LOCALIZEDTEXT("en-US", name);
    attr.dataType = UA_TYPES[UA_TYPES_INT32].typeId;

    /* Add the variable node to the information model */
    UA_NodeId myIntegerNodeId = UA_NODEID_STRING(1, name);
    UA_QualifiedName myIntegerName = UA_QUALIFIEDNAME(1, name);
    UA_NodeId parentNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    UA_NodeId parentReferenceNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES);
    UA_Server_addVariableNode(server, myIntegerNodeId, parentNodeId,
                              parentReferenceNodeId, myIntegerName,
                              UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
                              attr, NULL, NULL);

    UA_free(array);
}

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
    addVariable(16366);
    THREAD_CREATE(server_thread, serverloop);
}

static void teardown(void) {
    *running = false;
    THREAD_JOIN(server_thread);
    UA_Server_run_shutdown(server);
    UA_Boolean_delete(running);
    UA_Server_delete(server);
    UA_ServerConfig_delete(config);
}

START_TEST(Client_connect) {
    UA_Client *client = UA_Client_new(UA_ClientConfig_default);
    UA_StatusCode retval = UA_Client_connect(client, "opc.tcp://localhost:4840");

    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    UA_Client_disconnect(client);
    UA_Client_delete(client);
}
END_TEST

START_TEST(Client_connect_username) {
    UA_Client *client = UA_Client_new(UA_ClientConfig_default);
    UA_StatusCode retval = UA_Client_connect_username(client, "opc.tcp://localhost:4840", "user1", "password");

    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    UA_Client_disconnect(client);
    UA_Client_delete(client);
}
END_TEST

START_TEST(Client_endpoints) {
    UA_Client *client = UA_Client_new(UA_ClientConfig_default);

    UA_EndpointDescription* endpointArray = NULL;
    size_t endpointArraySize = 0;
    UA_StatusCode retval = UA_Client_getEndpoints(client, "opc.tcp://localhost:4840",
                                                  &endpointArraySize, &endpointArray);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert_msg(endpointArraySize > 0);

    UA_Array_delete(endpointArray,endpointArraySize, &UA_TYPES[UA_TYPES_ENDPOINTDESCRIPTION]);

    UA_Client_delete(client);
}
END_TEST

START_TEST(Client_endpoints_empty) {
        /* Issue a getEndpoints call with empty endpointUrl.
         * Using UA_Client_getEndpoints automatically passes the client->endpointUrl as requested endpointUrl.
         * The spec says:
         * The Server should return a suitable default URL if it does not recognize the HostName in the URL.
         *
         * See https://github.com/open62541/open62541/issues/775
         */
    UA_Client *client = UA_Client_new(UA_ClientConfig_default);

    UA_StatusCode retval = UA_Client_connect(client, "opc.tcp://localhost:4840");
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    UA_GetEndpointsRequest request;
    UA_GetEndpointsRequest_init(&request);
    request.requestHeader.timestamp = UA_DateTime_now();
    request.requestHeader.timeoutHint = 10000;

    UA_GetEndpointsResponse response;
    __UA_Client_Service(client, &request, &UA_TYPES[UA_TYPES_GETENDPOINTSREQUEST],
                        &response, &UA_TYPES[UA_TYPES_GETENDPOINTSRESPONSE]);

    ck_assert_uint_eq(response.responseHeader.serviceResult, UA_STATUSCODE_GOOD);

    ck_assert_msg(response.endpointsSize > 0);

    UA_GetEndpointsResponse_deleteMembers(&response);
    UA_GetEndpointsRequest_deleteMembers(&request);

    UA_Client_delete(client);
}
END_TEST

START_TEST(Client_read) {
    UA_Client *client = UA_Client_new(UA_ClientConfig_default);
    UA_StatusCode retval = UA_Client_connect(client, "opc.tcp://localhost:4840");
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    UA_Variant val;
    UA_NodeId nodeId = UA_NODEID_STRING(1, "my.variable");
    retval = UA_Client_readValueAttribute(client, nodeId, &val);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    UA_Variant_deleteMembers(&val);

    UA_Client_disconnect(client);
    UA_Client_delete(client);
}
END_TEST

START_TEST(Client_renewSecureChannel) {
    UA_Client *client = UA_Client_new(UA_ClientConfig_default);
    UA_StatusCode retval = UA_Client_connect(client, "opc.tcp://localhost:4840");
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    /* Forward the time */
    UA_fakeSleep((UA_UInt32)((UA_Double)UA_ClientConfig_default.secureChannelLifeTime * 0.8));

    /* Now read */
    UA_Variant val;
    UA_NodeId nodeId = UA_NODEID_STRING(1, "my.variable");
    retval = UA_Client_readValueAttribute(client, nodeId, &val);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    UA_Variant_deleteMembers(&val);

    UA_Client_disconnect(client);
    UA_Client_delete(client);

} END_TEST

START_TEST(Client_reconnect) {
    UA_ClientConfig clientConfig = UA_ClientConfig_default;
    UA_Client *client = UA_Client_new(clientConfig);
    UA_StatusCode retval = UA_Client_connect(client, "opc.tcp://localhost:4840");
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    UA_Variant val;
    UA_NodeId nodeId = UA_NODEID_STRING(1, "my.variable");
    retval = UA_Client_readValueAttribute(client, nodeId, &val);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    UA_Variant_deleteMembers(&val);

    // restart server to test reconnect
    teardown();
    setup();

    retval = UA_Client_readValueAttribute(client, nodeId, &val);
    ck_assert_uint_eq(retval, UA_STATUSCODE_BADCONNECTIONCLOSED);

    retval = UA_Client_connect(client, "opc.tcp://localhost:4840");
    ck_assert_msg(retval == UA_STATUSCODE_GOOD, UA_StatusCode_name(retval));

    retval = UA_Client_readValueAttribute(client, nodeId, &val);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    UA_Variant_deleteMembers(&val);

    UA_Client_disconnect(client);
    UA_Client_delete(client);
}
END_TEST

START_TEST(Client_delete_without_connect) {
    UA_ClientConfig clientConfig = UA_ClientConfig_default;
    UA_Client *client = UA_Client_new(clientConfig);
    ck_assert_msg(client != NULL);
    UA_Client_delete(client);
}
END_TEST

// TODO ACTIVATE THE TESTS WHEN SESSION RECOVERY IS GOOD
#ifdef UA_SESSION_RECOVERY

START_TEST(Client_activateSessionClose) {
    // restart server
    teardown();
    setup();
    ck_assert_uint_eq(server->sessionManager.currentSessionCount, 0);

    UA_ClientConfig clientConfig = UA_ClientConfig_default;
    UA_Client *client = UA_Client_new(clientConfig);
    UA_StatusCode retval = UA_Client_connect(client, "opc.tcp://localhost:4840");
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert_uint_eq(server->sessionManager.currentSessionCount, 1);

    UA_Variant val;
    UA_NodeId nodeId = UA_NODEID_STRING(1, "my.variable");
    retval = UA_Client_readValueAttribute(client, nodeId, &val);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    UA_Variant_deleteMembers(&val);

    UA_Client_close(client);

    retval = UA_Client_connect(client, "opc.tcp://localhost:4840");
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert_uint_eq(server->sessionManager.currentSessionCount, 1);

    nodeId = UA_NODEID_STRING(1, "my.variable");
    retval = UA_Client_readValueAttribute(client, nodeId, &val);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    UA_Variant_deleteMembers(&val);

    UA_Client_disconnect(client);
    UA_Client_delete(client);
    ck_assert_uint_eq(server->sessionManager.currentSessionCount, 0);
}
END_TEST

START_TEST(Client_activateSessionTimeout) {
    // restart server
    teardown();
    setup();
    ck_assert_uint_eq(server->sessionManager.currentSessionCount, 0);

    UA_Client *client = UA_Client_new(UA_ClientConfig_default);
    UA_StatusCode retval = UA_Client_connect(client, "opc.tcp://localhost:4840");
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    ck_assert_uint_eq(server->sessionManager.currentSessionCount, 1);

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
    retval = UA_Client_connect(client, "opc.tcp://localhost:4840");
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    ck_assert_uint_eq(server->sessionManager.currentSessionCount, 1);

    retval = UA_Client_readValueAttribute(client, nodeId, &val);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    UA_Variant_deleteMembers(&val);

    UA_Client_delete(client);

    ck_assert_uint_eq(server->sessionManager.currentSessionCount, 0);
}
END_TEST

#endif /* UA_SESSION_RECOVERY */

static Suite* testSuite_Client(void) {
    Suite *s = suite_create("Client");
    TCase *tc_client = tcase_create("Client Basic");
    tcase_add_checked_fixture(tc_client, setup, teardown);
    tcase_add_test(tc_client, Client_connect);
    tcase_add_test(tc_client, Client_connect_username);
    tcase_add_test(tc_client, Client_delete_without_connect);
    tcase_add_test(tc_client, Client_endpoints);
    tcase_add_test(tc_client, Client_endpoints_empty);
    tcase_add_test(tc_client, Client_read);
    suite_add_tcase(s,tc_client);
    TCase *tc_client_reconnect = tcase_create("Client Reconnect");
    tcase_add_checked_fixture(tc_client_reconnect, setup, teardown);
    tcase_add_test(tc_client_reconnect, Client_renewSecureChannel);
    tcase_add_test(tc_client_reconnect, Client_reconnect);
#ifdef UA_SESSION_RECOVERY
    tcase_add_test(tc_client_reconnect, Client_activateSessionClose);
    tcase_add_test(tc_client_reconnect, Client_activateSessionTimeout);
#endif /* UA_SESSION_RECOVERY */
    suite_add_tcase(s,tc_client_reconnect);
    return s;
}

int main(void) {
    Suite *s = testSuite_Client();
    SRunner *sr = srunner_create(s);
    srunner_set_fork_status(sr, CK_NOFORK);
    srunner_run_all(sr,CK_NORMAL);
    int number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
