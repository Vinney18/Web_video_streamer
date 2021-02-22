using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using Microsoft.AspNetCore.Mvc;
using PlayerServer.Licensing;
using PlayerServer.requestModel;
using Radev.Licensing;
using Radev.Licensing.Client;
using Newtonsoft.Json;

// For more information on enabling MVC for empty projects, visit https://go.microsoft.com/fwlink/?LinkID=397860

namespace PlayerServer.Controllers
{
    public class LicenseController : Controller
    {
        private static readonly string machine_key_file_path = "machine_keyy.txt";

        public LicenseController()
        {

        }

        [HttpGet]
        [Route("~/api/GenerateDeviceKey")]
        public IActionResult GenerateDeviceKey()
        {

            try
            {
                IntPtr intPtr_machinekey, intPtrError;
                bool isDeviceKeyGenrated = LicenseManager.generateMachineKey(machine_key_file_path, out intPtr_machinekey, out intPtrError);
                string errordetails = System.Runtime.InteropServices.Marshal.PtrToStringAnsi(intPtrError);

                if (isDeviceKeyGenrated)
                {
                    if (System.IO.File.Exists(Path.Combine(Directory.GetCurrentDirectory(), machine_key_file_path)))
                    {
                        string machinekey = System.IO.File.ReadAllText(Path.Combine(Directory.GetCurrentDirectory(), machine_key_file_path));
                        return Ok(machinekey);
                    }
                    else
                    {
                        return BadRequest("Machine key file not found, error: " + errordetails);

                    }
                }
                else
                {
                    return BadRequest("Cannot Generate Device Key: " + errordetails);
                }

            }
            catch (Exception ex)
            {
                return BadRequest(ex.Message);
            }

        }

        [HttpPost]
        [Route("~/api/License")]
        public async Task<IActionResult> ActivateLicense([FromBody] ActivateLicenseReq req)
        {
            try
            {
                if (req == null)
                {
                    return BadRequest();
                }
                string errorDetails = string.Empty;
                bool islicenceActivated = LicenseManager.UpdateLicense(req.license_data, out errorDetails);
                if (islicenceActivated)
                {
                    return Ok();

                }
                else
                {
                    return BadRequest("Not able to activate" + errorDetails);

                }
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
