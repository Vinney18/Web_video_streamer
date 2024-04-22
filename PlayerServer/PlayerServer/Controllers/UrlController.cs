using System;
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
        public async Task<IActionResult> GetLiveUrlAsync(string cameraId, int streamType, string analyticType)
        {
            Console.WriteLine("Route -> ~/url/GetLiveUrl");
            try
            {
                var url = await _playerUrlService.GetLiveUrlAsync(cameraId, streamType, analyticType);
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
                if(ex.Message.Contains("No connection could be made because the target machine actively refused it."))
                {
                    return Ok("URL_Server_Not_Connected");
                }
                else
                {
                    return BadRequest();
                }
            }
        }

        [HttpGet]
        [Route("~/url/GetPlaybackUrl")]
        public async Task<IActionResult> GetPlaybackUrl(string cameraId, long time, bool streamviaapache)
        {
            Console.WriteLine("Route -> ~/url/GetPlaybackUrl");
            try
            {
                var url = await _playerUrlService.GetPlaybackUrl(cameraId, time, streamviaapache);
                var obj = JsonConvert.DeserializeObject<dynamic>(url);
                string currentUrl = "";
                if (obj != null)
                {
                    if (url.Contains("GetEventPlaybackUrlResult"))
                    {
                        currentUrl = obj.GetEventPlaybackUrlResult.ToString();
                    }
                    else
                    {
                        currentUrl = obj.getEventPlaybackUrlResult.ToString();
                    } 
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
                if(obj != null)
                {
                    obj.GetEventPlaybackUrlResult = currentUrl;
                }
                url = JsonConvert.SerializeObject(obj);
                Console.WriteLine(url);

                return Ok(url);
            }
            catch (Exception ex)
            {
                // on for debugging
                // TODO: make debug flag
                // var url = @"{GetEventPlaybackUrlResult: 'D:\happytime-onvif-server\happytime-rtsp-server\sgv1.ts', Seek_Time_InSeconds: 0}";

                // var obj = new { GetEventPlaybackUrlResult = @"http://192.168.1.9:9000//sgv1.ts", Seek_Time_InSeconds = 0 };
                // url = JsonConvert.SerializeObject(obj);
                // return Ok(url);
                if (ex.Message.Contains("No connection could be made because the target machine actively refused it."))
                {
                    return Ok("URL_Server_Not_Connected");
                }
                else
                {
                    return BadRequest();
                }
            }
        }

        [HttpGet]
        [Route("~/url/GetExportUrl")]
        public async Task<IActionResult> GetExportUrl(string cameraId, long startTime, long endTime)
        {
            Console.WriteLine("Route -> ~/url/GetExportUrl");
            try
            {
                var url = await _playerUrlService.GetExportUrl(cameraId, startTime, endTime);
                var obj = JsonConvert.DeserializeObject<dynamic>(url);
                string currentUrl = "";
                if (obj != null)
                {
                    //"ErrorMessage" is present and is not null
                    if (url.Contains("IsSuccess") && obj.IsSuccess == false)
                    {
                        return StatusCode(500, obj.ErrorMessage.ToString());
                    }
                    
                    if (url.Contains("ExportedVideoUrl"))
                    {
                        currentUrl = obj.ExportedVideoUrl.ToString();
                    }
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
                if (obj != null)
                {
                    obj.ExportedVideoUrl = currentUrl;
                }
                url = JsonConvert.SerializeObject(obj);
                Console.WriteLine(url);

                return Ok(url);
            }
            catch (Exception ex)
            {
                // var url = @"{GetEventPlaybackUrlResult: 'D:\happytime-onvif-server\happytime-rtsp-server\sgv1.ts', Seek_Time_InSeconds: 0}";

                // var obj = new { ExportedVideoUrl = @"http://192.168.1.9:9000//face_det.ts", Seek_Time_InSeconds = 0 };
                // url = JsonConvert.SerializeObject(obj);
                // return Ok(url);
                if (ex.Message.Contains("No connection could be made because the target machine actively refused it."))
                {
                    return Ok("URL_Server_Not_Connected");
                }
                else
                {
                    return BadRequest();
                }
            }
        }

        [HttpGet]
        [Route("~/url/PauseVideo")]
        public async Task<IActionResult> PauseVideo(int cameraId, long time, int sessionId)
        {
            Console.WriteLine("Route -> ~/url/PauseVideo");
            try
            {
                var url = await _playerUrlService.PauseVideo(cameraId, time, sessionId);
                return Ok(url);
            }
            catch (Exception ex)
            {
                return BadRequest();
            }
        }

        [HttpGet]
        [Route("~/url/ResumeVideo")]
        public async Task<IActionResult> ResumeVideo(int cameraId, long time, int sessionId)
        {
            try
            {
                var url = await _playerUrlService.ResumeVideo(cameraId, time, sessionId);
                return Ok(url);
            }
            catch (Exception ex)
            {
                return BadRequest();
            }
        }

        [HttpGet]
        [Route("~/url/SeekVideo")]
        public async Task<IActionResult> SeekVideo(int cameraId, long time, long seekTime,int sessionId)
        {
            try
            {
                var url = await _playerUrlService.SeekVideo(cameraId, time, seekTime, sessionId);               
                return Ok(url);
            }
            catch (Exception ex)
            {
                return BadRequest();
            }
        }
    }
}
