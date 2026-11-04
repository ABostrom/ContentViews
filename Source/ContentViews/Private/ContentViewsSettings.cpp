#include "ContentViewsSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ContentViewsSettings)

bool UContentViewsSettings::IsLegacyActive()
{
	return GetDefault<UContentViewsSettings>()->Backend != EContentViewsBackend::Teds;
}

bool UContentViewsSettings::IsTedsActive()
{
	return GetDefault<UContentViewsSettings>()->Backend != EContentViewsBackend::Legacy;
}
