// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoROS.h"

#include "TempoROSAllocator.h"
#include "TempoROSSettings.h"

#if WITH_EDITOR
#include "IHotReload.h"
#endif

#define LOCTEXT_NAMESPACE "FTempoROSModule"

#include "rclcpp/utilities.hpp"

#include <atomic>

DEFINE_LOG_CATEGORY(LogTempoROS);

namespace
{
	// Static, not module members, because IsROSInitialized is static and may be called off the game thread.
	std::atomic<bool> bROSInitialized{false};
	std::atomic<bool> bROSInitFailed{false};
	std::atomic<bool> bLoggedROSUnavailable{false};

	const TCHAR* const ROSUnavailableHint = TEXT("On Windows the usual cause is the two OpenSSL 1.1 DLLs missing beside "
		"the rclcpp binaries; run tools/temporos/post_setup_windows.sh from the RealisticSensorsTestbed root (README).");

	void LogROSUnavailableOnce(const FString& Cause)
	{
		if (!bLoggedROSUnavailable.exchange(true))
		{
			UE_LOG(LogTempoROS, Error, TEXT("TempoROS is unavailable (IsROSInitialized is false): %s %s"), *Cause, ROSUnavailableHint);
		}
	}
}

void SetEnvironmentVar(const TCHAR* VariableName, const TCHAR* Value)
{
#if PLATFORM_WINDOWS
	// On Windows only, SetEnvironmentVar does not seem to work properly, but this does.
	_putenv_s(TCHAR_TO_UTF8(VariableName), TCHAR_TO_UTF8(Value));
#else
	FPlatformMisc::SetEnvironmentVar(VariableName, Value);
#endif
}

void SetAmentPrefixPath()
{
	// Find the rclcpp module directory. The module is not loaded yet, so we can't use FModuleManager.
	const FString ProjectPath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*FPaths::ProjectDir());
	TArray<FString> PossibleTargets;
#if WITH_EDITOR
	// With the Editor we simply look for the Build.cs file
	IFileManager::Get().FindFilesRecursive(PossibleTargets, *ProjectPath, TEXT("rclcpp.Build.cs"), true, false);
	for (FString& PossibleTarget : PossibleTargets)
	{
		PossibleTarget = FPaths::GetPath(PossibleTarget);
	}
#else
	// In the packaged game we search for a directory called "rclcpp" within a directory called "ThirdParty"
	IFileManager::Get().FindFilesRecursive(PossibleTargets, *ProjectPath, TEXT("rclcpp"), false, true);
	for (auto PossibleTargetIt = PossibleTargets.CreateIterator(); PossibleTargetIt; ++PossibleTargetIt)
	{
		if (!FPaths::GetPath(*PossibleTargetIt).EndsWith(TEXT("ThirdParty")))
		{
			PossibleTargetIt.RemoveCurrent();
		}
	}
#endif
	checkf(PossibleTargets.Num() == 1, TEXT("Expected to find exactly one rclcpp module"));
	const FString rclcppDir = PossibleTargets[0];

	// Find the Binaries and Libraries directories within rclcpp
#if PLATFORM_MAC
	const FString PlatformDir(TEXT("Mac"));
#elif PLATFORM_WINDOWS
	const FString PlatformDir(TEXT("Windows"));
#elif PLATFORM_LINUX
	const FString PlatformDir(TEXT("Linux"));
#else
	checkf(false, TEXT("Unsupported platform"));
#endif
#if PLATFORM_WINDOWS
	FString LibDir = FPaths::Combine(rclcppDir, "Binaries", PlatformDir);
#else
	FString LibDir = FPaths::Combine(rclcppDir, "Libraries", PlatformDir);
#endif
	FPaths::CollapseRelativeDirectories(LibDir);
	checkf(FPaths::DirectoryExists(*LibDir), TEXT("rclcpp library directory %s did not exist"), *LibDir);

	SetEnvironmentVar(TEXT("AMENT_PREFIX_PATH"), *LibDir);
}

void FTempoROSModule::StartupModule()
{
	// Route rclcpp's default std::pmr allocations through Unreal's allocator before anything in rclcpp runs.
	SetUnrealDefaultMemoryResource();

	SetAmentPrefixPath();

	InitROS();

#if WITH_EDITOR
	IHotReloadModule::Get().OnModuleCompilerStarted().AddLambda([this](bool bIsAsyncCompile)
	{
		ShutdownROS();
	});

	IHotReloadModule::Get().OnModuleCompilerFinished().AddLambda([this](const FString&, ECompilationResult::Type, bool)
	{
		InitROS();
	});
#endif

	GetMutableDefault<UTempoROSSettings>()->TempoROSSettingsChangedEvent.AddRaw(this, &FTempoROSModule::InitROS);
}

