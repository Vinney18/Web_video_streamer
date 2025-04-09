using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Security.Cryptography;

using Microsoft.Win32;
using System.IO;
using System.Threading.Tasks;

namespace PlayerServer.Licensing
{
    public class Licence
    {
        public static string globalMessage;
        private static string globalPath;
        private string InstallDateEncryptionkey = "E546C8DF278CD5931069B522E695D4F2";
        private string useDateEncryptionKey = "E546C8DF278CD5931069B522E695D4F3";
        private string noOfDaysEncryptedKey = "E546C8DF278CD5931069B522E695D4F4";




        private DateTime getUseDate()
        {
            RegistryKey regkey = Registry.CurrentUser;
            regkey = regkey.CreateSubKey(globalPath); //path
            string encryptedUseDate = (string)regkey.GetValue("Use");
            var decryptedUseDate = DecryptString(encryptedUseDate, useDateEncryptionKey);
            DateTime useDate = DateTime.ParseExact(decryptedUseDate, "dd/MM/yyyy", System.Globalization.CultureInfo.InvariantCulture);
            return useDate;
        }

        public bool checkRegistry()
        {
            RegistryKey regkey = Registry.CurrentUser;
            regkey = regkey.CreateSubKey(globalPath);
            string encryptedInstallDate = (string)regkey.GetValue("Install");
            if (encryptedInstallDate == null)
                return false;
            else
                return true;
        }

        private DateTime getInstallDate()
        {
            RegistryKey regkey = Registry.CurrentUser;
            regkey = regkey.CreateSubKey(globalPath); //path
            string encryptedInstallDate = (string)regkey.GetValue("Install");
            var decrytedinstallDate = DecryptString(encryptedInstallDate, InstallDateEncryptionkey);
            DateTime installDate = DateTime.ParseExact(decrytedinstallDate, "dd/MM/yyyy", System.Globalization.CultureInfo.InvariantCulture);
            return installDate;
        }

        private void setInstallDate(DateTime date)
        {
            RegistryKey regkey = Registry.CurrentUser;
            regkey = regkey.CreateSubKey(globalPath); //path
            string onlyDate = date.ToString("dd/MM/yyyy"); // get only date not time
            var encrypted = EncryptString(onlyDate, InstallDateEncryptionkey);
            regkey.SetValue("Install", encrypted); //Value Name,Value Data
        }

        private void setUseDate(DateTime date)
        {
            RegistryKey regkey = Registry.CurrentUser;
            regkey = regkey.CreateSubKey(globalPath); //path
            string onlyDate = date.ToString("dd/MM/yyyy"); // get only date not time
            var useDateEncryption = EncryptString(onlyDate, useDateEncryptionKey);
            regkey.SetValue("Use", useDateEncryption); //Value Name,Value Data

        }

        private void setNoOfDaysLicence(int noOfDays)
        {
            RegistryKey regkey = Registry.CurrentUser;
            regkey = regkey.CreateSubKey(globalPath); //path
            var noOfDaysEncrypt = EncryptString(noOfDays.ToString(), noOfDaysEncryptedKey);
            regkey.SetValue("Days", noOfDaysEncrypt); //Value Name,Value Data
        }

        private int getNoOfDaysLicence()
        {
            RegistryKey regkey = Registry.CurrentUser;
            regkey = regkey.CreateSubKey(globalPath); //path
            string encryptednoOfDays = (string)regkey.GetValue("Days");
            var decrytedinstallDate = DecryptString(encryptednoOfDays, noOfDaysEncryptedKey);
            int x = 0;
            Int32.TryParse(decrytedinstallDate, out x);
            return x;
        }


        private void firstTime()
        {
            DateTime dt = DateTime.Now;
            setInstallDate(dt);
            setUseDate(dt);
            setNoOfDaysLicence(15);
            globalMessage = "15";
        }

        private String checkfirstDate()
        {
            if (checkRegistry())
            {
                DateTime installDate = getInstallDate();
                return installDate.ToString();
            }
            else
                return "First";
        }


        private String dayDifPutPresent()
        {
            // get present date from system
            DateTime dt = DateTime.Now;
            string today = dt.ToString("dd/MM/yyyy");
            DateTime presentDate = DateTime.ParseExact(today, "dd/MM/yyyy", System.Globalization.CultureInfo.InvariantCulture);

            // get instalation date
            DateTime installationDate = getInstallDate();
            DateTime lastUse = getUseDate();


            TimeSpan diff = lastUse.Subtract(installationDate); //first.Subtract(second);
            int totaldays = (int)diff.TotalDays;

            // special check if user chenge date in system


            TimeSpan diff1 = presentDate.Subtract(lastUse); //first.Subtract(second);
            int useBetween = (int)diff1.TotalDays;
            int noOfDaysLicence = getNoOfDaysLicence();
            // put next use day in registry




            if (totaldays < 0)
            {
                globalMessage = "Error";
                return globalMessage; // if user change date in system like date set before installation
            }
            else if (totaldays >= 0 && totaldays <= noOfDaysLicence)
            {
                globalMessage = Convert.ToString(noOfDaysLicence - totaldays); //how many days remaining
                return globalMessage;
            }
            else
            {
                globalMessage = "Expired";
                return globalMessage; //Expired
            }



        }




