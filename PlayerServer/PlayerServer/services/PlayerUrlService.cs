using Newtonsoft.Json;
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Net.NetworkInformation;
using System.Net.Sockets;
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

        public async Task<string> GetLiveUrlAsync(string cameraId, int streamType, string analyticType)
        {
            var httpResponse = await httpClient.GetAsync("/RestService/server/LiveUrl?cameraId=" + cameraId + "&streamType=" + streamType + "&analyticType=" + analyticType);
            if (httpResponse.StatusCode == System.Net.HttpStatusCode.OK)
            {
                var responseString = await httpResponse.Content.ReadAsStringAsync();
                if (responseString.StartsWith("\""))
                {
                    var url = JsonConvert.DeserializeObject<string>(responseString);
                    return url;
                }
                else
                {
                    return responseString;
                }
               
            }
            return "";
        }

        public async Task<string> GetPlaybackUrl(string cameraId, long time, bool streamviaapache)
        {
            DateTimeOffset dateTime = DateTimeOffset.FromUnixTimeSeconds(time);
            string timePlayback = dateTime.LocalDateTime.ToString("MM/dd/yyyy, hh:mm:ss tt", CultureInfo.InvariantCulture);
            var httpResponse = await httpClient.GetAsync("/RestService/server/EventPlaybackUrl?cameraId=" + cameraId + "&eventDateTime=" + timePlayback+ "&streamviaapache="+ streamviaapache);
            if (httpResponse.StatusCode == System.Net.HttpStatusCode.OK)
            {
                return await httpResponse.Content.ReadAsStringAsync();
            }
            return "";
        }

        public async Task<string> GetExportUrl(string cameraId, long start_time, long end_time)
        {
            try
            {
                // testing...
                // DateTime startDateTime = new DateTime(1970, 1, 1, 0, 0, 0).AddSeconds(start_time)
                //     .ToLocalTime();
                // Console.WriteLine(startDateTime);
                // var epoch = new DateTime(1970, 1, 1, 0, 0, 0);
                // var timeSpan = startDateTime - epoch;
                // Console.WriteLine(Convert.ToInt64(timeSpan.TotalSeconds));
                // Console.WriteLine(CommonMethods.GetDateTimeFromLong(Convert.ToInt64(timeSpan.TotalSeconds)));

                long startTimeLocal = CommonMethods.ConvertGMTToLocalUnixTime(start_time);
                long endTimeLocal = CommonMethods.ConvertGMTToLocalUnixTime(end_time);
                var httpResponse = await httpClient.GetAsync("/RestService/server/GetExportVideoUrl?cameraId=" + cameraId + "&startTime=" + startTimeLocal + "&endTime=" + endTimeLocal + "&fileFormat=ts");
                if (httpResponse.StatusCode == System.Net.HttpStatusCode.OK)
                {
                    Console.WriteLine("requesting export url for time range");
                    Console.WriteLine(httpResponse.RequestMessage.RequestUri);
                    Console.WriteLine("for camera id: " + cameraId);
                    Console.WriteLine("server ip: " + httpClient.BaseAddress);
                    Console.WriteLine("start time: " + DateTimeOffset.FromUnixTimeSeconds(start_time).LocalDateTime);
                    Console.WriteLine("end time: " + DateTimeOffset.FromUnixTimeSeconds(end_time).LocalDateTime);
                    Console.WriteLine(await httpResponse.Content.ReadAsStringAsync());
                    return await httpResponse.Content.ReadAsStringAsync();
                }
                else
                {
                    // endpoint
                    Console.WriteLine(httpResponse.RequestMessage.RequestUri);
                    Console.WriteLine(httpResponse.StatusCode);
                    // respnse err
                    Console.WriteLine(await httpResponse.Content.ReadAsStringAsync());
                }
                return "";

            }
            catch (Exception e)
            {
                Console.WriteLine(e);
                throw;
            }
        }

        public async Task<string> SeekVideo(int cameraId, long time, long seekTime, int sessionId)
        {
            DateTimeOffset dateTime = DateTimeOffset.FromUnixTimeSeconds(time);
            string timePlayback = dateTime.LocalDateTime.ToString("MM/dd/yyyy, hh:mm:ss tt", CultureInfo.InvariantCulture);
            var httpResponse = await httpClient.GetAsync("/RestService/server/SeekVideo?cameraId=" + cameraId + "&eventDateTime=" + timePlayback + "&seekTime=" + seekTime + "&sessionId="+ sessionId);
            if (httpResponse.StatusCode == System.Net.HttpStatusCode.OK)
            {
                return await httpResponse.Content.ReadAsStringAsync();
            }
            return "";
        }
        public async Task<string> ResumeVideo(int cameraId, long time, int sessionId)
        {
            DateTimeOffset dateTime = DateTimeOffset.FromUnixTimeSeconds(time);
            string timePlayback = dateTime.LocalDateTime.ToString("MM/dd/yyyy, hh:mm:ss tt", CultureInfo.InvariantCulture);
            var httpResponse = await httpClient.GetAsync("/RestService/server/ResumeVideo?cameraId=" + cameraId + "&eventDateTime=" + timePlayback +  "&sessionId=" + sessionId);
            if (httpResponse.StatusCode == System.Net.HttpStatusCode.OK)
            {
                return await httpResponse.Content.ReadAsStringAsync();
            }
            return "";
        }

        public async Task<string> PauseVideo(int cameraId, long time, int sessionId)
        {
            DateTimeOffset dateTime = DateTimeOffset.FromUnixTimeSeconds(time);
            string timePlayback = dateTime.LocalDateTime.ToString("MM/dd/yyyy, hh:mm:ss tt", CultureInfo.InvariantCulture);
            var httpResponse = await httpClient.GetAsync("/RestService/server/PauseVideo?cameraId=" + cameraId + "&eventDateTime=" + timePlayback + "&sessionId=" + sessionId);
            Console.WriteLine(httpResponse.StatusCode);
            if (httpResponse.StatusCode == System.Net.HttpStatusCode.OK)
            {
                return await httpResponse.Content.ReadAsStringAsync();
            }
            return "";
        }
    }

    public static class CommonMethods
    {
        public static string GetLocalIPAddress()
        {
            try
            {
                var host = Dns.GetHostEntryAsync(Dns.GetHostName()).Result;
                foreach (var ip in host.AddressList)
                {
                    if (ip.AddressFamily == AddressFamily.InterNetwork)
                    {
                        return ip.ToString();
                    }
                }
            }
            catch (Exception ex)
            {
                //ExceptionHandler.HandleException(ex);
            }
            return null;
        }

        public static string GetBindIP(string ipAddress)
        {
            if (ipAddress == "localhost" || ipAddress == "127.0.0.1")
            {
                return "127.0.0.1";
            }
            string snet = null;
            string commonIp = string.Empty;
            var ips = Dns.GetHostAddresses("");

            foreach (IPAddress ip in ips)
            {
                if (ip.AddressFamily == System.Net.Sockets.AddressFamily.InterNetwork)
                {
                    string second = ip.ToString();//gets ip of computer
                    snet = GetSubnetMask(ip).ToString();//gets subnet of above ip
                    bool result = CheckWhetherInSameNetwork(ipAddress, snet, second);//check whether this is the common network or not
                    if (result)
                    {
                        commonIp = ip.ToString();
                        File.WriteAllText(Path.Combine(Directory.GetCurrentDirectory(), "logs.txt"), $"Common Ip: {commonIp}");
                        break;
                    }
                }
            }
            if (commonIp == string.Empty)
            {
                commonIp = getCommonIpOnMatchScore(ipAddress);
                File.WriteAllText(Path.Combine(Directory.GetCurrentDirectory(), "logs.txt"), $"if empty Common Ip: {commonIp}");
            }
            if (commonIp == string.Empty)
            {
                foreach (IPAddress ip in ips)
                {
                    if (ip.AddressFamily == System.Net.Sockets.AddressFamily.InterNetwork)
                    {
                        commonIp = ip.ToString();
                        File.WriteAllText(Path.Combine(Directory.GetCurrentDirectory(), "logs.txt"), $"match Common Ip: {commonIp}");
                        break;
                    }

                }
            }
            return commonIp;
        }

        private static string getCommonIpOnMatchScore(string IpAddress)
        {
            string CommonIpAddress = string.Empty;
            string[] passedIpArray = IpAddress.Split('.');
            var ips = Dns.GetHostAddresses("");
            Dictionary<IPAddress, int> ScoreDic = new Dictionary<IPAddress, int>();
            foreach (IPAddress ip in ips)
            {
                if (ip.AddressFamily == System.Net.Sockets.AddressFamily.InterNetwork)
                {
                    int CurrentIpScore = 0;
                    string[] ipArray = ip.ToString().Split('.');
                    for (int i = 0; i < ipArray.Length; i++)
                    {
                        if (ipArray[0] == passedIpArray[0] || ipArray[1] == passedIpArray[1] || ipArray[2] == passedIpArray[2] || ipArray[3] == ipArray[3])
                        {
                            CurrentIpScore++;
                        }
                    }
                    if (CurrentIpScore > 0)
                    {
                        ScoreDic.Add(ip, CurrentIpScore);
                    }
                }
            }
            int max = ScoreDic.Max(kvp => kvp.Value);
            CommonIpAddress = ScoreDic.FirstOrDefault(x => x.Value == max).Key.ToString();
            return CommonIpAddress;
        }

        public static IPAddress GetSubnetMask(IPAddress address)
        {
            foreach (NetworkInterface adapter in NetworkInterface.GetAllNetworkInterfaces())
            {
                foreach (UnicastIPAddressInformation unicastIPAddressInformation in adapter.GetIPProperties().UnicastAddresses)
                {
                    if (unicastIPAddressInformation.Address.AddressFamily == AddressFamily.InterNetwork)
                    {
                        if (address.Equals(unicastIPAddressInformation.Address))
                        {
                            return unicastIPAddressInformation.IPv4Mask;
                        }
                    }
                }
            }
            return IPAddress.Parse("255.255.255.0");
        }
        /// <summary>
        /// Check whether both(firstIP and secondIP) ip belong to same network or not
        /// </summary>
        /// <param name="firstIP">string firstIP.</param>
        /// <param name="subNet">string subNet.</param>
        /// <param name="secondIP">string secondIP</param>
        /// <return> true if both are from same network, otherwise false</return>
        public static bool CheckWhetherInSameNetwork(string firstIP, string subNet, string secondIP)
        {
            uint subnetmaskInInt = ConvertIPToUint(subNet);//Converting subnet to unsigned int
            uint firstIPInInt = ConvertIPToUint(firstIP);// converting firstip to unsigned int
            uint secondIPInInt = ConvertIPToUint(secondIP);// converting secondip to unsigned int
            uint networkPortionofFirstIP = firstIPInInt & subnetmaskInInt;//network portion of firstip by performing AND operation between firstip and subnet
            uint networkPortionofSecondIP = secondIPInInt & subnetmaskInInt;
            if (networkPortionofFirstIP == networkPortionofSecondIP)
                return true;
            else
                return false;
        }


        /// <summary>
        /// Convert ip address to unsigned integer
        /// </summary>
        /// <param name="ipAddress">string ipaddress</param>
        /// <returns> ip address in unsigned form</returns>
        public static uint ConvertIPToUint(string ipAddress)
        {
            System.Net.IPAddress iPAddress = System.Net.IPAddress.Parse(ipAddress);// striing to ip address
            byte[] byteIP = iPAddress.GetAddressBytes();//ip address to byte
            uint ipInUint = (uint)byteIP[3] << 24;
            ipInUint += (uint)byteIP[2] << 16;
            ipInUint += (uint)byteIP[1] << 8;
            ipInUint += (uint)byteIP[0];
            return ipInUint;

        }

        public static long GetUnixLongTime(DateTime strTime)
        {
            DateTime startTime = TimeZone.CurrentTimeZone.ToLocalTime(new System.DateTime(1970, 1, 1)); 
            return (long)(strTime - startTime).TotalSeconds; 
        }
        public static DateTime GetDateTimeFromLong(long time)
        {
            DateTime startDateTime = TimeZone.CurrentTimeZone.ToLocalTime(new System.DateTime(1970, 1, 1)); 
            return startDateTime.AddSeconds(time);
        }
        
        /// <summary>
        ///  Takes a Unix time in seconds and converts it to a DateTimeOffset object in the local time zone.
        ///  Accounts for daylight saving time.
        /// </summary>
        /// <param name="gmtUnixTime"></param>
        /// <returns></returns>
        public static long ConvertGMTToLocalUnixTime(long gmtUnixTime)
        {
            // Convert GMT Unix time to DateTimeOffset
            DateTimeOffset gmtDateTimeOffset = DateTimeOffset.FromUnixTimeSeconds(gmtUnixTime);

            // Get local time zone
            TimeZoneInfo localTimeZone = TimeZoneInfo.Local;

            // Convert to local time
            DateTimeOffset localDateTimeOffset;

            if (localTimeZone.IsDaylightSavingTime(gmtDateTimeOffset))
            {
                localDateTimeOffset = gmtDateTimeOffset.ToOffset(localTimeZone.GetUtcOffset(gmtDateTimeOffset)).AddHours(1);
            }
            else
            {
                localDateTimeOffset = gmtDateTimeOffset.ToLocalTime();
            }

            // Get local Unix time in seconds
            return localDateTimeOffset.ToUnixTimeSeconds();
        }
    }
}
