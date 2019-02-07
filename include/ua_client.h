/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 *    Copyright 2015-2017 (c) Fraunhofer IOSB (Author: Julius Pfrommer)
 *    Copyright 2015-2016 (c) Sten Grüner
 *    Copyright 2015-2016 (c) Chris Iatrou
 *    Copyright 2015-2017 (c) Florian Palm
 *    Copyright 2015 (c) Holger Jeromin
 *    Copyright 2015 (c) Oleksiy Vasylyev
 *    Copyright 2017 (c) Stefan Profanter, fortiss GmbH
 *    Copyright 2017 (c) Mark Giraud, Fraunhofer IOSB
 *    Copyright 2018 (c) Thomas Stalder, Blue Time Concept SA
 *    Copyright 2018 (c) Kalycito Infotech Private Limited
 */

#ifndef UA_CLIENT_H_
#define UA_CLIENT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "ua_types.h"
#include "ua_types_generated.h"
#include "ua_types_generated_handling.h"
#include "ua_plugin_securitypolicy.h"
#include "ua_plugin_network.h"
#include "ua_plugin_log.h"
#include "ua_client_config.h"
#include "ua_nodeids.h"

/**
 * .. _client:
 *
 * Client
 * ======
 *
 * The client implementation allows remote access to all OPC UA services. For
 * convenience, some functionality has been wrapped in :ref:`high-level
 * abstractions <client-highlevel>`.
 *
 * **However**: At this time, the client does not yet contain its own thread or
 * event-driven main-loop. So the client will not perform any actions
 * automatically in the background. This is especially relevant for
 * subscriptions. The user will have to periodically call
 * `UA_Client_Subscriptions_manuallySendPublishRequest`. See also :ref:`here
 * <client-subscriptions>`.
 *
 *
 * .. include:: client_config.rst
 *
 * Client Lifecycle
 * ---------------- */

/* Create a new client */
UA_Client UA_EXPORT *
UA_Client_new(UA_ClientConfig config);

/* Creates a new secure client with the required configuration, certificate
 * privatekey, trustlist and revocation list.
 *
 * @param  config                   new secure configuration for client
 * @param  certificate              client certificate
 * @param  privateKey               client's private key
 * @param  remoteCertificate        server certificate form the endpoints
 * @param  trustList                list of trustable certificate
 * @param  trustListSize            count of trustList
 * @param  revocationList           list of revoked digital certificate
 * @param  revocationListSize       count of revocationList
 * @param  securityPolicyFunction   securityPolicy function
 * @return Returns a client configuration for secure channel */
UA_Client UA_EXPORT *
UA_Client_secure_new(UA_ClientConfig config, UA_ByteString certificate,
                     UA_ByteString privateKey, const UA_ByteString *remoteCertificate,
                     const UA_ByteString *trustList, size_t trustListSize,
                     const UA_ByteString *revocationList, size_t revocationListSize,
                     UA_SecurityPolicy_Func securityPolicyFunction);

/* Get the client connection status */
UA_ClientState UA_EXPORT
UA_Client_getState(UA_Client *client);

/* Get the client context */
void UA_EXPORT *
UA_Client_getContext(UA_Client *client);

/* Reset a client */
void UA_EXPORT
UA_Client_reset(UA_Client *client);

/* Delete a client */
void UA_EXPORT
UA_Client_delete(UA_Client *client);

/**
 * Connect to a Server
 * ------------------- */

/* Connect to the server
 *
 * @param client to use
 * @param endpointURL to connect (for example "opc.tcp://localhost:4840")
 * @return Indicates whether the operation succeeded or returns an error code */
UA_StatusCode UA_EXPORT
UA_Client_connect(UA_Client *client, const char *endpointUrl);

UA_StatusCode UA_EXPORT
UA_Client_connect_async (UA_Client *client, const char *endpointUrl,
                         UA_ClientAsyncServiceCallback callback,
                         void *connected);

/* Connect to the server without creating a session
 *
 * @param client to use
 * @param endpointURL to connect (for example "opc.tcp://localhost:4840")
 * @return Indicates whether the operation succeeded or returns an error code */
UA_StatusCode UA_EXPORT
UA_Client_connect_noSession(UA_Client *client, const char *endpointUrl);

/* Connect to the selected server with the given username and password
 *
 * @param client to use
 * @param endpointURL to connect (for example "opc.tcp://localhost:4840")
 * @param username
 * @param password
 * @return Indicates whether the operation succeeded or returns an error code */
UA_StatusCode UA_EXPORT
UA_Client_connect_username(UA_Client *client, const char *endpointUrl,
                           const char *username, const char *password);

/* Disconnect and close a connection to the selected server */
UA_StatusCode UA_EXPORT
UA_Client_disconnect(UA_Client *client);

