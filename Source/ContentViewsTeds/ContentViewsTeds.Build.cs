using System.IO;
using UnrealBuildTool;

// The TEDS backend. Every TEDS call of the plugin is in this module, so the legacy module never depends on TEDS, and an
// engine upgrade that moves TEDS's experimental API breaks one module.
public class ContentViewsTeds : ModuleRules
{
	public ContentViewsTeds(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// TEDS is experimental by design here, and one of its headers (TypedElementTypeInfoColumns.h) does not compile
		// with the experimental-API check on: its UE_EXPERIMENTAL attribute sits where a USTRUCT cannot take one.
		bValidateExperimentalApi = false;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"ContentViews",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Engine",
			"DeveloperSettings",
			"InputCore",
			"ToolMenus",
			"Slate",
			"SlateCore",
			"AssetRegistry",
			"ContentBrowser",
			"ToolWidgets",
			"UnrealEd",
			"TypedElementFramework",
			"TedsUI",
			"TedsAlerts",
			"TedsAssetData",
			"TedsQueryStack",
			"TedsTableViewer",
			"ToolsetRegistry",
			"Json",
		});

		// The plugin's test row class, shared with the legacy module's tests.
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "..", "ContentViews", "Private", "Tests"));
	}
}
