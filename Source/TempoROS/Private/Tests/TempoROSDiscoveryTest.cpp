// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoROS.h"
#include "TempoROSSettings.h"

#include "Misc/AutomationTest.h"
#include "UObject/EnumProperty.h"

#include "rcl/context.h"
#include "rcl/init_options.h"
#include "rcl/localhost.h"
#include "rclcpp/contexts/default_context.hpp"

// The discovery range setting reaches the middleware: for each value of the setting, after the module re-inits ROS
// (the settings-changed path), the variable rcl reads and the context rcl_init built both say what UTempoROSSettings
// says. The original setting is put back and ROS re-initialized with it at the end. Needs a working rclcpp (the
// RosIsInitialized check).
//   Automation RunTests Tempo.ROS.Discovery   (from the editor console)

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// Writes the private ROSDiscoveryRange config property the way the settings editor does, through reflection,
	// then re-inits ROS the way an edit of it does (TempoROSSettingsChangedEvent, which runs the module's InitROS).
	bool SetDiscoveryRangeAndReinit(FAutomationTestBase& Test, EROSDiscoveryRange Range)
	{
		UTempoROSSettings* Settings = GetMutableDefault<UTempoROSSettings>();
		const FEnumProperty* Property = FindFProperty<FEnumProperty>(UTempoROSSettings::StaticClass(), TEXT("ROSDiscoveryRange"));
		if (!Test.TestNotNull(TEXT("ROSDiscoveryRange property"), Property))
		{
			return false;
		}
		Property->GetUnderlyingProperty()->SetIntPropertyValue(Property->ContainerPtrToValuePtr<void>(Settings), static_cast<int64>(Range));
		Settings->TempoROSSettingsChangedEvent.Broadcast();
		return Test.TestEqual(TEXT("Setting written"), static_cast<int32>(Settings->GetROSDiscoveryRange()), static_cast<int32>(Range));
	}

	void CheckDiscoveryRange(FAutomationTestBase& Test, EROSDiscoveryRange Range)
	{
		const FString Name = Range == EROSDiscoveryRange::Localhost ? TEXT("Localhost") : TEXT("Subnet");
		if (!Test.TestTrue(FString::Printf(TEXT("%s: ROS is initialized"), *Name), FTempoROSModule::IsROSInitialized()))
		{
			return;
		}
		const rmw_localhost_only_t Expected = Range == EROSDiscoveryRange::Localhost ? RMW_LOCALHOST_ONLY_ENABLED : RMW_LOCALHOST_ONLY_DISABLED;

		// The environment variable, read the way rcl reads it (ROS_LOCALHOST_ONLY: the bundled rclcpp is Humble).
		rmw_localhost_only_t FromEnvironment = RMW_LOCALHOST_ONLY_DEFAULT;
		Test.TestEqual(FString::Printf(TEXT("%s: rcl_get_localhost_only succeeds"), *Name), static_cast<int32>(rcl_get_localhost_only(&FromEnvironment)), static_cast<int32>(RCL_RET_OK));
		Test.TestEqual(FString::Printf(TEXT("%s: ROS_LOCALHOST_ONLY as configured"), *Name), static_cast<int32>(FromEnvironment), static_cast<int32>(Expected));

		// What rcl_init resolved into the global context from that variable.
		const std::shared_ptr<rcl_context_t> Context = rclcpp::contexts::get_global_default_context()->get_rcl_context();
		const rcl_init_options_t* InitOptions = rcl_context_get_init_options(Context.get());
		if (!Test.TestNotNull(FString::Printf(TEXT("%s: context init options"), *Name), InitOptions))
		{
			return;
		}
		const rmw_init_options_t* RMWInitOptions = rcl_init_options_get_rmw_init_options(const_cast<rcl_init_options_t*>(InitOptions));
		if (!Test.TestNotNull(FString::Printf(TEXT("%s: context rmw init options"), *Name), RMWInitOptions))
		{
			return;
		}
		Test.TestEqual(FString::Printf(TEXT("%s: context localhost_only as configured"), *Name), static_cast<int32>(RMWInitOptions->localhost_only), static_cast<int32>(Expected));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoROSDiscoveryRangeTest,
	"Tempo.ROS.Discovery.RangeFromSettings", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FTempoROSDiscoveryRangeTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("ROS is initialized"), FTempoROSModule::IsROSInitialized()))
	{
		return false;
	}

	const EROSDiscoveryRange Original = GetDefault<UTempoROSSettings>()->GetROSDiscoveryRange();
	for (const EROSDiscoveryRange Range : { EROSDiscoveryRange::Localhost, EROSDiscoveryRange::Subnet })
	{
		if (SetDiscoveryRangeAndReinit(*this, Range))
		{
			CheckDiscoveryRange(*this, Range);
		}
	}

	SetDiscoveryRangeAndReinit(*this, Original);
	TestTrue(TEXT("ROS is initialized again with the original setting"), FTempoROSModule::IsROSInitialized());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
