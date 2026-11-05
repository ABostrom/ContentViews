#pragma once

#include "CoreMinimal.h"

class SWidget;
class UContentViewBase;

/**
 * What the Content Views tab shows for a view of an archetype other than assets. The module that owns the archetype
 * registers a widget for its class (the TEDS module registers Scene Cost's), so this module never depends on it.
 */
namespace UE::ContentViews
{
	using FMakeViewWidget = TFunction<TSharedRef<SWidget>(UContentViewBase& View)>;

	/** The widget the tab shows for a view of ViewClass or a subclass. Replaces an earlier registration of the class. */
	CONTENTVIEWS_API void RegisterViewWidget(const UClass& ViewClass, FMakeViewWidget Make);
	CONTENTVIEWS_API void UnregisterViewWidget(const UClass& ViewClass);

	/**
	 * What the tab shows for View: an asset view's picker, the widget registered for its class (the nearest registered
	 * class up its chain), or a line saying nothing draws it.
	 */
	CONTENTVIEWS_API TSharedRef<SWidget> MakeViewWidget(UContentViewBase& View);
}