UA_StatusCode UA_EXPORT
UA_Client_disconnect_async(UA_Client *client, UA_UInt32 *requestId);

/* Close a connection to the selected server */
UA_StatusCode UA_EXPORT
UA_Client_close(UA_Client *client);

/* Renew the underlying secure channel */
UA_StatusCode UA_EXPORT
UA_Client_manuallyRenewSecureChannel(UA_Client *client);

/**
 * Discovery
 * --------- */

/* Gets a list of endpoints of a server
 *
 * @param client to use. Must be connected to the same endpoint given in
 *        serverUrl or otherwise in disconnected state.
 * @param serverUrl url to connect (for example "opc.tcp://localhost:4840")
 * @param endpointDescriptionsSize size of the array of endpoint descriptions
 * @param endpointDescriptions array of endpoint descriptions that is allocated
 *        by the function (you need to free manually)
 * @return Indicates whether the operation succeeded or returns an error code */
UA_StatusCode UA_EXPORT
UA_Client_getEndpoints(UA_Client *client, const char *serverUrl,
                       size_t* endpointDescriptionsSize,
                       UA_EndpointDescription** endpointDescriptions);

/* Gets a list of all registered servers at the given server.
 *
 * You can pass an optional filter for serverUris. If the given server is not registered,
 * an empty array will be returned. If the server is registered, only that application
 * description will be returned.
 *
 * Additionally you can optionally indicate which locale you want for the server name
 * in the returned application description. The array indicates the order of preference.
 * A server may have localized names.
 *
 * @param client to use. Must be connected to the same endpoint given in
 *        serverUrl or otherwise in disconnected state.
 * @param serverUrl url to connect (for example "opc.tcp://localhost:4840")
 * @param serverUrisSize Optional filter for specific server uris
 * @param serverUris Optional filter for specific server uris
 * @param localeIdsSize Optional indication which locale you prefer
 * @param localeIds Optional indication which locale you prefer
 * @param registeredServersSize size of returned array, i.e., number of found/registered servers
 * @param registeredServers array containing found/registered servers
 * @return Indicates whether the operation succeeded or returns an error code */
UA_StatusCode UA_EXPORT
UA_Client_findServers(UA_Client *client, const char *serverUrl,
                      size_t serverUrisSize, UA_String *serverUris,
                      size_t localeIdsSize, UA_String *localeIds,
                      size_t *registeredServersSize,
                      UA_ApplicationDescription **registeredServers);

#ifdef UA_ENABLE_DISCOVERY
/* Get a list of all known server in the network. Only supported by LDS servers.
 *
 * @param client to use. Must be connected to the same endpoint given in
 * serverUrl or otherwise in disconnected state.
 * @param serverUrl url to connect (for example "opc.tcp://localhost:4840")
 * @param startingRecordId optional. Only return the records with an ID higher
 *        or equal the given. Can be used for pagination to only get a subset of
 *        the full list
 * @param maxRecordsToReturn optional. Only return this number of records

 * @param serverCapabilityFilterSize optional. Filter the returned list to only
 *        get servers with given capabilities, e.g. "LDS"
 * @param serverCapabilityFilter optional. Filter the returned list to only get
 *        servers with given capabilities, e.g. "LDS"
 * @param serverOnNetworkSize size of returned array, i.e., number of
 *        known/registered servers
 * @param serverOnNetwork array containing known/registered servers
 * @return Indicates whether the operation succeeded or returns an error code */
UA_StatusCode UA_EXPORT
UA_Client_findServersOnNetwork(UA_Client *client, const char *serverUrl,
                               UA_UInt32 startingRecordId, UA_UInt32 maxRecordsToReturn,
                               size_t serverCapabilityFilterSize, UA_String *serverCapabilityFilter,
                               size_t *serverOnNetworkSize, UA_ServerOnNetwork **serverOnNetwork);
#endif

/**
 * .. _client-services:
 *
 * Services
 * --------
 *
 * The raw OPC UA services are exposed to the client. But most of them time, it
 * is better to use the convenience functions from ``ua_client_highlevel.h``
 * that wrap the raw services. */
/* Don't use this function. Use the type versions below instead. */
void UA_EXPORT
__UA_Client_Service(UA_Client *client, const void *request,
                    const UA_DataType *requestType, void *response,
                    const UA_DataType *responseType);

/*
 * Attribute Service Set
 * ^^^^^^^^^^^^^^^^^^^^^ */
static UA_INLINE UA_ReadResponse
UA_Client_Service_read(UA_Client *client, const UA_ReadRequest request) {
    UA_ReadResponse response;
    __UA_Client_Service(client, &request, &UA_TYPES[UA_TYPES_READREQUEST],
                        &response, &UA_TYPES[UA_TYPES_READRESPONSE]);
    return response;
}

