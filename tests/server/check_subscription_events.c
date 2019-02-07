/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ua_client_subscriptions.h"
#include "ua_server.h"
#include "server/ua_services.h"
#include "server/ua_server_internal.h"
#include "server/ua_subscription.h"
#include "ua_config_default.h"
#include "thread_wrapper.h"

#include "check.h"
#include "testing_clock.h"

#ifdef UA_ENABLE_SUBSCRIPTIONS_EVENTS

static UA_Server *server;
static UA_ServerConfig *config;
static UA_Boolean *running;
static THREAD_HANDLE server_thread;

UA_Client *client;

static UA_UInt32 subscriptionId;
static UA_UInt32 monitoredItemId;
static UA_NodeId eventType;
static size_t nSelectClauses = 4;
static UA_Boolean notificationReceived;
static UA_SimpleAttributeOperand *selectClauses;

UA_Double publishingInterval = 500.0;


static void addNewEventType(void) {
    UA_ObjectTypeAttributes attr = UA_ObjectTypeAttributes_default;
    attr.displayName = UA_LOCALIZEDTEXT_ALLOC("en-US", "SimpleEventType");
    attr.description = UA_LOCALIZEDTEXT_ALLOC("en-US", "The simple event type we created");

    UA_Server_addObjectTypeNode(server, UA_NODEID_NULL,
                                UA_NODEID_NUMERIC(0, UA_NS0ID_BASEEVENTTYPE),
                                UA_NODEID_NUMERIC(0, UA_NS0ID_HASSUBTYPE),
                                UA_QUALIFIEDNAME(0, "SimpleEventType"),
                                attr, NULL, &eventType);
    UA_LocalizedText_deleteMembers(&attr.displayName);
    UA_LocalizedText_deleteMembers(&attr.description);
}

static void setupSelectClauses(void) {
    // check for severity (set manually), message (set manually), eventType (automatic) and sourceNode (automatic)
    selectClauses = (UA_SimpleAttributeOperand *)
                     UA_Array_new(nSelectClauses, &UA_TYPES[UA_TYPES_SIMPLEATTRIBUTEOPERAND]);
    if (!selectClauses)
        return;

    for (size_t i = 0; i < nSelectClauses; ++i) {
        UA_SimpleAttributeOperand_init(&selectClauses[i]);
        selectClauses[i].typeDefinitionId = UA_NODEID_NUMERIC(0, UA_NS0ID_BASEEVENTTYPE);
        selectClauses[i].browsePathSize = 1;
        selectClauses[i].attributeId = UA_ATTRIBUTEID_VALUE;
        selectClauses[i].browsePath = (UA_QualifiedName *)
                       UA_Array_new(selectClauses[i].browsePathSize, &UA_TYPES[UA_TYPES_QUALIFIEDNAME]);
        if (!selectClauses[i].browsePathSize) {
            UA_Array_delete(selectClauses, nSelectClauses, &UA_TYPES[UA_TYPES_SIMPLEATTRIBUTEOPERAND]);
        }
    }

    selectClauses[0].browsePath[0] = UA_QUALIFIEDNAME_ALLOC(0, "Severity");
    selectClauses[1].browsePath[0] = UA_QUALIFIEDNAME_ALLOC(0, "Message");
    selectClauses[2].browsePath[0] = UA_QUALIFIEDNAME_ALLOC(0, "EventType");
    selectClauses[3].browsePath[0] = UA_QUALIFIEDNAME_ALLOC(0, "SourceNode");
}