void FTempoROSModule::ShutdownModule()
{
	ShutdownROS();
}

bool FTempoROSModule::IsROSInitialized()
{
	// NOTE: rclcpp::ok() THROWS, rather than returning false, when rclcpp::init failed (measured 2026-10-21
	// with libssl-1_1-x64.dll held out: an unhandled 0xe06d7363 from rclcpp.dll ended the editor). So it is
	// only asked once init succeeded, and even then a throw is caught: an escaping exception kills the process.
	if (bROSInitFailed)
	{
		LogROSUnavailableOnce(TEXT("rclcpp::init failed at startup (see the 'Failed to initialize rclcpp' error above)."));
		return false;
	}
	if (!bROSInitialized)
	{
		// Not started yet, or shut down (e.g. around a hot reload): not a failure.
		return false;
	}
	try
	{
		// rclcpp::ok() reports whether the global default context is valid: false after rclcpp::shutdown,
		// and after rclcpp's own signal handler shuts the context down.
		return rclcpp::ok();
	}
	catch (const std::exception& E)
	{
		LogROSUnavailableOnce(FString::Printf(TEXT("rclcpp::ok() threw '%s'."), UTF8_TO_TCHAR(E.what())));
	}
	catch (...)
	{
		LogROSUnavailableOnce(TEXT("rclcpp::ok() threw a non-std exception."));
	}
	return false;
}

void FTempoROSModule::InitROS()
{
	if (bROSInitialized)
	{
		ShutdownROS();
	}

	// RMW_IMPLEMENTATION
	const UTempoROSSettings* TempoROSSettings = GetDefault<UTempoROSSettings>();
	switch (const ERMWImplementation RMWImplementation = TempoROSSettings->GetRMWImplementation())
	{
	case ERMWImplementation::CycloneDDS:
		{
			SetEnvironmentVar(TEXT("RMW_IMPLEMENTATION"), TEXT("rmw_cyclonedds_cpp"));
			break;
		}
	case ERMWImplementation::FastRTPS:
		{
			SetEnvironmentVar(TEXT("RMW_IMPLEMENTATION"), TEXT("rmw_fastrtps_cpp"));
			break;
		}
	}

#if PLATFORM_LINUX
	// CYCLONEDDS_URI
	const FString CycloneDDS_URI = TempoROSSettings->GetCycloneDDS_URI();
	if (!CycloneDDS_URI.IsEmpty())
	{
		if (FPaths::FileExists(CycloneDDS_URI))
		{
			SetEnvironmentVar(TEXT("CYCLONEDDS_URI"), *FString::Printf(TEXT("file://%s"), *TempoROSSettings->GetCycloneDDS_URI()));
		}
		else
		{
			UE_LOG(LogTempoROS, Error, TEXT("Configured CycloneDDS URI file not found: %s"), *CycloneDDS_URI);
		}
	}
#endif

	// ROS_DOMAIN_ID
	SetEnvironmentVar(TEXT("ROS_DOMAIN_ID"), *FString::FromInt(TempoROSSettings->GetROSDomainID()));

	try
	{
		rclcpp::init(0, nullptr);
	}
	catch (const std::exception& E)
	{
		// Leave bROSInitialized false. Otherwise the failure is silent here and only resurfaces later,
		// as a confusing error from every UTempoROSNode::Create call against an uninitialized context.
		UE_LOG(LogTempoROS, Error, TEXT("Failed to initialize rclcpp with error: %s. TempoROS will be unavailable. %s"), UTF8_TO_TCHAR(E.what()), ROSUnavailableHint);
		bROSInitFailed = true;
		return;
	}

	bROSInitFailed = false;
	bLoggedROSUnavailable = false;
	bROSInitialized = true;
}

void FTempoROSModule::ShutdownROS()
{
	if (bROSInitialized)
	{
		try
		{
			rclcpp::shutdown();
		}
		catch (const std::exception& E)
		{
			UE_LOG(LogTempoROS, Error, TEXT("Failed to shut down rclcpp with error: %s"), UTF8_TO_TCHAR(E.what()));
		}

		bROSInitialized = false;
	}
	else
	{
		UE_LOG(LogTempoROS, Warning, TEXT("ShutdownROS called when ROS was not initialized"));
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FTempoROSModule, TempoROS)
