//
// Copyright (c) 2016 Repetti Adriano.
//
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Security;
using System.Threading;
using System.Threading.Tasks;
using Newtonsoft.Json;
using Radev.Licensing;

namespace PlayerServer.Licensing
{

   

    // Do not put this in a shared assembly.
    // For improved security this source file must be included in each
    // project you need to check for license (added as reference).
    //
    // Feel free to change this file to exactly match your "public" interface
    // for license management: you don't even need to expose License object
    // directly and you may forward calls.
    // This is the place to change if you want to store license in different
    // folders (see GetLicenseFilePath) or elsewhere (see ReadCurrentLicense).
    // Note that if you want you can also connect to a remove server (both
    // Internet or Intranet) to ask for license, license itself doesn't need
    // to be stored locally (it's a plain base64 encoded text token).
    //
    // Be aware malicious plug-ins may alter license information (also) via Reflection!
    public static class LicenseManager
    {


        #if (LINUX)
            const string LIBRARYNETSDK = "liblicenser.so";
        #else
           const string LIBRARYNETSDK = "liblicenser.dll";
        #endif


        [DllImport(LIBRARYNETSDK, CharSet = CharSet.Ansi, CallingConvention = CallingConvention.StdCall)]
        public static extern bool generateMachineKey([MarshalAs(UnmanagedType.LPStr)] string file_path, out IntPtr machine_key, out IntPtr error);

        [DllImport(LIBRARYNETSDK, CharSet = CharSet.Ansi, CallingConvention = CallingConvention.Cdecl)]
        public static extern bool activateLicense([MarshalAs(UnmanagedType.LPStr)] string licData, out IntPtr error);

        [DllImport(LIBRARYNETSDK, CharSet = CharSet.Ansi, CallingConvention = CallingConvention.Cdecl)]
        public static extern bool isLicenseValid(out IntPtr error, bool onlyCheckTime);

        [DllImport(LIBRARYNETSDK, CharSet = CharSet.Ansi, CallingConvention = CallingConvention.Cdecl)]
        public static extern bool getLicenseDetails(out IntPtr details);


        static LicenseManager()
        {

            var timer = new System.Threading.Timer(
                e => CheckLicenceValidation(),
                null,
                TimeSpan.Zero,
                TimeSpan.FromHours(1));


        }
        public static License License
        {
            [SecurityCritical, MethodImpl(MethodImplOptions.AggressiveInlining)]
            get
            {

                return GetLicenceModel();
            }
        }


        private static void CheckLicenceValidation()
        {

            LicenseInfo.IsValid = isLicenceActivated();
        }
        public static License GetLicenceModel()
        {
            License model = null;
            if (isLicenceActivated())
            {
                IntPtr details_ptr;
                bool isgetlicensedetails = LicenseManager.getLicenseDetails(out details_ptr);
                if (isgetlicensedetails)
                {
                    string details =  System.Runtime.InteropServices.Marshal.PtrToStringAnsi(details_ptr);
                    if (!string.IsNullOrEmpty(details))
                    {
                        License[] models = JsonConvert.DeserializeObject<License[]>(details);
                        if(models != null && models.Length > 0)
                        {
                            model = models[0];
                        }

                    }
                }
            }
            return model;
        }
        public static void LoadLicense()
        {
            if (isLicenceActivated())
            {
                CheckLicensePeriodically();
            }
        }
        private static async void CheckLicensePeriodically()
        {
            while (isLicenceActivated())
            {
                LicenseInfo.IsValid = true;
                await Task.Delay(24 * 60 * 60 * 1000);
                if (!isLicenceActivated())
                {
                    LicenseInfo.IsValid = false;
                }
            }
        }
        public static bool UpdateLicense(string LicenseKey)
        {
            IntPtr intPtrError;
            bool islicenceActivated = LicenseManager.activateLicense(LicenseKey, out intPtrError);
            if (islicenceActivated)
            {
                 LoadLicense();
            }

            return islicenceActivated;
        }


        private static bool isLicenceActivated()
        {
            IntPtr intPtrError;
            bool islicencevalid = isLicenseValid(out intPtrError, true);
            return islicencevalid;
        }














    }
}