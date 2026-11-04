using UnrealBuildTool;

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
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"UnrealEd",
			"ContentBrowser",
			"ContentBrowserData",
			"AssetRegistry",
		});
	}
}
