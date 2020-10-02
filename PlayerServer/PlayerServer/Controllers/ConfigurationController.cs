using Microsoft.AspNetCore.Mvc;
using Newtonsoft.Json;
using PlayerServer.requestModel;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace PlayerServer.Controllers
{
    public class ConfigurationController : Controller
    {
        public ConfigurationController()
        {

        }

        [HttpGet]
        [Route("~/api/Configuration")]
        public IActionResult GetPlayerConfiguration()
        {
            return Ok(new { ServerIp = ServerDetails.AttachedServerIp, Port = ServerDetails.port, Token = ServerDetails.token, isLicenseValid = ServerDetails.isLicenseValid });
        }

        [HttpPost]
        [Route("~/api/Configuration")]
        public IActionResult UpdateConfiguration([FromBody]ConfigurationReq configurationReq)
        {
            if (!ModelState.IsValid)
            {
                return BadRequest();
            }
            try
            {
                ServerDetails.AttachedServerIp = configurationReq.ServerIp;
                ServerDetails.port = configurationReq.port;
                ServerDetails.token = configurationReq.token;
                string path = Path.Combine(Directory.GetCurrentDirectory(), "config.json");
                System.IO.File.WriteAllBytes(path, Encoding.ASCII.GetBytes(JsonConvert.SerializeObject(configurationReq)));
            }
            catch (Exception ex)
            {
                return BadRequest("Some Problem occured");
            }
            return Ok();
        }

        [HttpPost]
        [Route("~/api/NetworkSetting")]
        public IActionResult UpdateNetworkSetting([FromBody]NetworkSettingReq configurationReq)
        {
            if (!ModelState.IsValid)
            {
                return BadRequest();
            }
            try
            {
                ServerDetails.isVPN = configurationReq.IsVPN;
                ServerDetails.ReturnIp = configurationReq.ReturnIp;
                string path = Path.Combine(Directory.GetCurrentDirectory(), "networkSetting.json");
                System.IO.File.WriteAllBytes(path, Encoding.ASCII.GetBytes(JsonConvert.SerializeObject(configurationReq)));
            }
            catch (Exception ex)
            {
                return BadRequest("Some Problem occured");
            }
            return Ok();
        }

        [HttpGet]
        [Route("~/api/NetworkSetting")]
        public IActionResult GetNetworkSetting()
        {
            return Ok(new { isVPN = ServerDetails.isVPN, returnIp = ServerDetails.ReturnIp });
        }
    }

}