static void
handler_events_simple(UA_Client *lclient, UA_UInt32 subId, void *subContext,
                      UA_UInt32 monId, void *monContext,
                      size_t nEventFields, UA_Variant *eventFields) {
    UA_Boolean foundSeverity = UA_FALSE;
    UA_Boolean foundMessage = UA_FALSE;
    UA_Boolean foundType = UA_FALSE;
    UA_Boolean foundSource = UA_FALSE;
    ck_assert_uint_eq(*(UA_UInt32 *) monContext, monitoredItemId);
    ck_assert_uint_eq(nEventFields, nSelectClauses);
    // check all event fields
    for (unsigned int i = 0; i < nEventFields; i++) {
        // find out which attribute of the event is being looked at
        if (UA_Variant_hasScalarType(&eventFields[i], &UA_TYPES[UA_TYPES_UINT16])) {
            // Severity
            ck_assert_uint_eq(*((UA_UInt16 *) (eventFields[i].data)), 1000);
            foundSeverity = UA_TRUE;
        } else if (UA_Variant_hasScalarType(&eventFields[i], &UA_TYPES[UA_TYPES_LOCALIZEDTEXT])) {
            // Message
            UA_LocalizedText comp = UA_LOCALIZEDTEXT("en-US", "Generated Event");
            ck_assert(UA_String_equal(&((UA_LocalizedText *) eventFields[i].data)->locale, &comp.locale));
            ck_assert(UA_String_equal(&((UA_LocalizedText *) eventFields[i].data)->text, &comp.text));
            foundMessage = UA_TRUE;
        } else if (UA_Variant_hasScalarType(&eventFields[i], &UA_TYPES[UA_TYPES_NODEID])) {
            // either SourceNode or EventType
            UA_NodeId serverId = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER);
            if (UA_NodeId_equal((UA_NodeId *)eventFields[i].data, &eventType)) {
                // EventType
                foundType = UA_TRUE;
            } else if (UA_NodeId_equal((UA_NodeId *)eventFields[i].data, &serverId)) {
                // SourceNode
                foundSource = UA_TRUE;
            } else {
                ck_assert_msg(UA_FALSE, "NodeId doesn't match");
            }
        } else {
            ck_assert_msg(UA_FALSE, "Field doesn't match");
        }
    }
    ck_assert_uint_eq(foundMessage, UA_TRUE);
    ck_assert_uint_eq(foundSeverity, UA_TRUE);
    ck_assert_uint_eq(foundType, UA_TRUE);
    ck_assert_uint_eq(foundSource, UA_TRUE);
    notificationReceived = true;
}

// create a subscription and add a monitored item to it
static void setupSubscription(void) {
    // Create subscription
    UA_CreateSubscriptionRequest request = UA_CreateSubscriptionRequest_default();
    UA_CreateSubscriptionResponse response = UA_Client_Subscriptions_create(client, request,
                                                                            NULL, NULL, NULL);
    subscriptionId = response.subscriptionId;

}

static void removeSubscription(void) {
    UA_DeleteSubscriptionsRequest deleteSubscriptionsRequest;
    UA_DeleteSubscriptionsRequest_init(&deleteSubscriptionsRequest);
    UA_UInt32 removeId = subscriptionId;
    deleteSubscriptionsRequest.subscriptionIdsSize = 1;
    deleteSubscriptionsRequest.subscriptionIds = &removeId;

    UA_DeleteSubscriptionsResponse deleteSubscriptionsResponse;
    UA_DeleteSubscriptionsResponse_init(&deleteSubscriptionsResponse);

    Service_DeleteSubscriptions(server, &adminSession, &deleteSubscriptionsRequest,
                                &deleteSubscriptionsResponse);
    UA_DeleteSubscriptionsResponse_deleteMembers(&deleteSubscriptionsResponse);
}

THREAD_CALLBACK(serverloop) {
    while (*running)
        UA_Server_run_iterate(server, true);
    return 0;
}

static void setup(void) {
    running = UA_Boolean_new();
    *running = true;
    config = UA_ServerConfig_new_default();
    config->maxPublishReqPerSession = 5;
    server = UA_Server_new(config);
    UA_Server_run_startup(server);
    addNewEventType();
    setupSelectClauses();
    THREAD_CREATE(server_thread, serverloop);

    client = UA_Client_new(UA_ClientConfig_default);
    UA_StatusCode retval = UA_Client_connect(client, "opc.tcp://localhost:4840");
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    setupSubscription();
}

static void teardown(void) {
    removeSubscription();
    *running = false;
    THREAD_JOIN(server_thread);
    UA_Server_run_shutdown(server);
    UA_Boolean_delete(running);
    UA_Server_delete(server);
    UA_ServerConfig_delete(config);
    UA_Array_delete(selectClauses, nSelectClauses, &UA_TYPES[UA_TYPES_SIMPLEATTRIBUTEOPERAND]);

    UA_Client_disconnect(client);
    UA_Client_delete(client);
}

