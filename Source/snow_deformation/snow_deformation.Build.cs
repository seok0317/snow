// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class snow_deformation : ModuleRules
{
	public snow_deformation(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate","RenderCore", "RHI", "Niagara"
        });

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"snow_deformation",
			"snow_deformation/Variant_Platforming",
			"snow_deformation/Variant_Platforming/Animation",
			"snow_deformation/Variant_Combat",
			"snow_deformation/Variant_Combat/AI",
			"snow_deformation/Variant_Combat/Animation",
			"snow_deformation/Variant_Combat/Gameplay",
			"snow_deformation/Variant_Combat/Interfaces",
			"snow_deformation/Variant_Combat/UI",
			"snow_deformation/Variant_SideScrolling",
			"snow_deformation/Variant_SideScrolling/AI",
			"snow_deformation/Variant_SideScrolling/Gameplay",
			"snow_deformation/Variant_SideScrolling/Interfaces",
			"snow_deformation/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
