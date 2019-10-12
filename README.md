# Demo project Smart Basement for Azure Sphere

This project demonstrates capabilities of Azure Sphere AVNET MT3620 Starter Kit - secure your house basement by alarm, made from accelerometer, gyroscope, ambient light sensors and microphone.

## How it works
When is security breach detected, notification is sent to user via Telegram bot.

You can arm alarm by pressing button A or by sending command `/arm` via Telegram bot.
You can disarm by sending command `/disarm` via Telegram bot.

All projects are set up for AVNET MT3620 Starter Kit, but with few changes can be used with Development Kit.

### Sensors
- Accelerometer+gyroscope are connected via I2C protocol
- Ambient light sensor is connected via ADC0(MT3620_ADC_CHANNEL0)
- Microphone is connected via MikroE Socket1 - connected as ADC1(MT3620_ADC_CHANNEL1)

### Buttons & LED
- Button A is connected via GPIO port 12 (AVNET_MT3620_SK_USER_BUTTON_A)
- User LED is connected via GPIO ports 8, 9, 10 (AVNET_MT3620_SK_USER_LED_RED,AVNET_MT3620_SK_USER_LED_GREEN,  AVNET_MT3620_SK_USER_LED_BLUE)

## Hardware

- [AVnet Azure Sphere MT3620 Starter Kit](https://www.avnet.com/shop/us/products/avid-technologies/aes-ms-mt3620-sk-g-3074457345636825680/)
- [MikroE Mic Click](https://mikroe.com/mic-click)

## Architecture

### Azure components
- Azure IoT Hub - provides IoT devices management, handling messages between device and cloud service
- Azure IoT Hub Device Provisioning Service
- Azure Functions `./cloud-backend/AzureSphereSmartBasementFunctionApp` - function, triggered by new message from device in IoT Hub
- Azure Bot Framework `./cloud-backend/SmartBasementBot`- provides bot communication from Telegram to device

## How to deploy solution

1. create Telegram bot ([manual](https://docs.microsoft.com/en-us/azure/bot-service/bot-service-channel-connect-telegram?view=azure-bot-service-4.0))
1. create Azure IoT Hub ([manual](https://docs.microsoft.com/cs-cz/azure/iot-dps/quick-setup-auto-provision))
1. create Azure IoT Hub DPS and link with IoT Hub ([manual](https://docs.microsoft.com/cs-cz/azure-sphere/app-development/setup-iot-hub))
1. create `./device-app/AzureSphereSmartBasement/app_manifest.json` file (use .template in same folder) and fill in:

    `AllowedConnections` - add your IoT Hub in format `someurl-basement.azure-devices.net`<br>
    `CmdArgs` - fill in scopeId of your IoT Hub DPS in format `["--SCOPE-ID--"]`<br>
    `DeviceAuthentication` - when device will be registered into IoT Hub, fill the Device ID
1. deploy `device-app` to device via Visual Studio
1. generate IoT Hub event token (IoT Hub -> Build-in endpoints -> Event Hub compatible endpoint
1. create `./cloud-backend/AzureSphereSmartBasementFunctionApp/local.settings.json` (use .template in same folder) and fill in:

    `IotHubConnectionString` - token from previous step<br>
    `TELEGRAM_BOT_TOKEN` - your Telegram bot token from step 1<br>
    `TELEGRAM_CHAT_ID` - your Telegram chatId ([how to get chat ID](https://answers.splunk.com/answers/590658/telegram-alert-action-where-do-you-get-a-chat-id.html))
1. deploy `AzureSphereSmartBasementFunctionApp` to Azure (create new Azure Function & deploy it)

1. generate IoT Hub service token (IoT Hub -> Shared access policies -> service -> connection string (primary key))
1. create `./cloud-backend/SmartBasementBot/appsettings.json` (use .template in same folder) and fill in:

    `IoTHub:ConnectionString` - token from previous step<br>
    `IoTHub:DeviceId` - Device ID (same as in DeviceAuthentication in app_manifest)<br>
1. deploy `SmartBasementBot` to Azure (create new Microsoft Bot Web & deploy it)
1. write message `/start` to Telegram bot
1. enjoy!
