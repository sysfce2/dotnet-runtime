// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Versioning;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Diagnostics.Metrics;
using Microsoft.Extensions.Hosting.Internal;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.EventLog;

namespace Microsoft.Extensions.Hosting
{
    /// <summary>
    /// Provides extension methods for the <see cref="IHostBuilder"/> from the hosting package.
    /// </summary>
    public static class HostingHostBuilderExtensions
    {
        /// <summary>
        /// Specifies the environment to be used by the host. To avoid the environment being overwritten by a default
        /// value, ensure this is called after defaults are configured.
        /// </summary>
        /// <param name="hostBuilder">The <see cref="IHostBuilder"/> to configure.</param>
        /// <param name="environment">The environment to host the application in.</param>
        /// <returns>The <see cref="IHostBuilder"/>.</returns>
        public static IHostBuilder UseEnvironment(this IHostBuilder hostBuilder, string environment)
        {
            return hostBuilder.ConfigureHostConfiguration(configBuilder =>
            {
                ArgumentNullException.ThrowIfNull(environment);

                configBuilder.AddInMemoryCollection(new[]
                {
                    new KeyValuePair<string, string?>(HostDefaults.EnvironmentKey, environment)
                });
            });
        }

        /// <summary>
        /// Specifies the content root directory to be used by the host. To avoid the content root directory being
        /// overwritten by a default value, ensure this is called after defaults are configured.
        /// </summary>
        /// <param name="hostBuilder">The <see cref="IHostBuilder"/> to configure.</param>
        /// <param name="contentRoot">Path to root directory of the application.</param>
        /// <returns>The <see cref="IHostBuilder"/>.</returns>
        public static IHostBuilder UseContentRoot(this IHostBuilder hostBuilder, string contentRoot)
        {
            return hostBuilder.ConfigureHostConfiguration(configBuilder =>
            {
                ArgumentNullException.ThrowIfNull(contentRoot);

                configBuilder.AddInMemoryCollection(new[]
                {
                    new KeyValuePair<string, string?>(HostDefaults.ContentRootKey, contentRoot)
                });
            });
        }

        /// <summary>
        /// Specifies the <see cref="IServiceProvider"/> to be the default one.
        /// </summary>
        /// <param name="hostBuilder">The <see cref="IHostBuilder"/> to configure.</param>
        /// <param name="configure">The delegate that configures the <see cref="IServiceProvider"/>.</param>
        /// <returns>The <see cref="IHostBuilder"/>.</returns>
        public static IHostBuilder UseDefaultServiceProvider(this IHostBuilder hostBuilder, Action<ServiceProviderOptions> configure)
            => hostBuilder.UseDefaultServiceProvider((context, options) => configure(options));

        /// <summary>
        /// Specifies the <see cref="IServiceProvider"/> to be the default one.
        /// </summary>
        /// <param name="hostBuilder">The <see cref="IHostBuilder"/> to configure.</param>
        /// <param name="configure">The delegate that configures the <see cref="IServiceProvider"/>.</param>
        /// <returns>The <see cref="IHostBuilder"/>.</returns>
        public static IHostBuilder UseDefaultServiceProvider(this IHostBuilder hostBuilder, Action<HostBuilderContext, ServiceProviderOptions> configure)
        {
            return hostBuilder.UseServiceProviderFactory(context =>
            {
                var options = new ServiceProviderOptions();
                configure(context, options);
                return new DefaultServiceProviderFactory(options);
            });
        }

        /// <summary>
        /// Adds a delegate for configuring the provided <see cref="ILoggingBuilder"/>. This can be called multiple times.
        /// </summary>
        /// <param name="hostBuilder">The <see cref="IHostBuilder" /> to configure.</param>
        /// <param name="configureLogging">The delegate that configures the <see cref="ILoggingBuilder"/>.</param>
        /// <returns>The same instance of the <see cref="IHostBuilder"/> for chaining.</returns>
        public static IHostBuilder ConfigureLogging(this IHostBuilder hostBuilder, Action<HostBuilderContext, ILoggingBuilder> configureLogging)
        {
            return hostBuilder.ConfigureServices((context, collection) => collection.AddLogging(builder => configureLogging(context, builder)));
        }

