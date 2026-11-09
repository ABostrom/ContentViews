#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ContentViewsUserSettings.generated.h"

/**
 * Editor Preferences > Plugins > Content Views: this user's own choices, saved in EditorPerProjectUserSettings, so they
 * are never shared and never written into a view asset. The views hidden here leave every list of views (the tab, the
 * Content Browser's filters, the TEDS Content Sources, ListViews) until they are unhidden.
 */
UCLASS(config = EditorPerProjectUserSettings, meta = (DisplayName = "Content Views"))
class CONTENTVIEWS_API UContentViewsUserSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** The views this user has hidden, by asset path. Clear the list to see every view again. */
	UPROPERTY(config, EditAnywhere, Category = "Content Views", meta = (AllowedClasses = "/Script/ContentViews.ContentViewBase"))
	TArray<FSoftObjectPath> HiddenViews;

	virtual FName GetContainerName() const override { return TEXT("Editor"); }
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	static bool IsHidden(const FSoftObjectPath& View);

	/** Hides or unhides View for this user and saves the choice. */
	static void SetHidden(const FSoftObjectPath& View, bool bHidden);

	/** Something listing views should list them again. */
	static FSimpleMulticastDelegate& OnHiddenViewsChanged();

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
#endif
};
