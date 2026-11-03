// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoROS.h"
#include "TempoROSSettings.h"

#include "Misc/AutomationTest.h"

#include "rcl/context.h"
#include "rcl/init_options.h"
#include "rcl/localhost.h"
#include "rclcpp/contexts/default_context.hpp"

// The discovery range setting reaches the middleware. Two checks: the value InitROS writes to ROS_LOCALHOST_ONLY, for
// both values of the setting (a pure function, no ROS involved); and the live context the module built at startup
// agrees with the current setting, both the variable rcl reads and what rcl_init resolved into the global context.
// It never re-inits ROS: shutting rclcpp down mid-run leaves every node already made (RealisticSensorsROS keeps one
// for the process) on a dead context, and tearing such nodes down has hung UE at world cleanup or corrupted the heap
// at exit. Needs a working rclcpp (the RosIsInitialized check).
//   Automation RunTests Tempo.ROS.Discovery   (from the editor console)

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoROSDiscoveryRangeTest,
	"Tempo.ROS.Discovery.RangeFromSettings", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTempoROSDiscoveryRangeTest::RunTest(const FString& Parameters)
{
	// What InitROS writes for each value.
	TestEqual(TEXT("Localhost: ROS_LOCALHOST_ONLY value"), FString(FTempoROSModule::LocalhostOnlyEnvValue(EROSDiscoveryRange::Localhost)), FString(TEXT("1")));
	TestEqual(TEXT("Subnet: ROS_LOCALHOST_ONLY value"), FString(FTempoROSModule::LocalhostOnlyEnvValue(EROSDiscoveryRange::Subnet)), FString(TEXT("0")));

	// The live context agrees with the current setting.
	if (!TestTrue(TEXT("ROS is initialized"), FTempoROSModule::IsROSInitialized()))
	{
		return false;
	}
	const EROSDiscoveryRange Range = GetDefault<UTempoROSSettings>()->GetROSDiscoveryRange();
	const FString Name = Range == EROSDiscoveryRange::Localhost ? TEXT("Localhost") : TEXT("Subnet");
	const rmw_localhost_only_t Expected = Range == EROSDiscoveryRange::Localhost ? RMW_LOCALHOST_ONLY_ENABLED : RMW_LOCALHOST_ONLY_DISABLED;

	// The environment variable, read the way rcl reads it (ROS_LOCALHOST_ONLY: the bundled rclcpp is Humble).
	rmw_localhost_only_t FromEnvironment = RMW_LOCALHOST_ONLY_DEFAULT;
	TestEqual(FString::Printf(TEXT("%s: rcl_get_localhost_only succeeds"), *Name), static_cast<int32>(rcl_get_localhost_only(&FromEnvironment)), static_cast<int32>(RCL_RET_OK));
	TestEqual(FString::Printf(TEXT("%s: ROS_LOCALHOST_ONLY as configured"), *Name), static_cast<int32>(FromEnvironment), static_cast<int32>(Expected));

	// What rcl_init resolved into the global context from that variable.
	const std::shared_ptr<rcl_context_t> Context = rclcpp::contexts::get_global_default_context()->get_rcl_context();
	const rcl_init_options_t* InitOptions = rcl_context_get_init_options(Context.get());
	if (!TestNotNull(FString::Printf(TEXT("%s: context init options"), *Name), InitOptions))
	{
		return false;
	}
	const rmw_init_options_t* RMWInitOptions = rcl_init_options_get_rmw_init_options(const_cast<rcl_init_options_t*>(InitOptions));
	if (!TestNotNull(FString::Printf(TEXT("%s: context rmw init options"), *Name), RMWInitOptions))
	{
		return false;
	}
	TestEqual(FString::Printf(TEXT("%s: context localhost_only as configured"), *Name), static_cast<int32>(RMWInitOptions->localhost_only), static_cast<int32>(Expected));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
