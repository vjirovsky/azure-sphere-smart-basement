#include "iothub_helper.h"
#include "build_options.h"
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h> 
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <math.h>
#include <stdbool.h>

#include <applibs/log.h>
#include <azureiot/iothub_client_core_common.h>
#include <azureiot/iothub_device_client_ll.h>
#include <azureiot/iothub_client_options.h>
#include <azureiot/iothubtransportmqtt.h>
#include <azureiot/iothub.h>
#include <azureiot/azure_sphere_provisioning.h>
#include "epoll_timerfd_utilities.h"

IOTHUB_DEVICE_CLIENT_LL_HANDLE iothubClientHandle = NULL;
extern char scopeId[SCOPEID_LENGTH]; // ScopeId for the Azure IoT Central application, set in
									 // app_manifest.json, CmdArgs

extern bool g_is_iothub_authenticated;
extern sig_atomic_t g_is_app_exit_requested;
extern bool g_is_wifi_connected;
extern bool g_is_breach_detected;
extern bool g_is_armed;
extern sensors_state g_previous_state;

// Azure IoT poll periods
const int g_iot_hub_default_poll_period_seconds = 5;
const int AzureIoTMinReconnectPeriodSeconds = 60;
const int AzureIoTMaxReconnectPeriodSeconds = 10 * 60;

const int keepalivePeriodSeconds = 20;
int g_iot_hub_current_poll_period_seconds = -1;

int g_azure_iot_sync_timer_fd = -1;

extern IOTHUBMESSAGE_DISPOSITION_RESULT ReceiveMessageCallback(IOTHUB_MESSAGE_HANDLE message, void* userContextCallback);

/// <summary>
///     Sets up the Azure IoT Hub connection (creates the iothubClientHandle)
///     When the SAS Token for a device expires the connection needs to be recreated
///     which is why this is not simply a one time call.
/// </summary>
void SetupAzureClient(void)
{
	if (iothubClientHandle != NULL)
		IoTHubDeviceClient_LL_Destroy(iothubClientHandle);

	AZURE_SPHERE_PROV_RETURN_VALUE provResult =
		IoTHubDeviceClient_LL_CreateWithAzureSphereDeviceAuthProvisioning(scopeId, 10000,
			&iothubClientHandle);

	if (provResult.result != AZURE_SPHERE_PROV_RESULT_OK) {
		Log_Debug("IoTHubDeviceClient_LL_CreateWithAzureSphereDeviceAuthProvisioning returned '%s'.\n",
			getAzureSphereProvisioningResultString(provResult));

		// If we fail to connect, reduce the polling frequency, starting at
		// AzureIoTMinReconnectPeriodSeconds and with a backoff up to
		// AzureIoTMaxReconnectPeriodSeconds
		if (g_iot_hub_current_poll_period_seconds == g_iot_hub_default_poll_period_seconds) {
			g_iot_hub_current_poll_period_seconds = AzureIoTMinReconnectPeriodSeconds;
		}
		else {
			g_iot_hub_current_poll_period_seconds *= 2;
			if (g_iot_hub_current_poll_period_seconds > AzureIoTMaxReconnectPeriodSeconds) {
				g_iot_hub_current_poll_period_seconds = AzureIoTMaxReconnectPeriodSeconds;
			}
		}

		struct timespec azureTelemetryPeriod = { g_iot_hub_current_poll_period_seconds, 0 };
		SetTimerFdToPeriod(g_azure_iot_sync_timer_fd, &azureTelemetryPeriod);

		Log_Debug("ERROR: failure to create IoTHub Handle - will retry in %i seconds.\n",
			g_iot_hub_current_poll_period_seconds);
		return;
	}

	// Successfully connected, so make sure the polling frequency is back to the default
	g_iot_hub_current_poll_period_seconds = g_iot_hub_default_poll_period_seconds;
	struct timespec azureTelemetryPeriod = { g_iot_hub_current_poll_period_seconds, 0 };
	SetTimerFdToPeriod(g_azure_iot_sync_timer_fd, &azureTelemetryPeriod);

	g_is_iothub_authenticated = true;

	if (IoTHubDeviceClient_LL_SetOption(iothubClientHandle, OPTION_KEEP_ALIVE,
		&keepalivePeriodSeconds) != IOTHUB_CLIENT_OK) {
		Log_Debug("ERROR: failure setting option \"%s\"\n", OPTION_KEEP_ALIVE);
		return;
	}
	IoTHubDeviceClient_LL_SetMessageCallback(iothubClientHandle, ReceiveMessageCallback, NULL);
	//IoTHubDeviceClient_LL_SetDeviceTwinCallback(iothubClientHandle, TwinCallback, NULL);
	IoTHubDeviceClient_LL_SetConnectionStatusCallback(iothubClientHandle,
		HubConnectionStatusCallback, NULL);
}
/// <summary>
///     Sets the IoT Hub authentication state for the app
///     The SAS Token expires which will set the authentication state
/// </summary>
void HubConnectionStatusCallback(IOTHUB_CLIENT_CONNECTION_STATUS result,
	IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason,
	void* userContextCallback)
{
	g_is_iothub_authenticated = (result == IOTHUB_CLIENT_CONNECTION_AUTHENTICATED);
	if (!g_is_iothub_authenticated) {
		Log_Debug("IoT Hub Authenticated: %s\n", GetReasonString(reason));
	}
}