static UA_INLINE UA_WriteResponse
UA_Client_Service_write(UA_Client *client, const UA_WriteRequest request) {
    UA_WriteResponse response;
    __UA_Client_Service(client, &request, &UA_TYPES[UA_TYPES_WRITEREQUEST],
                        &response, &UA_TYPES[UA_TYPES_WRITERESPONSE]);
    return response;
}

/*
 * Method Service Set
 * ^^^^^^^^^^^^^^^^^^ */
#ifdef UA_ENABLE_METHODCALLS
static UA_INLINE UA_CallResponse
UA_Client_Service_call(UA_Client *client, const UA_CallRequest request) {
    UA_CallResponse response;
    __UA_Client_Service(client, &request, &UA_TYPES[UA_TYPES_CALLREQUEST],
                        &response, &UA_TYPES[UA_TYPES_CALLRESPONSE]);
    return response;
}
#endif

/*
 * NodeManagement Service Set
 * ^^^^^^^^^^^^^^^^^^^^^^^^^^ */
static UA_INLINE UA_AddNodesResponse
UA_Client_Service_addNodes(UA_Client *client, const UA_AddNodesRequest request) {
    UA_AddNodesResponse response;
    __UA_Client_Service(client, &request, &UA_TYPES[UA_TYPES_ADDNODESREQUEST],
                        &response, &UA_TYPES[UA_TYPES_ADDNODESRESPONSE]);
    return response;
}

static UA_INLINE UA_AddReferencesResponse
UA_Client_Service_addReferences(UA_Client *client,
                                const UA_AddReferencesRequest request) {
    UA_AddReferencesResponse response;
    __UA_Client_Service(client, &request, &UA_TYPES[UA_TYPES_ADDREFERENCESREQUEST],
                        &response, &UA_TYPES[UA_TYPES_ADDREFERENCESRESPONSE]);
    return response;
}

static UA_INLINE UA_DeleteNodesResponse
UA_Client_Service_deleteNodes(UA_Client *client,
                              const UA_DeleteNodesRequest request) {
    UA_DeleteNodesResponse response;
    __UA_Client_Service(client, &request, &UA_TYPES[UA_TYPES_DELETENODESREQUEST],
                        &response, &UA_TYPES[UA_TYPES_DELETENODESRESPONSE]);
    return response;
}

static UA_INLINE UA_DeleteReferencesResponse
UA_Client_Service_deleteReferences(UA_Client *client,
                                   const UA_DeleteReferencesRequest request) {
    UA_DeleteReferencesResponse response;
    __UA_Client_Service(client, &request, &UA_TYPES[UA_TYPES_DELETEREFERENCESREQUEST],
                        &response, &UA_TYPES[UA_TYPES_DELETEREFERENCESRESPONSE]);
    return response;
}

/*
 * View Service Set
 * ^^^^^^^^^^^^^^^^ */
static UA_INLINE UA_BrowseResponse
UA_Client_Service_browse(UA_Client *client, const UA_BrowseRequest request) {
    UA_BrowseResponse response;
    __UA_Client_Service(client, &request, &UA_TYPES[UA_TYPES_BROWSEREQUEST],
                        &response, &UA_TYPES[UA_TYPES_BROWSERESPONSE]);
    return response;
}

static UA_INLINE UA_BrowseNextResponse
UA_Client_Service_browseNext(UA_Client *client,
                             const UA_BrowseNextRequest request) {
    UA_BrowseNextResponse response;
    __UA_Client_Service(client, &request, &UA_TYPES[UA_TYPES_BROWSENEXTREQUEST],
                        &response, &UA_TYPES[UA_TYPES_BROWSENEXTRESPONSE]);
    return response;
}

static UA_INLINE UA_TranslateBrowsePathsToNodeIdsResponse
UA_Client_Service_translateBrowsePathsToNodeIds(UA_Client *client,
                        const UA_TranslateBrowsePathsToNodeIdsRequest request) {
    UA_TranslateBrowsePathsToNodeIdsResponse response;
    __UA_Client_Service(client, &request,
                        &UA_TYPES[UA_TYPES_TRANSLATEBROWSEPATHSTONODEIDSREQUEST],
                        &response,
                        &UA_TYPES[UA_TYPES_TRANSLATEBROWSEPATHSTONODEIDSRESPONSE]);
    return response;
}

static UA_INLINE UA_RegisterNodesResponse
UA_Client_Service_registerNodes(UA_Client *client,
                                const UA_RegisterNodesRequest request) {
    UA_RegisterNodesResponse response;
    __UA_Client_Service(client, &request, &UA_TYPES[UA_TYPES_REGISTERNODESREQUEST],
                        &response, &UA_TYPES[UA_TYPES_REGISTERNODESRESPONSE]);
    return response;
}

