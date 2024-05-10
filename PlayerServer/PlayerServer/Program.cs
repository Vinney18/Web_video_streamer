using System;
using System.Collections.Generic;
using System.IO;
using System.Net;
using Microsoft.AspNetCore;
using Microsoft.AspNetCore.Hosting;
using Microsoft.Extensions.Configuration;
using Newtonsoft.Json;

namespace PlayerServer
{
    public class Program
    {
        public static void Main(string[] args)
        {
            var config = new ConfigurationBuilder()
                .SetBasePath(Directory.GetCurrentDirectory())
                .AddJsonFile("appsettings.json", optional: false, reloadOnChange: true)
                .Build();

            var defaultPort = config.GetSection("DefaultPort").Value;
            args = new string[] { defaultPort };
            LoadConfig("config.json", (configObject) =>
            {
                ServerDetails.AttachedServerIp = Convert.ToString(configObject["ServerIp"]);
                ServerDetails.port = Convert.ToInt32(configObject["port"]);
                ServerDetails.token = Convert.ToString(configObject["token"]);
            }, new Dictionary<string, object> { { "ServerIp", "127.0.0.1" }, { "port", 8800 }, { "token", "" } });

            LoadConfig("networkSetting.json", (configObject) =>
            {
                ServerDetails.isVPN = Convert.ToBoolean(configObject["IsVPN"]);
                ServerDetails.ReturnIp = Convert.ToString(configObject["ReturnIp"]);
            }, new Dictionary<string, object> { { "IsVPN", false }, { "ReturnIp", "" } });

            Licensing.LicenseManager.LoadLicense();
            CreateWebHostBuilder(args).Build().Run();
        }

        private static void LoadConfig(string fileName, Action<Dictionary<string, object>> action, Dictionary<string, object> defaultValues)
        {
            if (File.Exists(Path.Combine(Directory.GetCurrentDirectory(), fileName)))
            {
                var text = File.ReadAllText(Path.Combine(Directory.GetCurrentDirectory(), fileName));
                if (!string.IsNullOrEmpty(text))
                {
                    var configObject = JsonConvert.DeserializeObject<Dictionary<string, object>>(text);
                    if (configObject != null)
                    {
                        action(configObject);
                    }
                }
                else
                {
                    Console.WriteLine($"{fileName} is empty");
                    File.WriteAllText(Path.Combine(Directory.GetCurrentDirectory(), fileName), JsonConvert.SerializeObject(defaultValues));
                }
            }
            else
            {
                Console.WriteLine($"{fileName} does not exist");
                // create a new file with default json
                File.WriteAllText(Path.Combine(Directory.GetCurrentDirectory(), fileName), JsonConvert.SerializeObject(defaultValues));
            }
        }

        private static IWebHostBuilder CreateWebHostBuilder(string[] args) =>
            WebHost.CreateDefaultBuilder(args)
                .UseStartup<Startup>()
            .UseKestrel(options =>
            {
                options.Listen(IPAddress.Any, Convert.ToInt32(args[0]));
            });
    }
}