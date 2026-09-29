// Copyright Tempo Simulation, LLC. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Text.RegularExpressions;
using UnrealBuildTool;

public class rclcpp : ModuleRules
{
    public class ModuleDepPaths
    {
        public readonly string[] HeaderPaths;
        public readonly string[] LibraryPaths;
        public readonly string[] RuntimeLibraryPaths;

        public ModuleDepPaths(string[] headerPaths, string[] libraryPaths, string[] runtimeLibraryPaths)
        {
            HeaderPaths = headerPaths;
            LibraryPaths = libraryPaths;
            RuntimeLibraryPaths = runtimeLibraryPaths;
        }
    }

    private IEnumerable<string> FindFilesInDirectory(string dir, string suffix = "")
    {
        return Directory.EnumerateFiles(dir, "*." + suffix, SearchOption.AllDirectories);
    }

    // The plugin libraries pluginlib can load from this ament prefix: every <library path="X"> in each plugin description
    // xml that the ament index registers under a "<base>__pluginlib__plugin" resource. Returns the X.dll paths in prefixDir.
    private HashSet<string> FindPluginlibLibraries(string prefixDir)
    {
        HashSet<string> libraries = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        string resourceIndex = Path.Combine(prefixDir, "share", "ament_index", "resource_index");
        if (!Directory.Exists(resourceIndex))
        {
            return libraries;
        }
        foreach (string resourceDir in Directory.EnumerateDirectories(resourceIndex, "*__pluginlib__plugin"))
        {
            foreach (string resource in Directory.EnumerateFiles(resourceDir))
            {
                foreach (string line in File.ReadAllLines(resource))
                {
                    string xml = Path.Combine(prefixDir, line.Trim());
                    if (line.Trim().Length == 0 || !File.Exists(xml))
                    {
                        continue;
                    }
                    foreach (Match match in Regex.Matches(File.ReadAllText(xml), "<library\\s+path=\"([^\"]+)\""))
                    {
                        string dll = Path.Combine(prefixDir, match.Groups[1].Value + ".dll");
                        if (File.Exists(dll))
                        {
                            libraries.Add(dll);
                        }
                    }
                }
            }
        }
        return libraries;
    }

