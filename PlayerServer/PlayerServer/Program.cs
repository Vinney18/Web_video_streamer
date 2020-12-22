using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Threading.Tasks;
using Microsoft.AspNetCore;
using Microsoft.AspNetCore.Hosting;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Newtonsoft.Json;

namespace PlayerServer
{
    public class Program
    {
        public static void Main(string[] args)
        {

            if (File.Exists(Path.Combine(Directory.GetCurrentDirectory(), "config.json")))
            {
                Console.WriteLine("json file exixts");

                var text = File.ReadAllText(Path.Combine(Directory.GetCurrentDirectory(), "config.json"));
                if (!string.IsNullOrEmpty(text))
                {
                    var configObject = JsonConvert.DeserializeObject<Dictionary<string, object>>(text);
                    if (configObject != null)
                    {
                        if (configObject.ContainsKey("ServerIp"))
                        {
                            ServerDetails.AttachedServerIp = Convert.ToString(configObject["ServerIp"]);
                        }
                        if (configObject.ContainsKey("port"))
                        {
                            ServerDetails.port = Convert.ToInt32(configObject["port"]);
                        }
                        if (configObject.ContainsKey("token"))
                        {
                            ServerDetails.token = Convert.ToString(configObject["token"]);
                        }
                        if (configObject.ContainsKey("PlayerServerPort"))
                        {
                            int port = Convert.ToInt32(configObject["PlayerServerPort"]);
                            args = new List<string>() { port.ToString() }.ToArray();
                        }
                        else
                        {
                            args = new List<string>() { "8890" }.ToArray();
                        }
                    }
                    else
                    {
                        args = new List<string>() { "8890" }.ToArray();
                    }
                }
                else
                {
                    args = new List<string>() { "8890" }.ToArray();
                }
            }
            else
            {
                Console.WriteLine("json file does not exixt");
                Dictionary<string, object> serverDetails = new Dictionary<string, object>()
                {
                    {"ServerIp",ServerDetails.AttachedServerIp},
                    {"port",ServerDetails.port},
                    {"token",ServerDetails.token}
                };
                File.WriteAllText(Path.Combine(Directory.GetCurrentDirectory(), "config.json"), JsonConvert.SerializeObject(serverDetails));
                args = new List<string>() { "8890" }.ToArray();
            }

            if (File.Exists(Path.Combine(Directory.GetCurrentDirectory(), "networkSetting.json")))
            {
                var text = File.ReadAllText(Path.Combine(Directory.GetCurrentDirectory(), "networkSetting.json"));
                if (!string.IsNullOrEmpty(text))
                {
                    var configObject = JsonConvert.DeserializeObject<Dictionary<string, object>>(text);
                    if (configObject != null)
                    {
                        if (configObject.ContainsKey("IsVPN"))
                        {
                            ServerDetails.isVPN = Convert.ToBoolean(configObject["IsVPN"]);
                        }
                        if (configObject.ContainsKey("ReturnIp"))
                        {
                            ServerDetails.ReturnIp = Convert.ToString(configObject["ReturnIp"]);
                        }
                    }
                }
            }
            Licensing.LicenseManager.LoadLicense();
            CreateWebHostBuilder(args).Build().Run();
        }

        public static IWebHostBuilder CreateWebHostBuilder(string[] args) =>
            WebHost.CreateDefaultBuilder(args)
                .UseStartup<Startup>()
            .UseKestrel(options =>
            {
                options.Listen(IPAddress.Any, Convert.ToInt32(args[0]));
            });
    }
}