        /// <summary>
        /// Adds a delegate for configuring the provided <see cref="ILoggingBuilder"/>. This can be called multiple times.
        /// </summary>
        /// <param name="hostBuilder">The <see cref="IHostBuilder" /> to configure.</param>
        /// <param name="configureLogging">The delegate that configures the <see cref="ILoggingBuilder"/>.</param>
        /// <returns>The same instance of the <see cref="IHostBuilder"/> for chaining.</returns>
        public static IHostBuilder ConfigureLogging(this IHostBuilder hostBuilder, Action<ILoggingBuilder> configureLogging)
        {
            return hostBuilder.ConfigureServices((context, collection) => collection.AddLogging(configureLogging));
        }

        /// <summary>
        /// Adds a delegate for configuring the <see cref="HostOptions"/> of the <see cref="IHost"/>.
        /// </summary>
        /// <param name="hostBuilder">The <see cref="IHostBuilder" /> to configure.</param>
        /// <param name="configureOptions">The delegate for configuring the <see cref="HostOptions"/>.</param>
        /// <returns>The same instance of the <see cref="IHostBuilder"/> for chaining.</returns>
        public static IHostBuilder ConfigureHostOptions(this IHostBuilder hostBuilder, Action<HostBuilderContext, HostOptions> configureOptions)
        {
            return hostBuilder.ConfigureServices(
                (context, collection) => collection.Configure<HostOptions>(options => configureOptions(context, options)));
        }

        /// <summary>
        /// Adds a delegate for configuring the <see cref="HostOptions"/> of the <see cref="IHost"/>.
        /// </summary>
        /// <param name="hostBuilder">The <see cref="IHostBuilder" /> to configure.</param>
        /// <param name="configureOptions">The delegate for configuring the <see cref="HostOptions"/>.</param>
        /// <returns>The same instance of the <see cref="IHostBuilder"/> for chaining.</returns>
        public static IHostBuilder ConfigureHostOptions(this IHostBuilder hostBuilder, Action<HostOptions> configureOptions)
        {
            return hostBuilder.ConfigureServices(collection => collection.Configure(configureOptions));
        }

        /// <summary>
        /// Sets up the configuration for the remainder of the build process and application. This can be called multiple times and
        /// the results will be additive. The results will be available at <see cref="HostBuilderContext.Configuration"/> for
        /// subsequent operations, as well as in <see cref="IHost.Services"/>.
        /// </summary>
        /// <param name="hostBuilder">The <see cref="IHostBuilder" /> to configure.</param>
        /// <param name="configureDelegate">The delegate for configuring the <see cref="IConfigurationBuilder"/> that will be used
        /// to construct the <see cref="IConfiguration"/> for the host.</param>
        /// <returns>The same instance of the <see cref="IHostBuilder"/> for chaining.</returns>
        public static IHostBuilder ConfigureAppConfiguration(this IHostBuilder hostBuilder, Action<IConfigurationBuilder> configureDelegate)
        {
            return hostBuilder.ConfigureAppConfiguration((context, builder) => configureDelegate(builder));
        }

        /// <summary>
        /// Adds services to the container. This can be called multiple times and the results will be additive.
        /// </summary>
        /// <param name="hostBuilder">The <see cref="IHostBuilder" /> to configure.</param>
        /// <param name="configureDelegate">The delegate for configuring the <see cref="IServiceCollection"/>.</param>
        /// <returns>The same instance of the <see cref="IHostBuilder"/> for chaining.</returns>
        public static IHostBuilder ConfigureServices(this IHostBuilder hostBuilder, Action<IServiceCollection> configureDelegate)
        {
            return hostBuilder.ConfigureServices((context, collection) => configureDelegate(collection));
        }

        /// <summary>
        /// Enables configuring the instantiated dependency container. This can be called multiple times and
        /// the results will be additive.
        /// </summary>
        /// <typeparam name="TContainerBuilder">The type of builder.</typeparam>
        /// <param name="hostBuilder">The <see cref="IHostBuilder" /> to configure.</param>
        /// <param name="configureDelegate">The delegate for configuring the <typeparamref name="TContainerBuilder"/>.</param>
        /// <returns>The same instance of the <see cref="IHostBuilder"/> for chaining.</returns>
        public static IHostBuilder ConfigureContainer<TContainerBuilder>(this IHostBuilder hostBuilder, Action<TContainerBuilder> configureDelegate)
        {
            return hostBuilder.ConfigureContainer<TContainerBuilder>((context, builder) => configureDelegate(builder));
        }

