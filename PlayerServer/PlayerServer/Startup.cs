using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.FileProviders;
using Newtonsoft.Json;

namespace PlayerServer
{
    public class Startup
    {
        // This method gets called by the runtime. Use this method to add services to the container.
        // For more information on how to configure your application, visit https://go.microsoft.com/fwlink/?LinkID=398940
        public void ConfigureServices(IServiceCollection services)
        {
            services.AddMvc()
          .AddXmlSerializerFormatters()
          .AddNewtonsoftJson(options => options.SerializerSettings.ReferenceLoopHandling = Newtonsoft.Json.ReferenceLoopHandling.Ignore);

            services.AddTransient<PlayerServer.services.PlayerUrlService>();
        }

        // This method gets called by the runtime. Use this method to configure the HTTP request pipeline.
        public void Configure(IApplicationBuilder app, IHostingEnvironment env)
        {
            if (env.IsDevelopment())
            {
                app.UseDeveloperExceptionPage();
            }

            app.Use(async (context, next) =>
            {
                try
                {
                    if (context.Request.Path.Value.StartsWith("/url"))
                    {
                        if (Licensing.LicenseInfo.IsValid)
                        {
                            await next();
                        }
                        else
                        {
                            context.Response.StatusCode = 403;
                        }
                    }
                    else
                    {
                        if (!context.Request.Path.Value.StartsWith("/api/License") && !context.Request.Path.Value.StartsWith("/license") && !context.Request.Path.Value.StartsWith("/api/GenerateDeviceKey"))
                        {
                            if (!Licensing.LicenseInfo.IsValid && !context.Request.Path.Value.StartsWith("/js") && !context.Request.Path.Value.StartsWith("/css"))
                            {

                                context.Request.Path = "/license.html";
                            }

                        }
                        else if (context.Request.Path.Value.Equals("/license.html") && Licensing.LicenseInfo.IsValid)
                        {
                            context.Request.Path = "/index.html";
                        }
                        await next();
                        if (context.Response.StatusCode == 404 && !Path.HasExtension(context.Request.Path.Value))
                        {
                            context.Request.Path = "/index.html";
                            await next();
                        }
                    }

                }
                catch (Exception ex)
                {
                    Console.WriteLine(ex);
                }
            });

            app.UseRouting();

            app.UseEndpoints(endpoints =>
            {
                endpoints.MapControllerRoute(
                    name: "default",
                    pattern: "{controller}/{action}/{id?}");
            });
                       
            DefaultFilesOptions options = new DefaultFilesOptions();
            options.DefaultFileNames.Clear();
            //options.DefaultFileNames.Add("index.html");
            app.UseDefaultFiles();

            app.UseStaticFiles();

            if (!Directory.Exists(Path.Combine(Directory.GetCurrentDirectory(), "setup")))
            {
                Directory.CreateDirectory(Path.Combine(Directory.GetCurrentDirectory(), "setup"));
            }

            app.UseStaticFiles(new StaticFileOptions() // for files in snapshots folder
            {
                FileProvider = new PhysicalFileProvider(
                         Path.Combine(Directory.GetCurrentDirectory(), "setup")),
                RequestPath = new PathString("/setup")
            });
        }
    }
}