static UA_StatusCode eventSetup(UA_NodeId *eventNodeId) {
    UA_StatusCode retval;
    retval = UA_Server_createEvent(server, eventType, eventNodeId);
    ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    // add a severity to the event
    UA_Variant value;
    UA_RelativePathElement rpe;
    UA_RelativePathElement_init(&rpe);
    rpe.referenceTypeId = UA_NODEID_NUMERIC(0, UA_NS0ID_HASPROPERTY);
    rpe.isInverse = false;
    rpe.includeSubtypes = false;
    UA_BrowsePath bp;
    UA_BrowsePath_init(&bp);
    bp.startingNode = *eventNodeId;
    bp.relativePath.elementsSize = 1;
    bp.relativePath.elements = &rpe;
    rpe.targetName = UA_QUALIFIEDNAME(0, "Severity");
    UA_BrowsePathResult bpr = UA_Server_translateBrowsePathToNodeIds(server, &bp);
    // number with no special meaning
    UA_UInt16 eventSeverity = 1000;
    UA_Variant_setScalar(&value, &eventSeverity, &UA_TYPES[UA_TYPES_UINT16]);
    UA_Server_writeValue(server, bpr.targets[0].targetId.nodeId, value);
    UA_BrowsePathResult_deleteMembers(&bpr);

    //add a message to the event
    rpe.targetName = UA_QUALIFIEDNAME(0, "Message");
    bpr = UA_Server_translateBrowsePathToNodeIds(server, &bp);
    UA_LocalizedText message = UA_LOCALIZEDTEXT("en-US", "Generated Event");
    UA_Variant_setScalar(&value, &message, &UA_TYPES[UA_TYPES_LOCALIZEDTEXT]);
    UA_Server_writeValue(server, bpr.targets[0].targetId.nodeId, value);
    UA_BrowsePathResult_deleteMembers(&bpr);

    return retval;
}

static UA_MonitoredItemCreateResult addMonitoredItem(UA_Client_EventNotificationCallback handler) {
    UA_MonitoredItemCreateRequest item;
    UA_MonitoredItemCreateRequest_init(&item);
    item.itemToMonitor.nodeId = UA_NODEID_NUMERIC(0, 2253); // Root->Objects->Server
    item.itemToMonitor.attributeId = UA_ATTRIBUTEID_EVENTNOTIFIER;
    item.monitoringMode = UA_MONITORINGMODE_REPORTING;

    UA_EventFilter filter;
    UA_EventFilter_init(&filter);
    filter.selectClauses = selectClauses;
    filter.selectClausesSize = nSelectClauses;

    item.requestedParameters.filter.encoding = UA_EXTENSIONOBJECT_DECODED;
    item.requestedParameters.filter.content.decoded.data = &filter;
    item.requestedParameters.filter.content.decoded.type = &UA_TYPES[UA_TYPES_EVENTFILTER];
    item.requestedParameters.queueSize = 1;
    item.requestedParameters.discardOldest = true;

    return UA_Client_MonitoredItems_createEvent(client, subscriptionId,
                                                UA_TIMESTAMPSTORETURN_BOTH, item,
                                                &monitoredItemId, handler, NULL);
}

// ensure events are received with proper values
START_TEST(generateEvents)
    {
        UA_NodeId eventNodeId;
        UA_StatusCode retval = eventSetup(&eventNodeId);
        ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

        // add a monitored item
        UA_MonitoredItemCreateResult createResult = addMonitoredItem(handler_events_simple);
        ck_assert_uint_eq(createResult.statusCode, UA_STATUSCODE_GOOD);
        // trigger the event
        retval = UA_Server_triggerEvent(server, eventNodeId, UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER), NULL, UA_TRUE);
        ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

        // let the client fetch the event and check if the correct values were received
        notificationReceived = false;
        UA_fakeSleep((UA_UInt32) publishingInterval + 100);
        retval = UA_Client_run_iterate(client, 0);
        ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
        ck_assert_uint_eq(notificationReceived, true);
        ck_assert_uint_eq(createResult.revisedQueueSize, 1);

        // delete the monitoredItem
        UA_DeleteMonitoredItemsRequest deleteRequest;
        UA_DeleteMonitoredItemsRequest_init(&deleteRequest);
        deleteRequest.subscriptionId = subscriptionId;
        deleteRequest.monitoredItemIds = &monitoredItemId;
        deleteRequest.monitoredItemIdsSize = 1;

        UA_DeleteMonitoredItemsResponse deleteResponse =
                UA_Client_MonitoredItems_delete(client, deleteRequest);

        ck_assert_uint_eq(deleteResponse.responseHeader.serviceResult, UA_STATUSCODE_GOOD);
        ck_assert_uint_eq(deleteResponse.resultsSize, 1);

        UA_DeleteMonitoredItemsResponse_deleteMembers(&deleteResponse);
    }
