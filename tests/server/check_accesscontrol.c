/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <stdlib.h>

#include "ua_types.h"
#include "ua_server.h"
#include "ua_client.h"
#include "ua_config_default.h"
#include "check.h"

#include "thread_wrapper.h"

UA_Server *server;
UA_ServerConfig *config;
UA_Boolean *running;
UA_ServerNetworkLayer nl;
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
}

static void teardown(void) {
    *running = false;
    THREAD_JOIN(server_thread);
    UA_Server_run_shutdown(server);
    UA_Boolean_delete(running);
    UA_Server_delete(server);
    UA_ServerConfig_delete(config);
}

START_TEST(Client_anonymous) {
        UA_Client *client = UA_Client_new(UA_ClientConfig_default);
        UA_StatusCode retval = UA_Client_connect(client, "opc.tcp://localhost:4840");

        ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

        UA_Client_disconnect(client);
        UA_Client_delete(client);
    }
END_TEST

START_TEST(Client_user_pass_ok) {
        UA_Client *client = UA_Client_new(UA_ClientConfig_default);
        UA_StatusCode retval = UA_Client_connect_username(client, "opc.tcp://localhost:4840", "user1", "password");

        ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

        UA_Client_disconnect(client);
        UA_Client_delete(client);
    }
END_TEST

START_TEST(Client_user_fail) {
        UA_Client *client = UA_Client_new(UA_ClientConfig_default);
        UA_StatusCode retval = UA_Client_connect_username(client, "opc.tcp://localhost:4840", "user0", "password");

        ck_assert_uint_eq(retval, UA_STATUSCODE_BADUSERACCESSDENIED);

        UA_Client_disconnect(client);
        UA_Client_delete(client);
    }
END_TEST

START_TEST(Client_pass_fail) {
        UA_Client *client = UA_Client_new(UA_ClientConfig_default);
        UA_StatusCode retval = UA_Client_connect_username(client, "opc.tcp://localhost:4840", "user1", "secret");

        ck_assert_uint_eq(retval, UA_STATUSCODE_BADUSERACCESSDENIED);

        UA_Client_disconnect(client);
        UA_Client_delete(client);
    }
END_TEST


static Suite* testSuite_Client(void) {
    Suite *s = suite_create("Client");
    TCase *tc_client_user = tcase_create("Client User/Password");
    tcase_add_checked_fixture(tc_client_user, setup, teardown);
    tcase_add_test(tc_client_user, Client_anonymous);
    tcase_add_test(tc_client_user, Client_user_pass_ok);
    tcase_add_test(tc_client_user, Client_user_fail);
    tcase_add_test(tc_client_user, Client_pass_fail);
    suite_add_tcase(s,tc_client_user);
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
