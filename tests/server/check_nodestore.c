/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "ua_types.h"
#include "ua_plugin_nodestore.h"
#include "ua_nodestore_default.h"
#include "ua_util.h"
#include "check.h"

/* container_of */
#define container_of(ptr, type, member) \
    (type *)((uintptr_t)ptr - offsetof(type,member))

#ifdef UA_ENABLE_MULTITHREADING
#include <pthread.h>
#endif

/* Dirty redifinition from ua_nodestore_default.c to check that all nodes were
 * released */
typedef struct UA_NodeMapEntry {
    struct UA_NodeMapEntry *orig; /* the version this is a copy from (or NULL) */
    UA_UInt16 refCount; /* How many consumers have a reference to the node? */
    UA_Boolean deleted; /* Node was marked as deleted and can be deleted when refCount == 0 */
    UA_Node node;
} UA_NodeMapEntry;

static void checkAllReleased(void *context, const UA_Node* node) {
    UA_NodeMapEntry *entry = container_of(node, UA_NodeMapEntry, node);
    ck_assert_int_eq(entry->refCount, 1); /* The count is increased when the visited node is checked out */
}

UA_Nodestore ns;

static void setup(void) {
    UA_Nodestore_default_new(&ns);
}

static void teardown(void) {
    ns.iterate(ns.context, NULL, checkAllReleased);
    ns.deleteNodestore(ns.context);
}

static int zeroCnt = 0;
static int visitCnt = 0;
static void checkZeroVisitor(void *context, const UA_Node* node) {
    visitCnt++;
    if (node == NULL) zeroCnt++;
}

static UA_Node* createNode(UA_Int16 nsid, UA_Int32 id) {
    UA_Node *p = ns.newNode(ns.context, UA_NODECLASS_VARIABLE);
    p->nodeId.identifierType = UA_NODEIDTYPE_NUMERIC;
    p->nodeId.namespaceIndex = nsid;
    p->nodeId.identifier.numeric = id;
    p->nodeClass = UA_NODECLASS_VARIABLE;
    return p;
}

START_TEST(replaceExistingNode) {
    UA_Node* n1 = createNode(0,2253);
    ns.insertNode(ns.context, n1, NULL);
    UA_NodeId in1 = UA_NODEID_NUMERIC(0, 2253);
    UA_Node* n2;
    ns.getNodeCopy(ns.context, &in1, &n2);
    UA_StatusCode retval = ns.replaceNode(ns.context, n2);
    ck_assert_int_eq(retval, UA_STATUSCODE_GOOD);
}
END_TEST

START_TEST(replaceOldNode) {
    UA_Node* n1 = createNode(0,2253);
    ns.insertNode(ns.context, n1, NULL);
    UA_NodeId in1 = UA_NODEID_NUMERIC(0,2253);
    UA_Node* n2;
    UA_Node* n3;
    ns.getNodeCopy(ns.context, &in1, &n2);
    ns.getNodeCopy(ns.context, &in1, &n3);

    /* shall succeed */
    UA_StatusCode retval = ns.replaceNode(ns.context, n2);
    ck_assert_int_eq(retval, UA_STATUSCODE_GOOD);

    /* shall fail */
    retval = ns.replaceNode(ns.context, n3);
    ck_assert_int_ne(retval, UA_STATUSCODE_GOOD);
}
END_TEST

START_TEST(findNodeInUA_NodeStoreWithSingleEntry) {
    UA_Node* n1 = createNode(0,2253);
    ns.insertNode(ns.context, n1, NULL);
    UA_NodeId in1 = UA_NODEID_NUMERIC(0,2253);
    const UA_Node* nr = ns.getNode(ns.context, &in1);
    ck_assert_int_eq((uintptr_t)n1, (uintptr_t)nr);
    ns.releaseNode(ns.context, nr);
}
END_TEST

START_TEST(failToFindNodeInOtherUA_NodeStore) {
    UA_Node* n1 = createNode(0,2255);
    ns.insertNode(ns.context, n1, NULL);
    UA_NodeId in1 = UA_NODEID_NUMERIC(1, 2255);
    const UA_Node* nr = ns.getNode(ns.context, &in1);
    ck_assert_int_eq((uintptr_t)nr, 0);
}
END_TEST

