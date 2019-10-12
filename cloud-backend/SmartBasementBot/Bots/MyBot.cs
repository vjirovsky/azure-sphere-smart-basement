// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.
//
// Generated with Bot Builder V4 SDK Template for Visual Studio EchoBot v4.5.0

using System;
using System.Collections.Generic;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Azure.Devices;
using Microsoft.Bot.Builder;
using Microsoft.Bot.Schema;
using Microsoft.Extensions.Options;

namespace SmartBasementBot.Bots
{
    public class MyBot : ActivityHandler
    {
        private ServiceClient _iotHubServiceClient;

        private readonly IOptions<MySettings> _mySettings;

        public MyBot(IOptions<MySettings> mySettings)
        {
            _mySettings = mySettings ?? throw new ArgumentNullException(nameof(mySettings));

            _iotHubServiceClient = ServiceClient.CreateFromConnectionString(_mySettings.Value.ConnectionString);
        }

        protected override async Task OnMessageActivityAsync(ITurnContext<IMessageActivity> turnContext, CancellationToken cancellationToken)
        {

            var messageText = turnContext.Activity.Text.ToLower().Trim().TrimStart('/');

            if (messageText.Equals("help") || messageText.Equals("start"))
            {
                await turnContext.SendActivityAsync(MessageFactory.Text($"/help - display this help\n\n/arm - turn on alarm\n\n/disarm - turn off alarm\n"), cancellationToken);

            }
            else if (messageText.Equals("arm"))
            {
                var commandMessage = new
                 Message(Encoding.ASCII.GetBytes("arm"));
                await _iotHubServiceClient.SendAsync(_mySettings.Value.DeviceId, commandMessage);

                await turnContext.SendActivityAsync(MessageFactory.Text($"Alarm has been turned on."), cancellationToken);

            }

            else if (messageText.Equals("disarm"))
            {

                var commandMessage = new
                 Message(Encoding.ASCII.GetBytes("disarm"));
                await _iotHubServiceClient.SendAsync(_mySettings.Value.DeviceId, commandMessage);

                await turnContext.SendActivityAsync(MessageFactory.Text($"Alarm has been turned off."), cancellationToken);

            }
            else
            {

                await turnContext.SendActivityAsync(MessageFactory.Text($"Unknown command - see /help"), cancellationToken);
            }


        }

        protected override async Task OnMembersAddedAsync(IList<ChannelAccount> membersAdded, ITurnContext<IConversationUpdateActivity> turnContext, CancellationToken cancellationToken)
        {
            foreach (var member in membersAdded)
            {
                if (member.Id != turnContext.Activity.Recipient.Id)
                {
                    await turnContext.SendActivityAsync(MessageFactory.Text($"Hello and welcome!"), cancellationToken);
                }
            }
        }
    }
}
