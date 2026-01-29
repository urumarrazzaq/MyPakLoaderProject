// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class PakCreator : ModuleRules
{
	public PakCreator(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Projects",
                "DesktopPlatform",
				"InputCore",
				"UnrealEd",
				"Engine",
				"Slate",
				"SlateCore",
				"DesktopPlatform",
				"ApplicationCore",
                "CoreUObject"
            }
		);

		if ((Target.Version.MajorVersion == 4) ||
			(Target.Version.MajorVersion == 5 && Target.Version.MinorVersion == 0))
		{
            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
					"EditorStyle"
                }
            );
        }

		if (Target.Version.MajorVersion == 5)
		{
            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
                    "DeveloperToolSettings",
					"CoreUObject"
                }
            );
        }
	}
}
