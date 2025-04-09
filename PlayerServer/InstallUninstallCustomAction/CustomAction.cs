using System;
using System.Collections.Generic;
using Microsoft.Deployment.WindowsInstaller;
using System.Diagnostics;
using Microsoft.Win32;
using System.IO;
using System.Security.Principal;
using System.Security.AccessControl;
using System.ServiceProcess;
using System.Configuration.Install;

namespace InstallUninstallCustomAction
{
    public class CustomActions
    {
        public static Session session;
        public static string InstalledPath { get; set; }
        private static string path;
        private static readonly string player_server = "i2v_Player_Server";

        [CustomAction]
        public static ActionResult OnAfterInstall(Session session1)
        {
            //System.Diagnostics.Debugger.Launch();
            session = session1;

            try
            {
                InstalledPath = session.CustomActionData["INSTALLEDPATH"];
                path = InstalledPath;

                CreateExceptionFile_Installer();
                CreateServices();
            }
            catch (Exception ex)
            {
                session.Log("OnAfterInstall Exception");
                session.Log(ex.ToString());
            }
            return ActionResult.Success;
        }

        public static void CreateServices()
        {
            StopAllServices();
            DeleteAllService();
            CreateServiceUsingNSSM(player_server, "server/PlayerServer.bat");
            StartAllServices();
            Write_Registry();
        }

        public static void CreateServiceUsingNSSM(string ServiceName, string ExecutableFilePath)
        {
            Process process = new Process();
            process.StartInfo.UseShellExecute = false;
            process.StartInfo.CreateNoWindow = true;
            process.StartInfo.FileName = $"{InstalledPath}/nssm.exe";
            process.StartInfo.Arguments = $"install {ServiceName} \"{InstalledPath}/{ExecutableFilePath}\"";
            process.Start();
            process.WaitForExit();
            SetServiceAutomaticAndFailureConfiguration(ServiceName);
        }

        public static void SetServiceAutomaticAndFailureConfiguration(string ServiceName)
        {
            Process SetRecoveryProcess = new Process();
            SetRecoveryProcess.StartInfo.UseShellExecute = false;
            SetRecoveryProcess.StartInfo.CreateNoWindow = true;
            SetRecoveryProcess.StartInfo.FileName = $"sc";
            SetRecoveryProcess.StartInfo.Arguments = $"failure {ServiceName} reset= 0 actions= restart/0/restart/0/restart/0";
            SetRecoveryProcess.Start();
            SetRecoveryProcess.WaitForExit();
            Process SetAutomaticProcess = new Process();
            SetAutomaticProcess.StartInfo.UseShellExecute = false;
            SetAutomaticProcess.StartInfo.CreateNoWindow = true;
            SetAutomaticProcess.StartInfo.FileName = $"sc";
            SetAutomaticProcess.StartInfo.Arguments = $"config {ServiceName} start=auto";
            SetAutomaticProcess.Start();
            SetAutomaticProcess.WaitForExit();
        }

        public static void StartAllServices()
        {
            // postgres service has already been started in start_pgsql.bat file
            startService(player_server, 60000);
        }

        public static void StopAllServices()
        {
            Stop_Services(player_server, 60000);
        }

        private static bool startService(string serviceName, int timeoutMilliseconds)
        {
            ServiceController service = new ServiceController(serviceName);
            try
            {
                //System.Diagnostics.Debugger.Launch();
                session.Log("ENTER IN START SERVICE OF " + serviceName);
                TimeSpan timeout = TimeSpan.FromMilliseconds(timeoutMilliseconds);
                if (service == null)
                    session.Log("NULL VALUE OF SERVICE " + serviceName);
                else
                    session.Log("Displayname  OF SERVICE " + service.DisplayName);
                // WriteException("ENTER IN START  WriteException("NULL VALUE OF SERVICE " + serviceName); SERVICE OF " + serviceName);
                service.Start();
                session.Log("START SERVICE OF " + serviceName);
                service.WaitForStatus(ServiceControllerStatus.Running, timeout);
                return true;
            }
            catch (InvalidOperationException ex1)
            {
                session.Log(ex1.ToString());
                return false;
            }

            catch (Exception ex)
            {
                session.Log(ex.ToString());
                session.Log("Failed in" + serviceName);
                return false;
            }
            finally
            {
                service.Close();
            }

        }

