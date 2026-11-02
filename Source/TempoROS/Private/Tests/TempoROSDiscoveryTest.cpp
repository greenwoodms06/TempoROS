// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoROS.h"
#include "TempoROSSettings.h"

#include "Misc/AutomationTest.h"

#include "rcl/context.h"
#include "rcl/init_options.h"
#include "rcl/localhost.h"
#include "rclcpp/contexts/default_context.hpp"

// The discovery range setting reaches the middleware: after the module's InitROS, the variable rcl reads and the
// context rcl_init built both say what UTempoROSSettings says. Needs a working rclcpp (the RosIsInitialized check).
//   Automation RunTests Tempo.ROS.Discovery   (from the editor console)

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoROSDiscoveryRangeTest,
	"Tempo.ROS.Discovery.RangeFromSettings", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTempoROSDiscoveryRangeTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("ROS is initialized"), FTempoROSModule::IsROSInitialized()))
	{
		return false;
	}

	const EROSDiscoveryRange Range = GetDefault<UTempoROSSettings>()->GetROSDiscoveryRange();
	const rmw_localhost_only_t Expected = Range == EROSDiscoveryRange::Localhost ? RMW_LOCALHOST_ONLY_ENABLED : RMW_LOCALHOST_ONLY_DISABLED;

	// The environment variable, read the way rcl reads it (ROS_LOCALHOST_ONLY: the bundled rclcpp is Humble).
	rmw_localhost_only_t FromEnvironment = RMW_LOCALHOST_ONLY_DEFAULT;
	TestEqual(TEXT("rcl_get_localhost_only succeeds"), static_cast<int32>(rcl_get_localhost_only(&FromEnvironment)), static_cast<int32>(RCL_RET_OK));
	TestEqual(TEXT("ROS_LOCALHOST_ONLY as configured"), static_cast<int32>(FromEnvironment), static_cast<int32>(Expected));

	// What rcl_init resolved into the global context from that variable.
	const std::shared_ptr<rcl_context_t> Context = rclcpp::contexts::get_global_default_context()->get_rcl_context();
	const rcl_init_options_t* InitOptions = rcl_context_get_init_options(Context.get());
	if (!TestNotNull(TEXT("Context init options"), InitOptions))
	{
		return false;
	}
	const rmw_init_options_t* RMWInitOptions = rcl_init_options_get_rmw_init_options(const_cast<rcl_init_options_t*>(InitOptions));
	if (!TestNotNull(TEXT("Context rmw init options"), RMWInitOptions))
	{
		return false;
	}
	TestEqual(TEXT("Context localhost_only as configured"), static_cast<int32>(RMWInitOptions->localhost_only), static_cast<int32>(Expected));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
