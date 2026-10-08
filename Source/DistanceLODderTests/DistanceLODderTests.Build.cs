// Copyright (c) 2026 groatse. Licensed under the MIT License.

using UnrealBuildTool;

public class DistanceLODderTests : ModuleRules
{
	public DistanceLODderTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DistanceLODder",
			"FunctionalTesting",
			"MeshDescription",
			"RenderCore",
			"StaticMeshDescription",
		});
	}
}
