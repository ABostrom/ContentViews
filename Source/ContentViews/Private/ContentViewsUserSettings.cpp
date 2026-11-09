#include "ContentViewsUserSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ContentViewsUserSettings)

bool UContentViewsUserSettings::IsHidden(const FSoftObjectPath& View)
{
	return GetDefault<UContentViewsUserSettings>()->HiddenViews.Contains(View);
}

void UContentViewsUserSettings::SetHidden(const FSoftObjectPath& View, bool bHidden)
{
	UContentViewsUserSettings* Settings = GetMutableDefault<UContentViewsUserSettings>();
	if (bHidden == Settings->HiddenViews.Contains(View))
	{
		return;
	}
	if (bHidden)
	{
		Settings->HiddenViews.Add(View);
	}
	else
	{
		Settings->HiddenViews.Remove(View);
	}
	Settings->SaveConfig();
	OnHiddenViewsChanged().Broadcast();
}

FSimpleMulticastDelegate& UContentViewsUserSettings::OnHiddenViewsChanged()
{
	static FSimpleMulticastDelegate Changed;
	return Changed;
}

#if WITH_EDITOR
void UContentViewsUserSettings::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	Super::PostEditChangeProperty(Event);
	// Cleared or edited in Editor Preferences: every list follows.
	OnHiddenViewsChanged().Broadcast();
}
#endif