static UA_INLINE UA_UnregisterNodesResponse
UA_Client_Service_unregisterNodes(UA_Client *client,
                                  const UA_UnregisterNodesRequest request) {
    UA_UnregisterNodesResponse response;
    __UA_Client_Service(client, &request,
                        &UA_TYPES[UA_TYPES_UNREGISTERNODESREQUEST],
                        &response, &UA_TYPES[UA_TYPES_UNREGISTERNODESRESPONSE]);
    return response;
}

/*
 * Query Service Set
 * ^^^^^^^^^^^^^^^^^ */
static UA_INLINE UA_QueryFirstResponse
UA_Client_Service_queryFirst(UA_Client *client,
                             const UA_QueryFirstRequest request) {
    UA_QueryFirstResponse response;
    __UA_Client_Service(client, &request, &UA_TYPES[UA_TYPES_QUERYFIRSTREQUEST],
                        &response, &UA_TYPES[UA_TYPES_QUERYFIRSTRESPONSE]);
    return response;
}

static UA_INLINE UA_QueryNextResponse
UA_Client_Service_queryNext(UA_Client *client,
                            const UA_QueryNextRequest request) {
    UA_QueryNextResponse response;
    __UA_Client_Service(client, &request, &UA_TYPES[UA_TYPES_QUERYFIRSTREQUEST],
                        &response, &UA_TYPES[UA_TYPES_QUERYFIRSTRESPONSE]);
    return response;
}

/**
 * .. _client-async-services:
 *
 * Asynchronous Services
 * ---------------------
 * All OPC UA services are asynchronous in nature. So several service calls can
 * be made without waiting for a response first. Responess may come in a
 * different ordering. */

/* Listen on the network and process arriving asynchronous responses in the
 * background. Internal housekeeping and subscription management is done as
 * well. */

/*UA_StatusCode UA_EXPORT
UA_Client_runAsync(UA_Client *client, UA_UInt16 timeout);*/

/* Use the type versions of this method. See below. However, the general
 * mechanism of async service calls is explained here.
 *
 * We say that an async service call has been dispatched once this method
 * returns UA_STATUSCODE_GOOD. If there is an error after an async service has
 * been dispatched, the callback is called with an "empty" response where the
 * statusCode has been set accordingly. This is also done if the client is
 * shutting down and the list of dispatched async services is emptied.
 *
 * The statusCode received when the client is shutting down is
 * UA_STATUSCODE_BADSHUTDOWN.
 *
 * The statusCode received when the client don't receive response
 * after specified config->timeout (in ms) is
 * UA_STATUSCODE_BADTIMEOUT.
 *
 * Instead, you can use __UA_Client_AsyncServiceEx to specify
 * a custom timeout
 *
 * The userdata and requestId arguments can be NULL. */
UA_StatusCode UA_EXPORT
__UA_Client_AsyncService(UA_Client *client, const void *request,
                         const UA_DataType *requestType,
                         UA_ClientAsyncServiceCallback callback,
                         const UA_DataType *responseType,
                         void *userdata, UA_UInt32 *requestId);

/* For async connecting
 * */
UA_StatusCode UA_EXPORT
UA_Client_sendAsyncRequest(UA_Client *client, const void *request,
        const UA_DataType *requestType, UA_ClientAsyncServiceCallback callback,
const UA_DataType *responseType, void *userdata, UA_UInt32 *requestId);


UA_StatusCode UA_EXPORT
UA_Client_run_iterate(UA_Client *client, UA_UInt16 timeout);

/* Use the type versions of this method. See below. However, the general
 * mechanism of async service calls is explained here.
 *
 * We say that an async service call has been dispatched once this method
 * returns UA_STATUSCODE_GOOD. If there is an error after an async service has
 * been dispatched, the callback is called with an "empty" response where the
 * statusCode has been set accordingly. This is also done if the client is
 * shutting down and the list of dispatched async services is emptied.
 *
 * The statusCode received when the client is shutting down is
 * UA_STATUSCODE_BADSHUTDOWN.
 *
 * The statusCode received when the client don't receive response
 * after specified timeout (in ms) is
 * UA_STATUSCODE_BADTIMEOUT.
 *
 * The timeout can be disabled by setting timeout to 0
 *
 * The userdata and requestId arguments can be NULL. */
UA_StatusCode UA_EXPORT
__UA_Client_AsyncServiceEx(UA_Client *client, const void *request,
                           const UA_DataType *requestType,
                           UA_ClientAsyncServiceCallback callback,
                           const UA_DataType *responseType,
                           void *userdata, UA_UInt32 *requestId,
                           UA_UInt32 timeout);


/**
 * .. toctree::
 *
 *    client_highlevel
 *    client_subscriptions */

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* UA_CLIENT_H_ */
