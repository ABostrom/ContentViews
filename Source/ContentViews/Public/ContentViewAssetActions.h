#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"

class SWidget;

/** What a row of a view does when it is used: the same in the legacy tab and in a TEDS source. */
namespace UE::ContentViews
{
	/** Opens each asset's editor, as a double-click in the Content Browser does. */
	CONTENTVIEWS_API void OpenAssets(TConstArrayView<FAssetData> Assets);

	/**
	 * The right-click menu for a selection: the Content Browser's asset menu with every context and command list it passes,
	 * and Edit, Browse to Asset and Copy Reference in front.
	 */
	CONTENTVIEWS_API TSharedPtr<SWidget> MakeAssetContextMenu(const TArray<FAssetData>& Assets);
}
