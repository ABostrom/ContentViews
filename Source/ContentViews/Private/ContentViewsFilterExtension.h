#pragma once

#include "CoreMinimal.h"
#include "ContentBrowserFrontEndFilterExtension.h"
#include "ContentViewsFilterExtension.generated.h"

/**
 * One Content Browser filter per view asset, under the Content Views category: it passes the view's rows that meet the
 * view's filters, so any Content Browser can show a view's selection. Built when a Content Browser builds its filters.
 */
UCLASS()
class UContentViewsFilterExtension : public UContentBrowserFrontEndFilterExtension
{
	GENERATED_BODY()

public:
	virtual void AddFrontEndFilterExtensions(TSharedPtr<class FFrontendFilterCategory> DefaultCategory,
		TArray<TSharedRef<class FFrontendFilter>>& InOutFilterList) const override;
};
