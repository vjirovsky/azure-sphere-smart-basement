using IoTHubTrigger = Microsoft.Azure.WebJobs.EventHubTriggerAttribute;

using Microsoft.Azure.WebJobs;
using Microsoft.Azure.WebJobs.Host;
using Microsoft.Azure.EventHubs;
using System.Text;
using System.Net.Http;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using Newtonsoft.Json;
using System.Threading.Tasks;
using System.Net;
using System.Globalization;

namespace AzureSphereSmartBasementFunctionApp
{
    public static class ReactToMessageInIoTHub
    {
        private const float ABSOLUTE_ANGULAR_THRESHOLD = 0.8f;
        private const float ABSOLUTE_ACCELERATION_THRESHOLD = 10.0f;
        private const float ABSOLUTE_AMBIENT_THRESHOLD = 3.5f;
        private const float ABSOLUTE_MICROPHONE_THRESHOLD = 4.0f;

        [FunctionName("ReactToMessageInIoTHub")]
        public async static Task Run([IoTHubTrigger("messages/events", Connection = "IoTHubConnectionString")]EventData message, ILogger log)
        {
            var telegramBotToken = Environment.GetEnvironmentVariable("TELEGRAM_BOT_TOKEN");

            // recepient who should recieve the message
            var telegramChatId = Environment.GetEnvironmentVariable("TELEGRAM_CHAT_ID");

            dynamic msgObj = JsonConvert.DeserializeObject(Encoding.UTF8.GetString(message.Body.Array));

            string results;

            if (msgObj._t == null || msgObj._t != "alarm")
            {
                results = await SendTelegramMessage("unsupported message in IoT Hub!", telegramChatId, telegramBotToken);
            }
            else
            {
                float aAc = 0.0f, aAr = 0.0f, aAl = 0.0f, aMic = 0.0f;
                string aAcString = String.Format("ahoj {0}", msgObj);
                log.LogInformation(aAcString);

                if (msgObj.aAc != null)
                {
                    //absolute acceleration difference
                    aAc = float.Parse((string) msgObj.aAc, CultureInfo.InvariantCulture);
                }
                if (msgObj.aAr != null)
                {
                    //absolute angular rate difference
                    aAr = float.Parse((string) msgObj.aAr, CultureInfo.InvariantCulture);
                }
                if (msgObj.aAl != null)
                {
                    //absolute ambient light difference
                    aAl = float.Parse((string) msgObj.aAl, CultureInfo.InvariantCulture);
                }
                if (msgObj.aMic != null)
                {
                    //absolute microphone difference
                    aMic = float.Parse((string)msgObj.aMic, CultureInfo.InvariantCulture);
                }
                if (aAc > ABSOLUTE_ACCELERATION_THRESHOLD)
                {
                    results = await SendTelegramMessage("--Breach detected (based on acceleration sensor)!--", telegramChatId, telegramBotToken);
                }
                else if (aAr > ABSOLUTE_ANGULAR_THRESHOLD)
                {
                    results = await SendTelegramMessage("--Breach detected (based on angular rate sensor)!--", telegramChatId, telegramBotToken);
                }
                else if (aAl > ABSOLUTE_AMBIENT_THRESHOLD)
                {
                    results = await SendTelegramMessage("--Breach detected (based on ambient light sensor)!--", telegramChatId, telegramBotToken);
                }
                else if (aMic > ABSOLUTE_MICROPHONE_THRESHOLD)
                {
                    results = await SendTelegramMessage("--Breach detected (based on microphone)!--", telegramChatId, telegramBotToken);
                }
                else {

                    results = await SendTelegramMessage("--Breach detected!--", telegramChatId, telegramBotToken);
                }
            }
            log.LogInformation(String.Format("{0}", results));

            return;
        }

        public static async Task<string> SendTelegramMessage(string text, string chatId, string botToken)
        {
            using (var client = new HttpClient())
            {

                Dictionary<string, string> dictionary = new Dictionary<string, string>();
                dictionary.Add("chat_id", chatId);
                dictionary.Add("text", text);

                string json = JsonConvert.SerializeObject(dictionary);
                var requestData = new StringContent(json, Encoding.UTF8, "application/json");

                var response = await client.PostAsync(String.Format("https://api.telegram.org/bot" + botToken + "/sendMessage"), requestData);
                var result = await response.Content.ReadAsStringAsync();

                return result;
            }
        }
    }
}