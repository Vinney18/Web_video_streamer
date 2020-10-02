using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace PlayerServer.requestModel
{
    public class NetworkSettingReq
    {
        public string ReturnIp { get; set; }
        public bool IsVPN { get; set; }
    }
}