        /// <summary>
        /// Configures an existing <see cref="IHostBuilder"/> instance with pre-configured defaults. This will overwrite
        /// previously configured values and is intended to be called before additional configuration calls.
        /// </summary>
        /// <remarks>
        ///   The following defaults are applied to the <see cref="IHostBuilder"/>:
        ///     * set the <see cref="IHostEnvironment.ContentRootPath"/> to the result of <see cref="Directory.GetCurrentDirectory()"/>
        ///     * load host <see cref="IConfiguration"/> from "DOTNET_" prefixed environment variables
        ///     * load host <see cref="IConfiguration"/> from supplied command line args
        ///     * load app <see cref="IConfiguration"/> from 'appsettings.json' and 'appsettings.[<see cref="IHostEnvironment.EnvironmentName"/>].json'
        ///     * load app <see cref="IConfiguration"/> from '[<see cref="IHostEnvironment.ApplicationName"/>].settings.json' and '[<see cref="IHostEnvironment.ApplicationName"/>].settings.[<see cref="IHostEnvironment.EnvironmentName"/>].json' when <see cref="IHostEnvironment.ApplicationName"/> is not empty
        ///     * load app <see cref="IConfiguration"/> from User Secrets when <see cref="IHostEnvironment.EnvironmentName"/> is 'Development' using the entry assembly
        ///     * load app <see cref="IConfiguration"/> from environment variables
        ///     * load app <see cref="IConfiguration"/> from supplied command line args
        ///     * configure the <see cref="ILoggerFactory"/> to log to the console, debug, and event source output
        ///     * enables scope validation on the dependency injection container when <see cref="IHostEnvironment.EnvironmentName"/> is 'Development'
        /// </remarks>
        /// <param name="builder">The existing builder to configure.</param>
        /// <param name="args">The command line args.</param>
        /// <returns>The same instance of the <see cref="IHostBuilder"/> for chaining.</returns>
        public static IHostBuilder ConfigureDefaults(this IHostBuilder builder, string[]? args)
        {
            return builder.ConfigureHostConfiguration(config => ApplyDefaultHostConfiguration(config, args))
                          .ConfigureAppConfiguration((hostingContext, config) => ApplyDefaultAppConfiguration(hostingContext, config, args))
                          .ConfigureServices(AddDefaultServices)
                          .UseServiceProviderFactory(context => new DefaultServiceProviderFactory(CreateDefaultServiceProviderOptions(context)));
        }

        private static void ApplyDefaultHostConfiguration(IConfigurationBuilder hostConfigBuilder, string[]? args)
        {
            SetDefaultContentRoot(hostConfigBuilder);

            hostConfigBuilder.AddEnvironmentVariables(prefix: "DOTNET_");
            AddCommandLineConfig(hostConfigBuilder, args);
        }

        internal static void SetDefaultContentRoot(IConfigurationBuilder hostConfigBuilder)
        {
            // If we're running anywhere other than C:\Windows\system32, we default to using the CWD for the ContentRoot.
            // However, since many things like Windows services and MSIX installers have C:\Windows\system32 as there CWD which is not likely
            // to really be the home for things like appsettings.json, we skip changing the ContentRoot in that case. The non-"default" initial
            // value for ContentRoot is AppContext.BaseDirectory (e.g. the executable path) which probably makes more sense than the system32.

            // In my testing, both Environment.CurrentDirectory and Environment.SystemDirectory return the path without
            // any trailing directory separator characters. I'm not even sure the casing can ever be different from these APIs, but I think it makes sense to
            // ignore case for Windows path comparisons given the file system is usually (always?) going to be case insensitive for the system path.
            string cwd = Environment.CurrentDirectory;
            if (
#if NETFRAMEWORK
                Environment.OSVersion.Platform != PlatformID.Win32NT ||
#else
                !RuntimeInformation.IsOSPlatform(OSPlatform.Windows) ||
#endif
                !string.Equals(cwd, Environment.SystemDirectory, StringComparison.OrdinalIgnoreCase))
            {
                hostConfigBuilder.AddInMemoryCollection(new[]
                {
                    new KeyValuePair<string, string?>(HostDefaults.ContentRootKey, cwd),
                });
            }
        }

