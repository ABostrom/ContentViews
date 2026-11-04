using UnrealBuildTool;

// Editor only: views are browsing tools and nothing here reaches a cooked game.
public class ContentViews : ModuleRules
{
	public ContentViews(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"AssetRegistry",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"InputCore",
			"UnrealEd",
			"ContentBrowser",
			"ContentBrowserData",
			"AssetTools",
			"AssetDefinition",
			"ToolMenus",
			"ToolWidgets",
			"WorkspaceMenuStructure",
			"Json",
			"JsonUtilities",
			"ToolsetRegistry",
		});
	}
}
