/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ua_server.h"
#include "server/ua_server_internal.h"
#include "ua_config_default.h"

#include "check.h"

UA_Server *server = NULL;
UA_ServerConfig *config = NULL;

UA_UInt32 valueToBeInherited = 42;

static void setup(void) {
    config = UA_ServerConfig_new_default();
    server = UA_Server_new(config);
    UA_Server_run_startup(server);
}

static void teardown(void) {
    UA_Server_run_shutdown(server);
    UA_Server_delete(server);
    UA_ServerConfig_delete(config);
}


/* finds the NodeId of a StateNumber child of a given node id */
static void findChildId(UA_NodeId stateId, UA_NodeId referenceType, const UA_QualifiedName targetName, UA_NodeId *result) {
    UA_RelativePathElement rpe;
    UA_RelativePathElement_init(&rpe);
    rpe.referenceTypeId = referenceType;
    rpe.isInverse = false;
    rpe.includeSubtypes = false;
    rpe.targetName = targetName;

    UA_BrowsePath bp;
    UA_BrowsePath_init(&bp);
    bp.startingNode = stateId;
    bp.relativePath.elementsSize = 1;
    bp.relativePath.elements = &rpe;    //clion complains but is ok

    UA_BrowsePathResult bpr = UA_Server_translateBrowsePathToNodeIds(server, &bp);
    ck_assert_uint_eq(bpr.statusCode, UA_STATUSCODE_GOOD);


    UA_NodeId_copy(&bpr.targets[0].targetId.nodeId, result);
    UA_BrowsePathResult_deleteMembers(&bpr);
}


START_TEST(Nodes_createCustomStateType)
    {
        // Create a type "CustomStateType" with a variable "CustomStateNumber" as property
        UA_StatusCode retval = UA_STATUSCODE_GOOD;

        UA_ObjectTypeAttributes attrObject = UA_ObjectTypeAttributes_default;
        attrObject.displayName = UA_LOCALIZEDTEXT("", "CustomStateType");
        attrObject.description = UA_LOCALIZEDTEXT("", "");
        attrObject.writeMask = 0;
        attrObject.userWriteMask = 0;
        retval = UA_Server_addObjectTypeNode(server,
                                             UA_NODEID_NUMERIC(1, 6000),
                                             UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE),
                                             UA_NODEID_NUMERIC(0, UA_NS0ID_HASSUBTYPE),
                                             UA_QUALIFIEDNAME(1, "CustomStateType"),
                                             attrObject,
                                             NULL, NULL);
        ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

        // Now add a property "StateNumber"
        UA_VariableAttributes attr = UA_VariableAttributes_default;
        attr.minimumSamplingInterval = 0.000000;
        attr.userAccessLevel = 1;
        attr.accessLevel = 1;
        attr.valueRank = -2;
        attr.dataType = UA_NODEID_NUMERIC(0, UA_NS0ID_UINT32);
        UA_UInt32 val = 0;
        UA_Variant_setScalar(&attr.value, &val, &UA_TYPES[UA_TYPES_UINT32]);
        attr.displayName = UA_LOCALIZEDTEXT("", "CustomStateNumber");
        attr.description = UA_LOCALIZEDTEXT("", "");
        attr.writeMask = 0;
        attr.userWriteMask = 0;
        retval = UA_Server_addVariableNode(server,
                                           UA_NODEID_NUMERIC(1, 6001),
                                           UA_NODEID_NUMERIC(1, 6000),
                                           UA_NODEID_NUMERIC(0, UA_NS0ID_HASPROPERTY),
                                           UA_QUALIFIEDNAME(1, "CustomStateNumber"),
                                           UA_NODEID_NUMERIC(0, UA_NS0ID_PROPERTYTYPE),
                                           attr,
                                           NULL, NULL);
        ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

        retval = UA_Server_addReference(server, UA_NODEID_NUMERIC(1, 6001), UA_NODEID_NUMERIC(0, UA_NS0ID_HASMODELLINGRULE),
                                        UA_EXPANDEDNODEID_NUMERIC(0, UA_NS0ID_MODELLINGRULE_MANDATORY), true);
        ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    }
END_TEST


