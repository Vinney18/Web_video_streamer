using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Microsoft.AspNetCore.Mvc;
using PlayerServer.Licensing;
using PlayerServer.requestModel;
using Radev.Licensing;
using Radev.Licensing.Client;

// For more information on enabling MVC for empty projects, visit https://go.microsoft.com/fwlink/?LinkID=397860

namespace PlayerServer.Controllers
{
    public class LicenseController : Controller
    {
        public LicenseController()
        {

        }

        [HttpGet]
        [Route("~/api/GenerateDeviceKey")]
        public IActionResult GenerateDeviceKey()
        {

            try
            {
                if (System.IO.File.Exists(Path.Combine(Directory.GetCurrentDirectory(), "public_key.xml")))
                {
                    string publicKey = System.IO.File.ReadAllText(Path.Combine(Directory.GetCurrentDirectory(), "public_key.xml"));
                    return Ok(ContactWriter.ToString(ContactFactory.Create<Contact>(), publicKey));
                }
                else
                {
                    return BadRequest("Cannot Generate Device Key.");
                }
            }
            catch (Exception ex)
            {
                return BadRequest(ex.Message);
            }

        }

        [HttpPost]
        [Route("~/api/License")]
        public async Task<IActionResult> ActivateLicense([FromBody]ActivateLicenseReq req)
        {
            try
            {
                if(req == null)
                {
                    return BadRequest();
                }
                var license = LicenseManager.UpdateLicense(req.license_data);
                Licensing.LicenseInfo.IsValid = true;
                return Ok();
            }
            catch (Exception ex)
            {
                return BadRequest(ex.Message);
            }
        }

        [HttpGet]
        [Route("~/api/License")]
        public IActionResult GetLicenseDetails()
        {
            try
            {
                return Ok(LicenseManager.License);
            }
            catch (Exception ex)
            {
                return BadRequest(ex.Message);
            }
        }
    }
}
