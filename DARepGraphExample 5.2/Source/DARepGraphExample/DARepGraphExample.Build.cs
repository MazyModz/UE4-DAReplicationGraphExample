// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DARepGraphExample : ModuleRules
{
	public DARepGraphExample(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "HeadMountedDisplay" });
        PrivateDependencyModuleNames.AddRange(new string[] { "ReplicationGraph" });

        bool bTargetConfig = Target.Configuration != UnrealTargetConfiguration.Shipping && Target.Configuration != UnrealTargetConfiguration.Test;
        if (Target.bBuildDeveloperTools || bTargetConfig)
        {
            PrivateDependencyModuleNames.Add("GameplayDebugger");
            PublicDefinitions.Add("WITH_GAMEPLAY_DEBUGGER=1");
        }
        else
        {
            PublicDefinitions.Add("WITH_GAMEPLAY_DEBUGGER=0");
        }
    }
}
