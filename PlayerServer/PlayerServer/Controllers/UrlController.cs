using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Microsoft.AspNetCore.Mvc;

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
                return Ok(url);
            }
            catch (Exception ex)
            {
                return BadRequest();
            }
        }
    }
}
