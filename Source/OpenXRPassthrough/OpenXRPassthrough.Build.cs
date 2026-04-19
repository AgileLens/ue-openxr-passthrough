// Copyright Agile Lens. All Rights Reserved.

using UnrealBuildTool;

public class OpenXRPassthrough : ModuleRules
{
	public OpenXRPassthrough(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"OpenXRHMD",
			"HeadMountedDisplay",
		});
	}
}