        public string Algorithm(String pass)
        {

            globalPath = pass;
            //test1();
            //test();
            string message = null;

            string chinstall = checkfirstDate();
            if (chinstall == "First")
            {
                firstTime();// installation date
                return message;
            }
            else
            {
                try
                {
                    string status = dayDifPutPresent();
                    if (status == "Error")
                    {
                        message = "Error";
                        return message;
                    }
                    else if (status == "Expired")
                    {
                        message = "Expired";
                        return message;
                    }
                    else // execute with how many day remaining
                    {
                        message = "You are using trial Pack, you have " + status + " days left to Activate! Would you Like to Activate it now!";
                        return message;
                    }
                }
                catch (Exception ex)
                {
                    message = "Error";
                    globalMessage = "Error";
                    return message;
                }
            }

        }


        public void updateTimeInRegistry()
        {
            DateTime useDate = getUseDate();
            DateTime dt_returndate = useDate.AddDays(1);
            setUseDate(dt_returndate);
            dayDifPutPresent();
        }



        public string EncryptString(string text, string keyString)
        {
            try
            {
                var key = Encoding.UTF8.GetBytes(keyString);

                using (var aesAlg = Aes.Create())
                {
                    using (var encryptor = aesAlg.CreateEncryptor(key, aesAlg.IV))
                    {
                        using (var msEncrypt = new MemoryStream())
                        {
                            using (var csEncrypt = new CryptoStream(msEncrypt, encryptor, CryptoStreamMode.Write))
                            using (var swEncrypt = new StreamWriter(csEncrypt))
                            {
                                swEncrypt.Write(text);
                            }

                            var iv = aesAlg.IV;

                            var decryptedContent = msEncrypt.ToArray();

                            var result = new byte[iv.Length + decryptedContent.Length];

                            Buffer.BlockCopy(iv, 0, result, 0, iv.Length);
                            Buffer.BlockCopy(decryptedContent, 0, result, iv.Length, decryptedContent.Length);

                            return Convert.ToBase64String(result);
                        }
                    }
                }
            }
            catch (Exception ex) { return null; }
        }

        public string DecryptString(string cipherText, string keyString)
        {
            try
            {
                var fullCipher = Convert.FromBase64String(cipherText);

                var iv = new byte[16];
                var cipher = new byte[16];

                Buffer.BlockCopy(fullCipher, 0, iv, 0, iv.Length);
                Buffer.BlockCopy(fullCipher, iv.Length, cipher, 0, iv.Length);
                var key = Encoding.UTF8.GetBytes(keyString);

                using (var aesAlg = Aes.Create())
                {
                    using (var decryptor = aesAlg.CreateDecryptor(key, iv))
                    {
                        string result;
                        using (var msDecrypt = new MemoryStream(cipher))
                        {
                            using (var csDecrypt = new CryptoStream(msDecrypt, decryptor, CryptoStreamMode.Read))
                            {
                                using (var srDecrypt = new StreamReader(csDecrypt))
                                {
                                    result = srDecrypt.ReadToEnd();
                                }
                            }
                        }

                        return result;
                    }
                }
            }
            catch (Exception ex)
            {
                return null;
            }
        }


        public string ActivateLicenceInRegistry(string key)
        {
            try
            {
                string[] values = key.Split(',');
                string installDatekey = values[0];
                string useDateKey = values[1];
                string installDate = DecryptString(installDatekey, InstallDateEncryptionkey);
                string useDate = DecryptString(useDateKey, useDateEncryptionKey);
                DateTime installDateTime = DateTime.ParseExact(installDate, "dd/MM/yyyy", System.Globalization.CultureInfo.InvariantCulture);
                DateTime useDateTime = DateTime.ParseExact(useDate, "dd/MM/yyyy", System.Globalization.CultureInfo.InvariantCulture);
                TimeSpan diff = useDateTime.Subtract(installDateTime); //first.Subtract(second);
                int totaldays = (int)diff.TotalDays;

                if (totaldays > 0)
                {
                    setInstallDate(installDateTime);
                    setUseDate(installDateTime);
                    setNoOfDaysLicence(totaldays);
                    globalMessage = Convert.ToString(totaldays);
                    return globalMessage;
                }
                else
                {
                    return "Error";
                }

            }
            catch (Exception ex)
            {
                return "Error";

            }


        }

        public void test()
        {
            DateTime value = new DateTime(2018, 06, 10);
            setUseDate(value);
        }

        public void test1()
        {
            DateTime startTime = new DateTime(2018, 05, 10);
            DateTime endTime = new DateTime(2018, 06, 10);
            string onlyDate = startTime.ToString("dd/MM/yyyy"); // get only date not time
            var encryptedInstall = EncryptString(onlyDate, InstallDateEncryptionkey);
            string onlyDateuse = endTime.ToString("dd/MM/yyyy"); // get only date not time
            var encrypteduse = EncryptString(onlyDateuse, useDateEncryptionKey);
            var ss = "djd";

        }
    }
}
