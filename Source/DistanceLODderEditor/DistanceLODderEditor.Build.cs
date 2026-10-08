// Copyright (c) 2026 groatse. Licensed under the MIT License.

using UnrealBuildTool;

public class DistanceLODderEditor : ModuleRules
{
	public DistanceLODderEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core" });

		PrivateDependencyModuleNames.AddRange(new string[] { "CoreUObject", "Engine", "UnrealEd", "DistanceLODder" });
	}
}