/// <summary>
/// Azure timer event:  Check connection status and send telemetry
/// </summary>
void AzureTimerEventHandler(EventData* eventData)
{
	if (ConsumeTimerFdEvent(g_azure_iot_sync_timer_fd) != 0) {
		g_is_app_exit_requested = true;
		return;
	}

	if (g_is_wifi_connected && !g_is_iothub_authenticated) {
		SetupAzureClient();
	}

	if (g_is_iothub_authenticated) {
		IoTHubDeviceClient_LL_DoWork(iothubClientHandle);
	}
}

/// <summary>
///     Converts the IoT Hub connection status reason to a string.
/// </summary>
const char* GetReasonString(IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason)
{
	char* reasonString = "unknown reason";
	switch (reason) {
	case IOTHUB_CLIENT_CONNECTION_EXPIRED_SAS_TOKEN:
		reasonString = "IOTHUB_CLIENT_CONNECTION_EXPIRED_SAS_TOKEN";
		break;
	case IOTHUB_CLIENT_CONNECTION_DEVICE_DISABLED:
		reasonString = "IOTHUB_CLIENT_CONNECTION_DEVICE_DISABLED";
		break;
	case IOTHUB_CLIENT_CONNECTION_BAD_CREDENTIAL:
		reasonString = "IOTHUB_CLIENT_CONNECTION_BAD_CREDENTIAL";
		break;
	case IOTHUB_CLIENT_CONNECTION_RETRY_EXPIRED:
		reasonString = "IOTHUB_CLIENT_CONNECTION_RETRY_EXPIRED";
		break;
	case IOTHUB_CLIENT_CONNECTION_NO_NETWORK:
		reasonString = "IOTHUB_CLIENT_CONNECTION_NO_NETWORK";
		break;
	case IOTHUB_CLIENT_CONNECTION_COMMUNICATION_ERROR:
		reasonString = "IOTHUB_CLIENT_CONNECTION_COMMUNICATION_ERROR";
		break;
	case IOTHUB_CLIENT_CONNECTION_OK:
		reasonString = "IOTHUB_CLIENT_CONNECTION_OK";
		break;
	}
	return reasonString;
}
/// <summary>
///     Converts AZURE_SPHERE_PROV_RETURN_VALUE to a string.
/// </summary>
const char* getAzureSphereProvisioningResultString(
	AZURE_SPHERE_PROV_RETURN_VALUE provisioningResult)
{
	switch (provisioningResult.result) {
	case AZURE_SPHERE_PROV_RESULT_OK:
		return "AZURE_SPHERE_PROV_RESULT_OK";
	case AZURE_SPHERE_PROV_RESULT_INVALID_PARAM:
		return "AZURE_SPHERE_PROV_RESULT_INVALID_PARAM";
	case AZURE_SPHERE_PROV_RESULT_NETWORK_NOT_READY:
		return "AZURE_SPHERE_PROV_RESULT_NETWORK_NOT_READY";
	case AZURE_SPHERE_PROV_RESULT_DEVICEAUTH_NOT_READY:
		return "AZURE_SPHERE_PROV_RESULT_DEVICEAUTH_NOT_READY";
	case AZURE_SPHERE_PROV_RESULT_PROV_DEVICE_ERROR:
		return "AZURE_SPHERE_PROV_RESULT_PROV_DEVICE_ERROR";
	case AZURE_SPHERE_PROV_RESULT_GENERIC_ERROR:
		return "AZURE_SPHERE_PROV_RESULT_GENERIC_ERROR";
	default:
		return "UNKNOWN_RETURN_VALUE";
	}
}


