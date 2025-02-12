// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SphereCaptuer : ModuleRules
{
	public SphereCaptuer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput" });
	}
}