START_TEST(findNodeInUA_NodeStoreWithSeveralEntries) {
    UA_Node* n1 = createNode(0,2253);
    ns.insertNode(ns.context, n1, NULL);
    UA_Node* n2 = createNode(0,2255);
    ns.insertNode(ns.context, n2, NULL);
    UA_Node* n3 = createNode(0,2257);
    ns.insertNode(ns.context, n3, NULL);
    UA_Node* n4 = createNode(0,2200);
    ns.insertNode(ns.context, n4, NULL);
    UA_Node* n5 = createNode(0,1);
    ns.insertNode(ns.context, n5, NULL);
    UA_Node* n6 = createNode(0,12);
    ns.insertNode(ns.context, n6, NULL);

    UA_NodeId in3 = UA_NODEID_NUMERIC(0, 2257);
    const UA_Node* nr = ns.getNode(ns.context, &in3);
    ck_assert_int_eq((uintptr_t)nr, (uintptr_t)n3);
    ns.releaseNode(ns.context, nr);
}
END_TEST

START_TEST(iterateOverUA_NodeStoreShallNotVisitEmptyNodes) {
    UA_Node* n1 = createNode(0,2253);
    ns.insertNode(ns.context, n1, NULL);
    UA_Node* n2 = createNode(0,2255);
    ns.insertNode(ns.context, n2, NULL);
    UA_Node* n3 = createNode(0,2257);
    ns.insertNode(ns.context, n3, NULL);
    UA_Node* n4 = createNode(0,2200);
    ns.insertNode(ns.context, n4, NULL);
    UA_Node* n5 = createNode(0,1);
    ns.insertNode(ns.context, n5, NULL);
    UA_Node* n6 = createNode(0,12);
    ns.insertNode(ns.context, n6, NULL);

    zeroCnt = 0;
    visitCnt = 0;
    ns.iterate(ns.context, NULL, checkZeroVisitor);
    ck_assert_int_eq(zeroCnt, 0);
    ck_assert_int_eq(visitCnt, 6);
}
END_TEST

START_TEST(findNodeInExpandedNamespace) {
    for(UA_UInt32 i = 0; i < 200; i++) {
        UA_Node* n = createNode(0,i);
        ns.insertNode(ns.context, n, NULL);
    }
    // when
    UA_Node *n2 = createNode(0,25);
    const UA_Node* nr = ns.getNode(ns.context, &n2->nodeId);
    ck_assert_int_eq(nr->nodeId.identifier.numeric, n2->nodeId.identifier.numeric);
    ns.releaseNode(ns.context, nr);
    ns.deleteNode(ns.context, n2);
}
END_TEST

START_TEST(iterateOverExpandedNamespaceShallNotVisitEmptyNodes) {
    for(UA_UInt32 i = 0; i < 200; i++) {
        UA_Node* n = createNode(0,i+1);
        ns.insertNode(ns.context, n, NULL);
    }
    // when
    zeroCnt = 0;
    visitCnt = 0;
    ns.iterate(ns.context, NULL, checkZeroVisitor);
    // then
    ck_assert_int_eq(zeroCnt, 0);
    ck_assert_int_eq(visitCnt, 200);
}
END_TEST

START_TEST(failToFindNonExistentNodeInUA_NodeStoreWithSeveralEntries) {
    UA_Node* n1 = createNode(0,2253);
    ns.insertNode(ns.context, n1, NULL);
    UA_Node* n2 = createNode(0,2255);
    ns.insertNode(ns.context, n2, NULL);
    UA_Node* n3 = createNode(0,2257);
    ns.insertNode(ns.context, n3, NULL);
    UA_Node* n4 = createNode(0,2200);
    ns.insertNode(ns.context, n4, NULL);
    UA_Node* n5 = createNode(0,1);
    ns.insertNode(ns.context, n5, NULL);

    UA_NodeId id = UA_NODEID_NUMERIC(0, 12);
    const UA_Node* nr = ns.getNode(ns.context, &id);
    ck_assert_int_eq((uintptr_t)nr, 0);
}
END_TEST

/************************************/
/* Performance Profiling Test Cases */
/************************************/

#ifdef UA_ENABLE_MULTITHREADING
struct UA_NodeStoreProfileTest {
    UA_Int32 min_val;
    UA_Int32 max_val;
    UA_Int32 rounds;
};