/// <summary>
/// Callback confirming message delivered to IoT Hub.
/// </summary>
void SendMessageCallback(IOTHUB_CLIENT_CONFIRMATION_RESULT result, void* context)
{
	if (result == IOTHUB_CLIENT_CONFIRMATION_OK) {
		Log_Debug("IoT Hub ACK - message delivered.");
	}
	else {
		Log_Debug("IoT Hub NACK - message NOT delivered.");
	}
}

/// <summary>
/// Send message from device to IoT Hub(device-to-cloud)
/// </summary>
void SendMessageToIoTHub(char payloadToSend[JSON_BUFFER_SIZE])
{

	IOTHUB_MESSAGE_HANDLE messageHandle = IoTHubMessage_CreateFromString(payloadToSend);

	if (messageHandle == 0) {
		Log_Debug("WARNING: unable to create a new IoTHubMessage\n");
		return;
	}
	IOTHUB_CLIENT_RESULT result = IoTHubDeviceClient_LL_SendEventAsync(iothubClientHandle, messageHandle, SendMessageCallback, 0);
	if (result != IOTHUB_CLIENT_OK) {
		Log_Debug("WARNING: failed to hand over the message to IoTHubClient\n");
	}

	IoTHubMessage_Destroy(messageHandle);
}


/// <summary>
/// Callback when is message from Azure IoT Hub sent to device (cloud-to-device)
/// </summary>
IOTHUBMESSAGE_DISPOSITION_RESULT ReceiveMessageCallback(IOTHUB_MESSAGE_HANDLE message, void* userContextCallback)
{

	const unsigned char* buffer = NULL;
	size_t size = 0;
	const char* messageId;
	const char* correlationId;

	// Message properties
	if ((messageId = IoTHubMessage_GetMessageId(message)) == NULL)
	{
		messageId = "<null>";
	}

	if ((correlationId = IoTHubMessage_GetCorrelationId(message)) == NULL)
	{
		correlationId = "<null>";
	}

	// Increment the counter
	// Message content
	IOTHUBMESSAGE_CONTENT_TYPE contentType = IoTHubMessage_GetContentType(message);
	if (contentType == IOTHUBMESSAGE_BYTEARRAY)
	{
		if (IoTHubMessage_GetByteArray(message, &buffer, &size) == IOTHUB_MESSAGE_OK)
		{
			//(void)printf("Received Message \r\n Message ID: %s\r\n Correlation ID: %s\r\n BINARY Data: <<<%.*s>>> & Size=%d\r\n", messageId, correlationId, (int)size, buffer, (int)size);
		}
		else
		{
			(void)printf("Failed getting the BINARY body of the message received.\r\n");
		}
	}
	else if (contentType == IOTHUBMESSAGE_STRING)
	{
		if ((buffer = (const unsigned char*)IoTHubMessage_GetString(message)) != NULL && (size = strlen((const char*)buffer)) > 0)
		{
			//(void)printf("Received Message\r\n Message ID: %s\r\n Correlation ID: %s\r\n STRING Data: <<<%.*s>>> & Size=%d\r\n", messageId, correlationId, (int)size, buffer, (int)size);

		}
		else
		{
			(void)printf("Failed getting the STRING body of the message received.\r\n");
		}
	}
	else
	{
		(void)printf("Failed getting the body of the message received (type %i).\r\n", contentType);
	}

	if (memcmp(buffer, "arm", size) == 0)
	{
		Log_Debug("Recieved command to arm from IoT Hub.\n");
		//reset previous alarm
		g_is_breach_detected = false;

		//reset previous values from sensor
		g_previous_state.is_acceleration_set = false;
		g_previous_state.is_angular_rate_set = false;
		g_previous_state.is_ambient_set = false;
		g_previous_state.is_microphone_set = false;

		//turn on armed state
		g_is_armed = true;
	}
	else if (memcmp(buffer, "disarm", size) == 0)
	{
		Log_Debug("Recieved command to disarm from IoT Hub.\n");

		//turn off armed state
		g_is_armed = false;
	}
	else {
		Log_Debug("Unknown command recieved from IoT Hub - %s\n", buffer);
	}

	return IOTHUBMESSAGE_ACCEPTED;
}