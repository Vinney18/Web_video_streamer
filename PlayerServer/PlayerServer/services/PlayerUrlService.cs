using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Net.Http;
using System.Threading.Tasks;

namespace PlayerServer.services
{
    public class PlayerUrlService
    {
        HttpClient httpClient;
        public PlayerUrlService()
        {
            httpClient = new HttpClient()
            {
                BaseAddress = new Uri($"http://{ServerDetails.AttachedServerIp}:{ServerDetails.port}")
            };
            if (!string.IsNullOrEmpty(ServerDetails.token))
            {
                httpClient.DefaultRequestHeaders.Authorization = new System.Net.Http.Headers.AuthenticationHeaderValue("Bearer", ServerDetails.token);
            }
        }

        public async Task<string> GetLiveUrlAsync(int cameraId, int streamType)
        {
            var httpResponse = await httpClient.GetAsync("/RestService/server/LiveUrl?cameraId=" + cameraId + "&streamType=" + streamType);
            if (httpResponse.StatusCode == System.Net.HttpStatusCode.OK)
            {
                return await httpResponse.Content.ReadAsStringAsync();
            }
            return "";
        }

        public async Task<string> GetPlaybackUrl(int cameraId, long time)
        {
            DateTimeOffset dateTime = DateTimeOffset.FromUnixTimeSeconds(time);
            string timePlayback = dateTime.LocalDateTime.ToString("MM/dd/yyyy, hh:mm:ss tt", CultureInfo.InvariantCulture);
            var httpResponse = await httpClient.GetAsync("/RestService/server/EventPlaybackUrl?cameraId=" + cameraId + "&eventDateTime=" + timePlayback);
            if (httpResponse.StatusCode == System.Net.HttpStatusCode.OK)
            {
                return await httpResponse.Content.ReadAsStringAsync();
            }
            return "";
        }
    }
}
