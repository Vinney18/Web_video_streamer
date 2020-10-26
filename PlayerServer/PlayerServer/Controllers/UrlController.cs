using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Microsoft.AspNetCore.Mvc;
using Newtonsoft.Json;
using PlayerServer.services;

namespace PlayerServer.Controllers
{
    public class UrlController : Controller
    {
        private PlayerServer.services.PlayerUrlService _playerUrlService;
        public UrlController(PlayerServer.services.PlayerUrlService playerUrlService)
        {
            _playerUrlService = playerUrlService;
        }

        [HttpGet]
        [Route("~/url/GetLiveUrl")]
        public async Task<IActionResult> GetLiveUrlAsync(int cameraId, int streamType)
        {
            try
            {
                var url = await _playerUrlService.GetLiveUrlAsync(cameraId, streamType);
                Console.WriteLine(url);
                if (url.Contains("127.0.0.1") || url.Contains("localhost"))
                {
                    if (ServerDetails.isVPN)
                    {
                        url = url.Replace("127.0.0.1", ServerDetails.ReturnIp);
                        url = url.Replace("localhost", ServerDetails.ReturnIp);
                    }
                    else
                    {
                        string clientIp = HttpContext.Connection.RemoteIpAddress.ToString();
                        var hostIP = CommonMethods.GetBindIP(clientIp);
                        url = url.Replace("127.0.0.1", hostIP);
                        url = url.Replace("localhost", hostIP);
                    }
                }

                return Ok(url);
            }
            catch (Exception ex)
            {
                return BadRequest();
            }
        }

        [HttpGet]
        [Route("~/url/GetPlaybackUrl")]
        public async Task<IActionResult> GetPlaybackUrl(int cameraId, long time)
        {
            try
            {
                var url = await _playerUrlService.GetPlaybackUrl(cameraId, time);
                var obj = JsonConvert.DeserializeObject<dynamic>(url);
                string currentUrl = "";
                if (url.Contains("GetEventPlaybackUrlResult"))
                {
                    currentUrl = obj.GetEventPlaybackUrlResult.ToString();
                }
                else
                {
                    currentUrl = obj.getEventPlaybackUrlResult.ToString();
                }
               
                if (currentUrl.Contains("127.0.0.1") || currentUrl.Contains("localhost"))
                {
                    if (ServerDetails.isVPN)
                    {
                        currentUrl = currentUrl.Replace("127.0.0.1", ServerDetails.ReturnIp);
                        currentUrl = currentUrl.Replace("localhost", ServerDetails.ReturnIp);
                    }
                    else
                    {
                        string clientIp = HttpContext.Connection.RemoteIpAddress.ToString();
                        var hostIP = CommonMethods.GetBindIP(clientIp);
                        currentUrl = currentUrl.Replace("127.0.0.1", hostIP);
                        currentUrl = currentUrl.Replace("localhost", hostIP);
                    }

                }
                obj.GetEventPlaybackUrlResult = currentUrl;
                url = JsonConvert.SerializeObject(obj);

                return Ok(url);
            }
            catch (Exception ex)
            {
                return BadRequest();
            }
        }
    }
}