static void *profileGetThread(void *arg) {
    struct UA_NodeStoreProfileTest *test = (struct UA_NodeStoreProfileTest*) arg;
    UA_NodeId id;
    UA_NodeId_init(&id);
    UA_Int32 max_val = test->max_val;
    for(UA_Int32 x = 0; x<test->rounds; x++) {
        for(UA_Int32 i=test->min_val; i<max_val; i++) {
            id.identifier.numeric = i+1;
            const UA_Node *n = ns.getNode(ns.context, &id);
            ns.releaseNode(ns.context, n);
        }
    }
    return NULL;
}
#endif

#define N 1000 /* make bigger to test */

START_TEST(profileGetDelete) {
    clock_t begin, end;
    begin = clock();

    for(UA_UInt32 i = 0; i < N; i++) {
        UA_Node *n = createNode(0,i+1);
        ns.insertNode(ns.context, n, NULL);
    }

#ifdef UA_ENABLE_MULTITHREADING
#define THREADS 4
    pthread_t t[THREADS];
    struct UA_NodeStoreProfileTest p[THREADS];
    for (int i = 0; i < THREADS; i++) {
        p[i] = (struct UA_NodeStoreProfileTest){i*(N/THREADS), (i+1)*(N/THREADS), 50};
        pthread_create(&t[i], NULL, profileGetThread, &p[i]);
    }
    for (int i = 0; i < THREADS; i++)
        pthread_join(t[i], NULL);
    end = clock();
    printf("Time for %d create/get/delete on %d threads in a namespace: %fs.\n", N, THREADS, (double)(end - begin) / CLOCKS_PER_SEC);
#else
    UA_NodeId id = UA_NODEID_NULL;
    for(size_t i = 0; i < N; i++) {
        id.identifier.numeric = (UA_UInt32)i+1;
        const UA_Node *node = ns.getNode(ns.context, &id);
        ns.releaseNode(ns.context, node);
    }
    end = clock();
    printf("Time for single-threaded %d create/get/delete in a namespace: %fs.\n", N,
           (double)(end - begin) / CLOCKS_PER_SEC);
#endif
}
END_TEST

static Suite * namespace_suite (void) {
    Suite *s = suite_create ("UA_NodeStore");

    TCase* tc_find = tcase_create ("Find");
    tcase_add_checked_fixture(tc_find, setup, teardown);
    tcase_add_test (tc_find, findNodeInUA_NodeStoreWithSingleEntry);
    tcase_add_test (tc_find, findNodeInUA_NodeStoreWithSeveralEntries);
    tcase_add_test (tc_find, findNodeInExpandedNamespace);
    tcase_add_test (tc_find, failToFindNonExistentNodeInUA_NodeStoreWithSeveralEntries);
    tcase_add_test (tc_find, failToFindNodeInOtherUA_NodeStore);
    suite_add_tcase (s, tc_find);

    TCase *tc_replace = tcase_create("Replace");
    tcase_add_checked_fixture(tc_replace, setup, teardown);
    tcase_add_test (tc_replace, replaceExistingNode);
    tcase_add_test (tc_replace, replaceOldNode);
    suite_add_tcase (s, tc_replace);

    TCase* tc_iterate = tcase_create ("Iterate");
    tcase_add_checked_fixture(tc_iterate, setup, teardown);
    tcase_add_test (tc_iterate, iterateOverUA_NodeStoreShallNotVisitEmptyNodes);
    tcase_add_test (tc_iterate, iterateOverExpandedNamespaceShallNotVisitEmptyNodes);
    suite_add_tcase (s, tc_iterate);
    
    TCase* tc_profile = tcase_create ("Profile");
    tcase_add_checked_fixture(tc_profile, setup, teardown);
    tcase_add_test (tc_profile, profileGetDelete);
    suite_add_tcase (s, tc_profile);

    return s;
}


int main (void) {
    int number_failed = 0;
    Suite *s = namespace_suite();
    SRunner *sr = srunner_create(s);
    srunner_set_fork_status(sr,CK_NOFORK);
    srunner_run_all(sr, CK_NORMAL);
    number_failed += srunner_ntests_failed (sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
