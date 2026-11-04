#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ContentViewsSettings.generated.h"

/** Which backend shows the views. Both read the same view assets. */
UENUM()
enum class EContentViewsBackend : uint8
{
	/** The Tools > Content Views tab and the Content Browser filters, built on the stock asset view. */
	Legacy,

	/** A Content Source per view in the Content Browser's source bar, drawn by TEDS (experimental). */
	Teds UMETA(DisplayName = "TEDS"),

	Both,
};

/** Project Settings > Plugins > Content Views. */
UCLASS(config = Editor, defaultconfig, meta = (DisplayName = "Content Views"))
class CONTENTVIEWS_API UContentViewsSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(config, EditAnywhere, Category = "Content Views")
	EContentViewsBackend Backend = EContentViewsBackend::Legacy;

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	static bool IsLegacyActive();
	static bool IsTedsActive();
};