        internal static void ApplyDefaultAppConfiguration(HostBuilderContext hostingContext, IConfigurationBuilder appConfigBuilder, string[]? args)
        {
            IHostEnvironment env = hostingContext.HostingEnvironment;
            bool reloadOnChange = GetReloadConfigOnChangeValue(hostingContext);

            appConfigBuilder.AddJsonFile("appsettings.json", optional: true, reloadOnChange: reloadOnChange)
                    .AddJsonFile($"appsettings.{env.EnvironmentName}.json", optional: true, reloadOnChange: reloadOnChange);

            if (env.ApplicationName is { Length: > 0 })
            {
                string sanitizedApplicationName = env.ApplicationName.Replace(Path.DirectorySeparatorChar, '_')
                                                                    .Replace(Path.AltDirectorySeparatorChar, '_');
                appConfigBuilder.AddJsonFile($"{sanitizedApplicationName}.settings.json", optional: true, reloadOnChange: reloadOnChange)
                        .AddJsonFile($"{sanitizedApplicationName}.settings.{env.EnvironmentName}.json", optional: true, reloadOnChange: reloadOnChange);
            }

            if (env.IsDevelopment() && env.ApplicationName is { Length: > 0 })
            {
                try
                {
                    var appAssembly = Assembly.Load(new AssemblyName(env.ApplicationName));
                    appConfigBuilder.AddUserSecrets(appAssembly, optional: true, reloadOnChange: reloadOnChange);
                }
                catch (FileNotFoundException)
                {
                    // The assembly cannot be found, so just skip it.
                }
            }

            appConfigBuilder.AddEnvironmentVariables();

            AddCommandLineConfig(appConfigBuilder, args);

            [UnconditionalSuppressMessage("ReflectionAnalysis", "IL2026:RequiresUnreferencedCode", Justification = "Calling IConfiguration.GetValue is safe when the T is bool.")]
            static bool GetReloadConfigOnChangeValue(HostBuilderContext hostingContext) => hostingContext.Configuration.GetValue("hostBuilder:reloadConfigOnChange", defaultValue: true);
        }

        internal static void AddCommandLineConfig(IConfigurationBuilder configBuilder, string[]? args)
        {
            if (args is { Length: > 0 })
            {
                configBuilder.AddCommandLine(args);
            }
        }

        internal static void AddDefaultServices(HostBuilderContext hostingContext, IServiceCollection services)
        {
            services.AddLogging(logging =>
            {
                bool isWindows =
#if NET
                    OperatingSystem.IsWindows();
#elif NETFRAMEWORK
                    Environment.OSVersion.Platform == PlatformID.Win32NT;
#else
                    RuntimeInformation.IsOSPlatform(OSPlatform.Windows);
#endif

                // IMPORTANT: This needs to be added *before* configuration is loaded, this lets
                // the defaults be overridden by the configuration.
                if (isWindows)
                {
                    // Default the EventLogLoggerProvider to warning or above
                    logging.AddFilter<EventLogLoggerProvider>(level => level >= LogLevel.Warning);
                }

                logging.AddConfiguration(hostingContext.Configuration.GetSection("Logging"));
#if NET
                if (!OperatingSystem.IsBrowser() && !OperatingSystem.IsWasi())
#endif
                {
                    logging.AddConsole();
                }
                logging.AddDebug();
                logging.AddEventSourceLogger();

                if (isWindows)
                {
                    // Add the EventLogLoggerProvider on windows machines
                    logging.AddEventLog();
                }

                logging.Configure(options =>
                {
                    options.ActivityTrackingOptions =
                        ActivityTrackingOptions.SpanId |
                        ActivityTrackingOptions.TraceId |
                        ActivityTrackingOptions.ParentId;
                });
            });

            services.AddMetrics(metrics =>
            {
                metrics.AddConfiguration(hostingContext.Configuration.GetSection("Metrics"));
            });
        }

        internal static ServiceProviderOptions CreateDefaultServiceProviderOptions(HostBuilderContext context)
        {
            bool isDevelopment = context.HostingEnvironment.IsDevelopment();
            return new ServiceProviderOptions
            {
                ValidateScopes = isDevelopment,
                ValidateOnBuild = isDevelopment,
            };
        }

        /// <summary>
        /// Listens for Ctrl+C or SIGTERM and calls <see cref="IHostApplicationLifetime.StopApplication"/> to start the shutdown process.
        /// This will unblock extensions like RunAsync and WaitForShutdownAsync.
        /// </summary>
        /// <param name="hostBuilder">The <see cref="IHostBuilder" /> to configure.</param>
        /// <returns>The same instance of the <see cref="IHostBuilder"/> for chaining.</returns>
        [UnsupportedOSPlatform("android")]
        [UnsupportedOSPlatform("browser")]
        [UnsupportedOSPlatform("ios")]
        [UnsupportedOSPlatform("tvos")]
        public static IHostBuilder UseConsoleLifetime(this IHostBuilder hostBuilder)
        {
            return hostBuilder.ConfigureServices(collection => collection.AddSingleton<IHostLifetime, ConsoleLifetime>());
        }

