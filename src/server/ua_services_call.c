/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. 
 *
 *    Copyright 2015 (c) Chris Iatrou
 *    Copyright 2015-2017 (c) Florian Palm
 *    Copyright 2015-2017 (c) Fraunhofer IOSB (Author: Julius Pfrommer)
 *    Copyright 2015-2016 (c) Sten Grüner
 *    Copyright 2015 (c) Oleksiy Vasylyev
 *    Copyright 2016 (c) LEvertz
 *    Copyright 2017 (c) Stefan Profanter, fortiss GmbH
 *    Copyright 2017 (c) Julian Grothoff
 */

#include "ua_services.h"
#include "ua_server_internal.h"

#ifdef UA_ENABLE_METHODCALLS /* conditional compilation */

static const UA_VariableNode *
getArgumentsVariableNode(UA_Server *server, const UA_MethodNode *ofMethod,
                         UA_String withBrowseName) {
    UA_NodeId hasProperty = UA_NODEID_NUMERIC(0, UA_NS0ID_HASPROPERTY);
    for(size_t i = 0; i < ofMethod->referencesSize; ++i) {
        UA_NodeReferenceKind *rk = &ofMethod->references[i];

        if(rk->isInverse != false)
            continue;

        if(!UA_NodeId_equal(&hasProperty, &rk->referenceTypeId))
            continue;

        for(size_t j = 0; j < rk->targetIdsSize; ++j) {
            const UA_Node *refTarget =
                server->config.nodestore.getNode(server->config.nodestore.context,
                                                 &rk->targetIds[j].nodeId);
            if(!refTarget)
                continue;
            if(refTarget->nodeClass == UA_NODECLASS_VARIABLE &&
               refTarget->browseName.namespaceIndex == 0 &&
               UA_String_equal(&withBrowseName, &refTarget->browseName.name)) {
                return (const UA_VariableNode*)refTarget;
            }
            server->config.nodestore.releaseNode(server->config.nodestore.context,
                                                 refTarget);
        }
    }
    return NULL;
}

static UA_StatusCode
typeCheckArguments(UA_Server *server, const UA_VariableNode *argRequirements,
                   size_t argsSize, UA_Variant *args) {
    /* Verify that we have a Variant containing UA_Argument (scalar or array) in
     * the "InputArguments" node */
    if(argRequirements->valueSource != UA_VALUESOURCE_DATA)
        return UA_STATUSCODE_BADINTERNALERROR;
    if(!argRequirements->value.data.value.hasValue)
        return UA_STATUSCODE_BADINTERNALERROR;
    if(argRequirements->value.data.value.value.type != &UA_TYPES[UA_TYPES_ARGUMENT])
        return UA_STATUSCODE_BADINTERNALERROR;

    /* Verify the number of arguments. A scalar argument value is interpreted as
     * an array of length 1. */
    size_t argReqsSize = argRequirements->value.data.value.value.arrayLength;
    if(UA_Variant_isScalar(&argRequirements->value.data.value.value))
        argReqsSize = 1;
    if(argReqsSize > argsSize)
        return UA_STATUSCODE_BADARGUMENTSMISSING;
    if(argReqsSize < argsSize)
        return UA_STATUSCODE_BADTOOMANYARGUMENTS;

    /* Type-check every argument against the definition */
    UA_Argument *argReqs = (UA_Argument*)argRequirements->value.data.value.value.data;
    for(size_t i = 0; i < argReqsSize; ++i) {
        if(!compatibleValue(server, &argReqs[i].dataType, argReqs[i].valueRank,
                            argReqs[i].arrayDimensionsSize, argReqs[i].arrayDimensions,
                            &args[i], NULL))
            return UA_STATUSCODE_BADTYPEMISMATCH;
    }
    return UA_STATUSCODE_GOOD;
}

static UA_StatusCode
validMethodArguments(UA_Server *server, const UA_MethodNode *method,
                     const UA_CallMethodRequest *request) {
    /* Get the input arguments node */
    const UA_VariableNode *inputArguments =
        getArgumentsVariableNode(server, method, UA_STRING("InputArguments"));
    UA_StatusCode retval = UA_STATUSCODE_GOOD;
    if(!inputArguments) {
        if(request->inputArgumentsSize > 0)
            retval = UA_STATUSCODE_BADINVALIDARGUMENT;
        return retval;
    }

    /* Verify the request */
    retval = typeCheckArguments(server, inputArguments,
                                request->inputArgumentsSize,
                                request->inputArguments);

    /* Release the input arguments node */
    server->config.nodestore.releaseNode(server->config.nodestore.context,
                                         (const UA_Node*)inputArguments);
    return retval;
}

static const UA_NodeId hasComponentNodeId = {0, UA_NODEIDTYPE_NUMERIC, {UA_NS0ID_HASCOMPONENT}};
static const UA_NodeId hasSubTypeNodeId = {0, UA_NODEIDTYPE_NUMERIC, {UA_NS0ID_HASSUBTYPE}};