END_TEST

static void
handler_events_propagate(UA_Client *lclient, UA_UInt32 subId, void *subContext,
                         UA_UInt32 monId, void *monContext,
                         size_t nEventFields, UA_Variant *eventFields) {
    UA_Boolean foundSeverity = UA_FALSE;
    UA_Boolean foundMessage = UA_FALSE;
    UA_Boolean foundType = UA_FALSE;
    UA_Boolean foundSource = UA_FALSE;
    ck_assert_uint_eq(*(UA_UInt32 *) monContext, monitoredItemId);
    ck_assert_uint_eq(nEventFields, nSelectClauses);
    // check all event fields
    for (unsigned int i = 0; i < nEventFields; i++) {
        // find out which attribute of the event is being looked at
        if (UA_Variant_hasScalarType(&eventFields[i], &UA_TYPES[UA_TYPES_UINT16])) {
            // Severity
            ck_assert_uint_eq(*((UA_UInt16 *) (eventFields[i].data)), 1000);
            foundSeverity = UA_TRUE;
        } else if (UA_Variant_hasScalarType(&eventFields[i], &UA_TYPES[UA_TYPES_LOCALIZEDTEXT])) {
            // Message
            UA_LocalizedText comp = UA_LOCALIZEDTEXT("en-US", "Generated Event");
            ck_assert(UA_String_equal(&((UA_LocalizedText *) eventFields[i].data)->locale, &comp.locale));
            ck_assert(UA_String_equal(&((UA_LocalizedText *) eventFields[i].data)->text, &comp.text));
            foundMessage = UA_TRUE;
        } else if (UA_Variant_hasScalarType(&eventFields[i], &UA_TYPES[UA_TYPES_NODEID])) {
            // either SourceNode or EventType
            UA_NodeId serverNameSpaceId = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER_NAMESPACES);
            if (UA_NodeId_equal((UA_NodeId *)eventFields[i].data, &eventType)) {
                // EventType
                foundType = UA_TRUE;
            } else if (UA_NodeId_equal((UA_NodeId *)eventFields[i].data, &serverNameSpaceId)) {
                // SourceNode
                foundSource = UA_TRUE;
            } else {
                ck_assert_msg(UA_FALSE, "NodeId doesn't match");
            }
        } else {
            ck_assert_msg(UA_FALSE, "Field doesn't match");
        }
    }
    ck_assert_uint_eq(foundMessage, UA_TRUE);
    ck_assert_uint_eq(foundSeverity, UA_TRUE);
    ck_assert_uint_eq(foundType, UA_TRUE);
    ck_assert_uint_eq(foundSource, UA_TRUE);
    notificationReceived = true;
}

START_TEST(uppropagation) {
        // trigger first event
        UA_NodeId eventNodeId;
        UA_StatusCode retval = eventSetup(&eventNodeId);
        ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

        //add a monitored item
        UA_MonitoredItemCreateResult createResult = addMonitoredItem(handler_events_propagate);
        ck_assert_uint_eq(createResult.statusCode, UA_STATUSCODE_GOOD);
        // trigger the event on a child of server, using namespaces in this case (no reason in particular)
        retval = UA_Server_triggerEvent(server, eventNodeId, UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER_NAMESPACES), NULL,
                                        UA_TRUE);
        ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

        // let the client fetch the event and check if the correct values were received
        notificationReceived = false;
        UA_fakeSleep((UA_UInt32) publishingInterval + 100);
        retval = UA_Client_run_iterate(client, 0);
        ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
        ck_assert_uint_eq(notificationReceived, true);
        ck_assert_uint_eq(createResult.revisedQueueSize, 1);

        // delete the monitoredItem
        UA_DeleteMonitoredItemsRequest deleteRequest;
        UA_DeleteMonitoredItemsRequest_init(&deleteRequest);
        deleteRequest.subscriptionId = subscriptionId;
        deleteRequest.monitoredItemIds = &monitoredItemId;
        deleteRequest.monitoredItemIdsSize = 1;

        UA_DeleteMonitoredItemsResponse deleteResponse =
                UA_Client_MonitoredItems_delete(client, deleteRequest);

        ck_assert_uint_eq(deleteResponse.responseHeader.serviceResult, UA_STATUSCODE_GOOD);
        ck_assert_uint_eq(deleteResponse.resultsSize, 1);

        UA_DeleteMonitoredItemsResponse_deleteMembers(&deleteResponse);
}
END_TEST

