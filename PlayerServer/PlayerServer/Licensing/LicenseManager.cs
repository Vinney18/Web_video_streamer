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
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Security;
using System.Threading;
using System.Threading.Tasks;
using Radev.Licensing;
using Radev.Licensing.Client;

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
        static LicenseManager()
        {
            _license = new ThreadLocal<License>(ReadCurrentLicense);
        }

        public static bool IsLicenseValid
        {
            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            get
            {
                return License != null;
            }
        }

        public static License License
        {
            [SecurityCritical, MethodImpl(MethodImplOptions.AggressiveInlining)]
            get
            {
                if (_license.IsValueCreated)
                    return _license.Value;

                var license = _license.Value;
                if (license != null && !license.IsValid())
                {
                    // Here, if license is not valid because expired (check License.IsExpired())
                    // you may automatically renew it from a remote server. Setting license
                    // validity to a short timespan will allow you to disable licenses
                    // (or to blacklist them) centralizing this logic.
                    // Remote server may simply be a service "ping" with license ID,
                    // in this way you may detect virtual machines (and blacklist such licenses)
                    // or permanently/temporary disable a license even if its validity is longer.
                    _license.Value = null;
                    LicenseInfo.IsValid = false;
                }

                return _license.Value;
            }
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        internal static int? GetFeature(string feature)
        {
            if (!IsLicenseValid)
                return null;

            return License.ReadFeature(feature);
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        internal static bool IsFeatureAvailable(string feature)
        {
            return (GetFeature(feature) ?? 0) != 0;
        }

        internal static string GetLicenseFilePath()
        {
            // 1st attempt is license file located in application folder
            var executablePath = Assemblies.ResolveEntryPointPath();
            if (String.IsNullOrWhiteSpace(executablePath))
                return null;

            //var path = Path.Combine(Path.GetDirectoryName(executablePath), LicenseFileName);
            var path = Path.Combine(Environment.CurrentDirectory, "data", LicenseFileName);
            if (File.Exists(path))
                return path;

            try
            {
                // 2nd attempt is license file located in shared documents folder
                return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonDocuments),
                    LicenseFileName);
            }
            catch (PlatformNotSupportedException)
            {
                // 3rd attempt...something weird, I can't find SpecialFolder.CommonDocuments
                return Path.Combine(Environment.CurrentDirectory, LicenseFileName);
            }
        }

        // This is useful, primary for testing purposes because we're testing
        // a (sigh) static class. If you refactor to a singleton (or you create an instance
        // each time) you can drop this method (and rewrite tests).
        internal static void Renew()
        {
            _license = new ThreadLocal<License>(ReadCurrentLicense);
        }

        private const string LicenseFileName = "Product.lic";

        private static ThreadLocal<License> _license;

        private static License ReadCurrentLicense()
        {
            var path = GetLicenseFilePath();
            if (File.Exists(path))
            {
                try
                {
                    if (File.Exists(Path.Combine(Directory.GetCurrentDirectory(), "public_key.xml")))
                    {
                        string publicKey = File.ReadAllText(Path.Combine(Directory.GetCurrentDirectory(), "public_key.xml"));

                        var license = LicenseReader.FromFile(path, publicKey);
                        if (license == null || !license.IsValid())
                        {
                            LicenseInfo.IsValid = false;
                        }
                        else
                        {
                            LicenseInfo.IsValid = true;
                        }
                        return license;
                    }
                    else
                    {
                        LicenseInfo.IsValid = false;
                        return null;
                    }
                }
                catch (Exception ex)
                {
                    LicenseInfo.IsValid = false;
                    return null;
                }
            }

            LicenseInfo.IsValid = false;
            return null;
        }

        public static void LoadLicense()
        {
            foreach (var prop in typeof(LicenseInfo).GetProperties())
            {
                if (prop.Name != "IsValid")
                {
                    int value = GetFeature(prop.Name).GetValueOrDefault();

                    //switch (prop.Name)
                    //{
                    //    case "Resource_Limit":
                    //        LicenseInfo.Resource_Limit = value;
                    //        break;
                    //    case "ResourceType_Limit":
                    //        LicenseInfo.ResourceType_Limit = value;
                    //        break;
                    //    case "CustomEvent_Limit":
                    //        LicenseInfo.CustomEvent_Limit = value;
                    //        break;
                    //    case "Individual_CustomEvent_Properties_Limit":
                    //        LicenseInfo.Individual_CustomEvent_Properties_Limit = value;
                    //        break;
                    //    case "Individual_ResourceType_Properties_Limit":
                    //        LicenseInfo.Individual_ResourceType_Properties_Limit = value;
                    //        break;
                    //    case "Dashboard_Limit":
                    //        LicenseInfo.Dashboard_Limit = value;
                    //        break;
                    //    case "Widget_Limit":
                    //        LicenseInfo.Widget_Limit = value;
                    //        break;
                    //    case "User_Limit":
                    //        LicenseInfo.User_Limit = value;
                    //        break;
                    //    case "UserType_Limit":
                    //        LicenseInfo.UserType_Limit = value;
                    //        break;
                    //    case "Sop_Limit":
                    //        LicenseInfo.Sop_Limit = value;
                    //        break;
                    //    case "Individual_SopNode_Limit":
                    //        LicenseInfo.Individual_SopNode_Limit = value;
                    //        break;
                    //    case "Total_CustomEvent_Properties_Limit":
                    //        LicenseInfo.Total_CustomEvent_Properties_Limit = value;
                    //        break;
                    //    case "Total_ResourceType_Properties_Limit":
                    //        LicenseInfo.Total_ResourceType_Properties_Limit = value;
                    //        break;
                    //    default:
                    //        break;
                    //}
                }
            }
            Licensing.LicenseInfo.IsValid = true;
            CheckLicensePeriodically();
        }

        public static License UpdateLicense(string LicenseKey)
        {
            if (File.Exists(Path.Combine(Directory.GetCurrentDirectory(), "public_key.xml")))
            {
                string publicKey = File.ReadAllText(Path.Combine(Directory.GetCurrentDirectory(), "public_key.xml"));
                var license = LicenseReader.FromString(LicenseKey, publicKey);

                if (license != null && license.IsValid())
                {
                    var path = Path.Combine(Directory.GetCurrentDirectory(), LicenseFileName);
                    if (!File.Exists(path))
                    {

                        var fs = File.Create(path);
                        fs.Close();
                    }
                    File.WriteAllText(path, LicenseKey);
                    LoadLicense();

                    return license;
                }
                else
                {
                    throw new ArgumentException("Invalid  License Key");
                }
            }
            else
            {
                throw new ArgumentException("Invalid  License Key");
            }
        }

        private static async void CheckLicensePeriodically()
        {
            while (LicenseManager.IsLicenseValid)
            {
                LicenseInfo.IsValid = true;
                await Task.Delay(24 * 60 * 60 * 1000);
                if (!LicenseManager.IsLicenseValid)
                {
                    LicenseInfo.IsValid = false;
                }
            }
        }

    }
}