static void
callWithMethodAndObject(UA_Server *server, UA_Session *session,
                        const UA_CallMethodRequest *request, UA_CallMethodResult *result,
                        const UA_MethodNode *method, const UA_ObjectNode *object) {
    /* Verify the object's NodeClass */
    if(object->nodeClass != UA_NODECLASS_OBJECT &&
       object->nodeClass != UA_NODECLASS_OBJECTTYPE) {
        result->statusCode = UA_STATUSCODE_BADNODECLASSINVALID;
        return;
    }

    /* Verify the method's NodeClass */
    if(method->nodeClass != UA_NODECLASS_METHOD) {
        result->statusCode = UA_STATUSCODE_BADNODECLASSINVALID;
        return;
    }

    /* Is there a method to execute? */
    if(!method->method) {
        result->statusCode = UA_STATUSCODE_BADINTERNALERROR;
        return;
    }

    /* Verify method/object relations. Object must have a hasComponent or a
     * subtype of hasComponent reference to the method node. Therefore, check
     * every reference between the parent object and the method node if there is
     * a hasComponent (or subtype) reference */
    UA_Boolean found = false;
    for(size_t i = 0; i < object->referencesSize && !found; ++i) {
        UA_NodeReferenceKind *rk = &object->references[i];
        if(rk->isInverse)
            continue;
        if(!isNodeInTree(&server->config.nodestore, &rk->referenceTypeId,
                         &hasComponentNodeId, &hasSubTypeNodeId, 1))
            continue;
        for(size_t j = 0; j < rk->targetIdsSize; ++j) {
            if(UA_NodeId_equal(&rk->targetIds[j].nodeId, &request->methodId)) {
                found = true;
                break;
            }
        }
    }
    if(!found) {
        result->statusCode = UA_STATUSCODE_BADMETHODINVALID;
        return;
    }

    /* Verify access rights */
    UA_Boolean executable = method->executable;
    if(session != &adminSession)
        executable = executable &&
            server->config.accessControl.getUserExecutableOnObject(server, 
                           &server->config.accessControl, &session->sessionId,
                           session->sessionHandle, &request->methodId, method->context,
                           &request->objectId, object->context);
    if(!executable) {
        result->statusCode = UA_STATUSCODE_BADNOTWRITABLE; // There is no NOTEXECUTABLE?
        return;
    }

    /* Verify Input Arguments */
    result->statusCode = validMethodArguments(server, method, request);
    if(result->statusCode != UA_STATUSCODE_GOOD)
        return;

    /* Get the output arguments node */
    const UA_VariableNode *outputArguments =
        getArgumentsVariableNode(server, method, UA_STRING("OutputArguments"));

    /* Allocate the output arguments array */
    if(outputArguments) {
        if(outputArguments->value.data.value.value.arrayLength > 0) {
            result->outputArguments = (UA_Variant*)
                UA_Array_new(outputArguments->value.data.value.value.arrayLength,
                             &UA_TYPES[UA_TYPES_VARIANT]);
            if(!result->outputArguments) {
                result->statusCode = UA_STATUSCODE_BADOUTOFMEMORY;
                return;
            }
            result->outputArgumentsSize = outputArguments->value.data.value.value.arrayLength;
        }

        /* Release the output arguments node */
        server->config.nodestore.releaseNode(server->config.nodestore.context,
                                             (const UA_Node*)outputArguments);
    }

    /* Call the method */
    result->statusCode = method->method(server, &session->sessionId, session->sessionHandle,
                                        &method->nodeId, (void*)(uintptr_t)method->context,
                                        &object->nodeId, (void*)(uintptr_t)&object->context,
                                        request->inputArgumentsSize, request->inputArguments,
                                        result->outputArgumentsSize, result->outputArguments);
    /* TODO: Verify Output matches the argument definition */
}

static void
Operation_CallMethod(UA_Server *server, UA_Session *session, void *context,
                     const UA_CallMethodRequest *request, UA_CallMethodResult *result) {
    /* Get the method node */
    const UA_MethodNode *method = (const UA_MethodNode*)
        server->config.nodestore.getNode(server->config.nodestore.context,
                                         &request->methodId);
    if(!method) {
        result->statusCode = UA_STATUSCODE_BADMETHODINVALID;
        return;
    }

    /* Get the object node */
    const UA_ObjectNode *object = (const UA_ObjectNode*)
        server->config.nodestore.getNode(server->config.nodestore.context,
                                         &request->objectId);
    if(!object) {
        result->statusCode = UA_STATUSCODE_BADNODEIDINVALID;
        server->config.nodestore.releaseNode(server->config.nodestore.context,
                                             (const UA_Node*)method);
        return;
    }

    /* Continue with method and object as context */
    callWithMethodAndObject(server, session, request, result, method, object);

    /* Release the method and object node */
    server->config.nodestore.releaseNode(server->config.nodestore.context,
                                         (const UA_Node*)method);
    server->config.nodestore.releaseNode(server->config.nodestore.context,
                                         (const UA_Node*)object);
}

void Service_Call(UA_Server *server, UA_Session *session,
                  const UA_CallRequest *request,
                  UA_CallResponse *response) {
    UA_LOG_DEBUG_SESSION(server->config.logger, session,
                         "Processing CallRequest");

    if(server->config.maxNodesPerMethodCall != 0 &&
       request->methodsToCallSize > server->config.maxNodesPerMethodCall) {
        response->responseHeader.serviceResult = UA_STATUSCODE_BADTOOMANYOPERATIONS;
        return;
    }

    response->responseHeader.serviceResult =
        UA_Server_processServiceOperations(server, session, (UA_ServiceOperation)Operation_CallMethod, NULL,
                                           &request->methodsToCallSize, &UA_TYPES[UA_TYPES_CALLMETHODREQUEST],
                                           &response->resultsSize, &UA_TYPES[UA_TYPES_CALLMETHODRESULT]);
}

UA_CallMethodResult UA_EXPORT
UA_Server_call(UA_Server *server, const UA_CallMethodRequest *request) {
    UA_CallMethodResult result;
    UA_CallMethodResult_init(&result);
    Operation_CallMethod(server, &adminSession, NULL, request, &result);
    return result;
}

#endif /* UA_ENABLE_METHODCALLS */