    public ModuleDepPaths GatherDeps()
    {
        List<string> HeaderPaths = new List<string>();
        List<string> LibraryPaths = new List<string>();
        List<string> RuntimeLibraryPaths = new List<string>();
        
        HeaderPaths.Add(Path.Combine(ModuleDirectory, "Includes"));

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            LibraryPaths.AddRange(FindFilesInDirectory(Path.Combine(ModuleDirectory, "Libraries", "Windows"), "lib"));
            RuntimeLibraryPaths.AddRange(FindFilesInDirectory(Path.Combine(ModuleDirectory, "Binaries", "Windows"), "dll"));
            RuntimeLibraryPaths.AddRange(FindFilesInDirectory(Path.Combine(ModuleDirectory, "Binaries", "Windows", "share"), "*"));
        }
        else if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            LibraryPaths.AddRange(FindFilesInDirectory(Path.Combine(ModuleDirectory, "Libraries", "Mac"), "dylib"));
            RuntimeLibraryPaths.AddRange(FindFilesInDirectory(Path.Combine(ModuleDirectory, "Libraries", "Mac"), "dylib"));
            RuntimeLibraryPaths.AddRange(FindFilesInDirectory(Path.Combine(ModuleDirectory, "Libraries", "Mac", "share"), "*"));
        }
        else if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            LibraryPaths.AddRange(FindFilesInDirectory(Path.Combine(ModuleDirectory, "Libraries", "Linux"), "so"));
            RuntimeLibraryPaths.AddRange(FindFilesInDirectory(Path.Combine(ModuleDirectory, "Libraries", "Linux"), "so*"));
            RuntimeLibraryPaths.AddRange(FindFilesInDirectory(Path.Combine(ModuleDirectory, "Libraries", "Linux", "share"), "*"));
        }
        else
        {
            Console.WriteLine("Unsupported target platform for module rclcpp.");
        }

        return new ModuleDepPaths(HeaderPaths.ToArray(), LibraryPaths.ToArray(), RuntimeLibraryPaths.ToArray());
    }
    
    public rclcpp(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;
        
        PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public"));
        
        ModuleDepPaths moduleDepPaths = GatherDeps();
        PublicIncludePaths.AddRange(moduleDepPaths.HeaderPaths);
        PublicAdditionalLibraries.AddRange(moduleDepPaths.LibraryPaths);

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicRuntimeLibraryPaths.Add(Path.Combine(ModuleDirectory, "Binaries", "Windows"));
        }

        // In a monolithic build (i.e. a packaged game) TempoROS is linked into the game executable itself, so the
        // Windows loader binds rclcpp's imports before any of our code - TempoROSBootstrap included - gets to run.
        // No module startup hook can ever be early enough. The executable's own directory is the one place the
        // loader searches unaided, so stage the dlls there. Every later load by name (the rmw implementation,
        // typesupport) resolves from that same directory too.
        // The share directory is deliberately left where it is, since AMENT_PREFIX_PATH points at its parent.
        // NOTE: pluginlib plugins (image_transport's) are the exception. pluginlib looks for the plugin library FILE
        // under the ament prefix and skips a plugin whose file is not there, before any load by name is attempted, so
        // with the dll only next to the executable every image publisher fails with "No plugins found!". They are
        // staged in BOTH places: next to the executable (their imports), and in the prefix (pluginlib's lookup).
        // Found by a packaged Win64 demo (dains-thermal DAINS-144, 2026-09-29): copying the four image_transport
        // plugin dlls into the prefix was the only change between a failing and a passing run.
        bool bStageDllsNextToExecutable = Target.Platform == UnrealTargetPlatform.Win64 &&
                                          Target.LinkType == TargetLinkType.Monolithic;

        HashSet<string> pluginlibLibraries = bStageDllsNextToExecutable
            ? FindPluginlibLibraries(Path.Combine(ModuleDirectory, "Binaries", "Windows"))
            : new HashSet<string>();

        foreach (string libraryPath in moduleDepPaths.RuntimeLibraryPaths)
        {
            if (bStageDllsNextToExecutable && libraryPath.EndsWith(".dll", StringComparison.OrdinalIgnoreCase))
            {
                RuntimeDependencies.Add(Path.Combine("$(TargetOutputDir)", Path.GetFileName(libraryPath)),
                    libraryPath, StagedFileType.NonUFS);
                if (pluginlibLibraries.Contains(libraryPath))
                {
                    RuntimeDependencies.Add(libraryPath);
                }
            }
            else
            {
                RuntimeDependencies.Add(libraryPath);
            }
        }

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "Core"
            }
            );

        // We need _LIBCPP_HAS_NO_RTTI to be set to avoid using RTTI in rclcpp, but it is already
        // set in UE 5.6 (but not 5.7) in v25_clang-18.1.0-rockylinux8/x86_64-unknown-linux-gnu/include/c++/v1/__config
        if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion != 6 && Target.Platform == UnrealTargetPlatform.Linux)
        {
            PublicDefinitions.Add("_LIBCPP_HAS_NO_RTTI=1");
        }

        // Xcode 26+ (Apple clang-1700.3+) no longer automatically defines _LIBCPP_HAS_NO_RTTI in
        // libc++'s __config when compiling with -fno-rtti. Older Xcode versions still define it,
        // so we check the clang build version to avoid a -Werror,-Wmacro-redefined error.
        // Note: clang must already be installed for UBT to reach this point.
        if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            bool NeedNoRTTI = false;
            try
            {
                var Psi = new System.Diagnostics.ProcessStartInfo("clang", "--version")
                {
                    RedirectStandardOutput = true,
                    UseShellExecute = false
                };
                using var Proc = System.Diagnostics.Process.Start(Psi);
                string Output = Proc.StandardOutput.ReadToEnd();
                Proc.WaitForExit();
                // Output contains e.g. "(clang-1700.6.3.2)" - gate on MAJOR.MINOR >= 1700.3
                var Match = Regex.Match(Output, @"clang-(\d+)\.(\d+)\.");
                if (Match.Success
                    && int.TryParse(Match.Groups[1].Value, out int Major)
                    && int.TryParse(Match.Groups[2].Value, out int Minor)
                    && (Major > 1700 || (Major == 1700 && Minor >= 3)))
                {
                    NeedNoRTTI = true;
                }
                else if (!Match.Success)
                {
                    Console.WriteLine("rclcpp: Warning: could not parse clang version from: " + Output);
                }
            }
            catch (Exception Ex)
            {
                Console.WriteLine("rclcpp: Warning: failed to query clang version: " + Ex.Message);
            }
            if (NeedNoRTTI)
            {
                PublicDefinitions.Add("_LIBCPP_HAS_NO_RTTI=1");
            }
        }

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicDefinitions.Add("EPROSIMA_ALL_DYN_LINK=1");
            // Either of these supposedly silence a build warning. But they do not actually work.
            // https://developercommunity.visualstudio.com/t/unable-to-remove-warning-for-cxx17-deprecated-feat/273815
            // PublicDefinitions.Add("_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS=1");
            // PublicDefinitions.Add("_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING=1");
            PrivateDefinitions.Add("ROSIDL_TYPESUPPORT_CPP_BUILDING_DLL=1");
            PrivateDefinitions.Add("ROSIDL_TYPESUPPORT_INTROSPECTION_CPP_BUILDING_DLL=1");
        }

        PublicDefinitions.Add("RCLCPP_INTRA_PROCESS_DISABLED=1");

        // rclcpp and all modules that depend on it must enable exceptions.
        bEnableExceptions = true;

        AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");
        AddEngineThirdPartyPrivateStaticDependencies(Target, "zlib");
        AddEngineThirdPartyPrivateStaticDependencies(Target, "UElibPNG");
        AddEngineThirdPartyPrivateDynamicDependencies(Target, "Python3");
    }
}
