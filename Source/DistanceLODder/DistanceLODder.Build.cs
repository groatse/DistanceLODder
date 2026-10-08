// Copyright (c) 2026 groatse. Licensed under the MIT License.

using UnrealBuildTool;

public class DistanceLODder : ModuleRules
{
	public DistanceLODder(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core" });

		PrivateDependencyModuleNames.AddRange(new string[] { "CoreUObject", "Engine" });
	}
}
