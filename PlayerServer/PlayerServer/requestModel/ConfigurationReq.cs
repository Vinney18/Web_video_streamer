using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace PlayerServer.requestModel
{
    public class ConfigurationReq
    {
        public string ServerIp { get; set; }
        public int port { get; set; }
        public string token { get; set; }
    }
}