START_TEST(Nodes_createCustomObjectType)
    {
        // Create a custom object type "CustomDemoType" which has a "CustomStateType" component

        UA_StatusCode retval = UA_STATUSCODE_GOOD;

        /* create new object type node which has a subcomponent of the type StateType */
        UA_ObjectTypeAttributes otAttr = UA_ObjectTypeAttributes_default;
        otAttr.displayName = UA_LOCALIZEDTEXT("", "CustomDemoType");
        otAttr.description = UA_LOCALIZEDTEXT("", "");
        retval = UA_Server_addObjectTypeNode(server, UA_NODEID_NUMERIC(1, 6010),
                                             UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE),
                                             UA_NODEID_NUMERIC(0, UA_NS0ID_HASSUBTYPE),
                                             UA_QUALIFIEDNAME(1, "CustomDemoType"),
                                             otAttr, NULL, NULL);
        ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);


        UA_ObjectAttributes oAttr = UA_ObjectAttributes_default;
        oAttr.displayName = UA_LOCALIZEDTEXT("", "State");
        oAttr.description = UA_LOCALIZEDTEXT("", "");
        retval = UA_Server_addObjectNode(server, UA_NODEID_NUMERIC(1, 6011), UA_NODEID_NUMERIC(1, 6010),
                                         UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
                                         UA_QUALIFIEDNAME(1, "State"),
                                         UA_NODEID_NUMERIC(1, 6000),
                                         oAttr, NULL, NULL);
        ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);


        /* modelling rule is mandatory so it will be inherited for the object created from CustomDemoType */
        retval = UA_Server_addReference(server, UA_NODEID_NUMERIC(1, 6011), UA_NODEID_NUMERIC(0, UA_NS0ID_HASMODELLINGRULE),
                                        UA_EXPANDEDNODEID_NUMERIC(0, UA_NS0ID_MODELLINGRULE_MANDATORY), true);
        ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);




        /* assign a default value to the attribute "StateNumber" inside the state attribute (part of the MyDemoType) */
        UA_Variant stateNum;
        UA_Variant_init(&stateNum);
        UA_Variant_setScalar(&stateNum, &valueToBeInherited, &UA_TYPES[UA_TYPES_UINT32]);
        UA_NodeId childID;
        findChildId(UA_NODEID_NUMERIC(1, 6011), UA_NODEID_NUMERIC(0, UA_NS0ID_HASPROPERTY),
                    UA_QUALIFIEDNAME(1, "CustomStateNumber"), &childID);
        ck_assert(!UA_NodeId_isNull(&childID));

        retval = UA_Server_writeValue(server, childID, stateNum);
        ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);

    }
END_TEST

START_TEST(Nodes_createInheritedObject)
    {
        /* create an object/instance of the demo type */
        UA_ObjectAttributes oAttr2 = UA_ObjectAttributes_default;
        oAttr2.displayName = UA_LOCALIZEDTEXT("", "Demo");
        oAttr2.description = UA_LOCALIZEDTEXT("", "");
        UA_StatusCode retval = UA_Server_addObjectNode(server, UA_NODEID_NUMERIC(1, 6020),
                                                       UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER),
                                                       UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
                                                       UA_QUALIFIEDNAME(1, "Demo"), UA_NODEID_NUMERIC(1, 6010),
                                                       oAttr2, NULL, NULL);
        ck_assert_uint_eq(retval, UA_STATUSCODE_GOOD);
    }
END_TEST

START_TEST(Nodes_checkInheritedValue)
    {
        UA_NodeId childState;
        findChildId(UA_NODEID_NUMERIC(1, 6020), UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
                    UA_QUALIFIEDNAME(1, "State"), &childState);
        ck_assert(!UA_NodeId_isNull(&childState));
        UA_NodeId childNumber;
        findChildId(childState, UA_NODEID_NUMERIC(0, UA_NS0ID_HASPROPERTY),
                    UA_QUALIFIEDNAME(1, "CustomStateNumber"), &childNumber);
        ck_assert(!UA_NodeId_isNull(&childNumber));

        UA_Variant inheritedValue;
        UA_Variant_init(&inheritedValue);
        UA_Server_readValue(server, childNumber, &inheritedValue);
        ck_assert(inheritedValue.type == &UA_TYPES[UA_TYPES_UINT32]);

        UA_UInt32 *value = (UA_UInt32 *) inheritedValue.data;

        ck_assert_int_eq(*value, valueToBeInherited);
        UA_Variant_deleteMembers(&inheritedValue);
    }
END_TEST


static Suite *testSuite_Client(void) {
    Suite *s = suite_create("Node inheritance");
    TCase *tc_inherit_subtype = tcase_create("Inherit subtype value");
    tcase_add_unchecked_fixture(tc_inherit_subtype, setup, teardown);
    tcase_add_test(tc_inherit_subtype, Nodes_createCustomStateType);
    tcase_add_test(tc_inherit_subtype, Nodes_createCustomObjectType);
    tcase_add_test(tc_inherit_subtype, Nodes_createInheritedObject);
    tcase_add_test(tc_inherit_subtype, Nodes_checkInheritedValue);
    suite_add_tcase(s, tc_inherit_subtype);
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