        /// <summary>
        /// Listens for Ctrl+C or SIGTERM and calls <see cref="IHostApplicationLifetime.StopApplication"/> to start the shutdown process.
        /// This will unblock extensions like RunAsync and WaitForShutdownAsync.
        /// </summary>
        /// <param name="hostBuilder">The <see cref="IHostBuilder" /> to configure.</param>
        /// <param name="configureOptions">The delegate for configuring the <see cref="ConsoleLifetime"/>.</param>
        /// <returns>The same instance of the <see cref="IHostBuilder"/> for chaining.</returns>
        [UnsupportedOSPlatform("android")]
        [UnsupportedOSPlatform("browser")]
        [UnsupportedOSPlatform("ios")]
        [UnsupportedOSPlatform("tvos")]
        public static IHostBuilder UseConsoleLifetime(this IHostBuilder hostBuilder, Action<ConsoleLifetimeOptions> configureOptions)
        {
            return hostBuilder.ConfigureServices(collection =>
            {
                collection.AddSingleton<IHostLifetime, ConsoleLifetime>();
                collection.Configure(configureOptions);
            });
        }

        /// <summary>
        /// Enables console support, builds and starts the host, and waits for Ctrl+C or SIGTERM to shut down.
        /// </summary>
        /// <param name="hostBuilder">The <see cref="IHostBuilder" /> to configure.</param>
        /// <param name="cancellationToken">A <see cref="CancellationToken"/> that can be used to cancel the console.</param>
        /// <returns>A <see cref="Task"/> that only completes when the token is triggered or shutdown is triggered.</returns>
        [UnsupportedOSPlatform("android")]
        [UnsupportedOSPlatform("browser")]
        [UnsupportedOSPlatform("ios")]
        [UnsupportedOSPlatform("tvos")]
        public static Task RunConsoleAsync(this IHostBuilder hostBuilder, CancellationToken cancellationToken = default)
        {
            return hostBuilder.UseConsoleLifetime().Build().RunAsync(cancellationToken);
        }

        /// <summary>
        /// Enables console support, builds and starts the host, and waits for Ctrl+C or SIGTERM to shut down.
        /// </summary>
        /// <param name="hostBuilder">The <see cref="IHostBuilder" /> to configure.</param>
        /// <param name="configureOptions">The delegate for configuring the <see cref="ConsoleLifetime"/>.</param>
        /// <param name="cancellationToken">A <see cref="CancellationToken"/> that can be used to cancel the console.</param>
        /// <returns>A <see cref="Task"/> that only completes when the token is triggered or shutdown is triggered.</returns>
        [UnsupportedOSPlatform("android")]
        [UnsupportedOSPlatform("browser")]
        [UnsupportedOSPlatform("ios")]
        [UnsupportedOSPlatform("tvos")]
        public static Task RunConsoleAsync(this IHostBuilder hostBuilder, Action<ConsoleLifetimeOptions> configureOptions, CancellationToken cancellationToken = default)
        {
            return hostBuilder.UseConsoleLifetime(configureOptions).Build().RunAsync(cancellationToken);
        }

        /// <summary>
        /// Adds a delegate for configuring the provided <see cref="IMetricsBuilder"/>. This can be called multiple times.
        /// </summary>
        /// <param name="hostBuilder">The <see cref="IHostBuilder" /> to configure.</param>
        /// <param name="configureMetrics">The delegate that configures the <see cref="IMetricsBuilder"/>.</param>
        /// <returns>The same instance of the <see cref="IHostBuilder"/> for chaining.</returns>
        public static IHostBuilder ConfigureMetrics(this IHostBuilder hostBuilder, Action<IMetricsBuilder> configureMetrics)
        {
            return hostBuilder.ConfigureServices((context, collection) => collection.AddMetrics(configureMetrics));
        }

        /// <summary>
        /// Adds a delegate for configuring the provided <see cref="IMetricsBuilder"/>. This can be called multiple times.
        /// </summary>
        /// <param name="hostBuilder">The <see cref="IHostBuilder" /> to configure.</param>
        /// <param name="configureMetrics">The delegate that configures the <see cref="IMetricsBuilder"/>.</param>
        /// <returns>The same instance of the <see cref="IHostBuilder"/> for chaining.</returns>
        public static IHostBuilder ConfigureMetrics(this IHostBuilder hostBuilder, Action<HostBuilderContext, IMetricsBuilder> configureMetrics)
        {
            return hostBuilder.ConfigureServices((context, collection) => collection.AddMetrics(builder => configureMetrics(context, builder)));
        }
    }
}