/*
static void
handler_events_overflow(UA_Client *lclient, UA_UInt32 subId, void *subContext,
                        UA_UInt32 monId, void *monContext,
                        size_t nEventFields, UA_Variant *eventFields) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND, "Event overflow was found");
    ck_assert_uint_eq(*(UA_UInt32 *) monContext, monitoredItemId);
    ck_assert_uint_eq(nEventFields, 1);
    ck_assert(eventFields->type == &UA_TYPES[UA_TYPES_NODEID]);
    UA_NodeId comp = UA_NODEID_NUMERIC(0, UA_NS0ID_SIMPLEOVERFLOWEVENTTYPE);
    ck_assert((UA_NodeId_equal((UA_NodeId *)eventFields->data, &comp)));
    notificationReceived = true;
}

// ensures an eventQueueOverflowEvent is published when appropriate
START_TEST(eventOverflow)
    {
        // add a monitored item
        UA_MonitoredItemCreateResult createResult = addMonitoredItem(handler_events_overflow);
        ck_assert_uint_eq(createResult.statusCode, UA_STATUSCODE_GOOD);

        // trigger first event
        UA_NodeId eventNodeId;
        UA_StatusCode retval = UA_STATUSCODE_GOOD;
        for (int i = 0; i < 3; i++) {
            retval = eventSetup(&eventNodeId);
            ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
            retval = UA_Server_triggerEvent(server, eventNodeId, UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER), NULL, UA_TRUE);
            ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
        }


        // fetch the events
        notificationReceived = false;
        UA_fakeSleep((UA_UInt32) publishingInterval + 100);
        retval = UA_Client_run_iterate(client, 0);
        ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
        ck_assert_uint_eq(notificationReceived, true);
        ck_assert_uint_eq(createResult.revisedQueueSize, 1);

        // delete the monitoredItem
        UA_DeleteMonitoredItemsRequest deleteRequest;
        UA_DeleteMonitoredItemsRequest_init(&deleteRequest);
        deleteRequest.subscriptionId = subscriptionId;
        deleteRequest.monitoredItemIds = &monitoredItemId;
        deleteRequest.monitoredItemIdsSize = 1;

        UA_DeleteMonitoredItemsResponse deleteResponse =
                UA_Client_MonitoredItems_delete(client, deleteRequest);

        ck_assert_uint_eq(deleteResponse.responseHeader.serviceResult, UA_STATUSCODE_GOOD);
        ck_assert_uint_eq(deleteResponse.resultsSize, 1);

        UA_DeleteMonitoredItemsResponse_deleteMembers(&deleteResponse);
    }
END_TEST
*/

#endif // UA_ENABLE_SUBSCRIPTIONS_EVENTS

// assumes subscriptions work fine with data change because of other unit test
static Suite *testSuite_Client(void) {
    Suite *s = suite_create("Server Subscription Events");
    TCase *tc_server = tcase_create("Server Subscription Events");
#ifdef UA_ENABLE_SUBSCRIPTIONS_EVENTS
    tcase_add_checked_fixture(tc_server, setup, teardown);
    tcase_add_test(tc_server, generateEvents);
    tcase_add_test(tc_server, uppropagation);
//    tcase_add_test(tc_server, eventOverflow);
#endif // UA_ENABLE_SUBSCRIPTIONS_EVENTS
    suite_add_tcase(s, tc_server);

    return s;
}

int main(void) {
    Suite *s = testSuite_Client();
    SRunner *sr = srunner_create(s);
    srunner_set_fork_status(sr, CK_NOFORK);
    srunner_run_all(sr, CK_NORMAL);
    int number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