        private static void Stop_Services(string serviceName, int timeoutMilliseconds)
        {
            ServiceController service = new ServiceController(serviceName);
            try
            {

                if (IsService_Already_Exist(serviceName))
                {
                    int millisec1 = Environment.TickCount;
                    TimeSpan timeout = TimeSpan.FromMilliseconds(timeoutMilliseconds);
                    //MonitoringLog.AddLogtoFile(Enum_LogType.Information, "Going to stop Service ");
                    if (service.Status != ServiceControllerStatus.Stopped && service.Status != ServiceControllerStatus.StopPending)
                    {
                        //MonitoringLog.AddLogtoFile(Enum_LogType.Information, "Check Service status: " + service.Status.ToString());
                        service.Stop();
                        service.WaitForStatus(ServiceControllerStatus.Stopped, timeout);

                        if (service.Status != ServiceControllerStatus.Stopped)
                        {
                            //KillProcess(processName);
                        }
                    }
                }
            }

            catch (System.InvalidOperationException ex)
            {
                //WriteException(ex);
            }
            catch (Exception ex)
            {
                session.Log(ex.ToString());
            }
            finally
            {
                service.Close();
            }
        }

        private static bool IsService_Already_Exist(string serviceName)
        {
            ServiceController[] services = ServiceController.GetServices();
            foreach (ServiceController service in services)
            {
                if (service.ServiceName == serviceName)
                {

                    //sendUninstallCommand(exeName, PATH);
                    return true;
                }
            }
            return false;
        }

        private static void UninstallService(string servicename)
        {
            try
            {
                if (IsService_Already_Exist(servicename))
                {
                    ServiceInstaller ServiceInstallerObj = new ServiceInstaller();
                    InstallContext context = new InstallContext(GenerateDefaultLogFileName(), null);
                    ServiceInstallerObj.Context = context;
                    ServiceInstallerObj.ServiceName = servicename;
                    ServiceInstallerObj.Uninstall(null);
                }
            }
            catch (Exception ex)
            {
                session.Log(ex.ToString());
            }

        }

        private static string GenerateDefaultLogFileName()
        {
            string path = InstalledPath;
            string FileName = path + @"\Common\InstallerException.txt";
            return FileName;
            //DirectoryInfo root = Directory.CreateDirectory(Application.StartupPath + @"\ExceptionLogs");
            // string Logs_Directory = Application.StartupPath + @"\ExceptionLogs";


            // return Logs_Directory + @"\" + DateTime.Now.Month + "_" + DateTime.Now.Day + "_" + DateTime.Now.Year + ".log";

        }

        public static void DeleteAllService()
        {
            UninstallService(player_server);
        }

        private static bool Write_Registry()
        {
            try
            {
                // RegistryKey rk = REGISTRY_KEY;
                string subkey = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\PlayerServer";
                // I have to use CreateSubKey 
                // (create or open it if already exits), 
                // 'cause OpenSubKey open a subKey as read-only
                RemoveRegistryEntry();
                RegistryKey sk1 = Registry.LocalMachine.CreateSubKey(subkey);
                // Save the value
                sk1.SetValue("DisplayName", "i2V_Player_Server");
                sk1.SetValue("Publisher", "i2V");
                sk1.SetValue("InstallLocation", InstalledPath);
                sk1.SetValue("InstallMode", "Self");
                return true;
            }
            catch (Exception e)
            {
                // AAAAAAAAAAARGH, an error!
                session.Log(e.ToString());//, "Writing registry " + KeyName.ToUpper());
                return false;
            }

        }

        private static bool RemoveRegistryEntry()
        {
            try
            {
                // RegistryKey rk = REGISTRY_KEY;
                string subkey = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\PlayerServer";
                // I have to use CreateSubKey 
                // (create or open it if already exits), 
                // 'cause OpenSubKey open a subKey as read-only
                Registry.LocalMachine.DeleteSubKey(subkey);
                // Save the value
                return true;
            }
            catch (Exception e)
            {
                // AAAAAAAAAAARGH, an error!
                session.Log(e.ToString());//, "Writing registry " + KeyName.ToUpper());
                return false;
            }

        }

        [CustomAction]
        public static ActionResult OnUninstall(Session session1)
        {
            //System.Diagnostics.Debugger.Launch();
            session = session1;
            try
            {
                InstalledPath = session.CustomActionData["INSTALLEDPATH"];

                CreateExceptionFile_Installer();
                StopAllServices();
                DeleteAllService();
                RemoveRegistryEntry();
            }
            catch (Exception ex)
            {
                session.Log("OnUninstall Exception");
                session.Log(ex.ToString());
            }
            return ActionResult.Success;

        }

        [CustomAction]
        public static ActionResult Rollback(Session session1)
        {

            //  System.Diagnostics.Debugger.Launch();
            session = session1;
            try
            {
                CreateExceptionFile_Installer();
                session.Log("Enter on Rollback Method to delete Services");
                session.Log("Exit From Rollback Method to delete Services");

            }
            catch (Exception ex)
            {
                session.Log("OnUninstall Exception");
                session.Log(ex.ToString());
            }
            return ActionResult.Success;


        }

        private static void CreateExceptionFile_Installer()
        {
            try
            {
                string path = InstalledPath;
                if (!File.Exists(path + @"InstallerException.txt"))
                    File.Create(path + @"InstallerException.txt");
            }
            catch (Exception ex)
            {
            }
        }
    }
}
