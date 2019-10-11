#pragma once
#include <azureiot\iothub_device_client_ll.h>
#include <azureiot\azure_sphere_provisioning.h>
#include "build_options.h"
#include <stdbool.h>
#include <azureiot\iothub_client_options.h>

void SetupAzureClient(void);
void HubConnectionStatusCallback(IOTHUB_CLIENT_CONNECTION_STATUS result, IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason, void* userContextCallback);
const char* GetReasonString(IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason);
const char* getAzureSphereProvisioningResultString(AZURE_SPHERE_PROV_RETURN_VALUE provisioningResult);
void SendMessageCallback(IOTHUB_CLIENT_CONFIRMATION_RESULT result, void* context);
void SendMessageToIoTHub(char payloadToSend[JSON_BUFFER_SIZE]);
IOTHUBMESSAGE_DISPOSITION_RESULT ReceiveMessageCallback(IOTHUB_MESSAGE_HANDLE message, void* userContextCallback);