#pragma once

#include "ToolsetRegistry/ToolsetJsonConverter.h"

/**
 * Writes an FContentViewValue as a plain JSON scalar (a number, a bool, a string, or null when unset), so a view's rows
 * reach an agent typed, not as an object of every field.
 */
class FContentViewValueConverter : public UE::ToolsetRegistry::FToolsetJsonConverter
{
public:
	virtual FString GetName() const override;
	virtual bool CanConvertProperty(TNotNull<const FProperty*> Property) override;
	virtual TSharedPtr<FJsonObject> PropertyToJsonSchema(TNotNull<const FProperty*> Property) override;
	virtual TSharedPtr<FJsonValue> PropertyToDefault(TNotNull<const FProperty*> Property, const FString& DefaultString) override;
	virtual TSharedPtr<FJsonValue> PropertyToJsonData(TNotNull<FProperty*> Property, const void* Value) override;
	virtual bool JsonDataToProperty(const TSharedPtr<FJsonValue>& JsonValue, TNotNull<FProperty*> Property,
		void* OutValue, UObject* Outer) override;

	/** The JSON a cell becomes; shared with the tests. */
	static TSharedPtr<FJsonValue> ToJson(const struct FContentViewValue& Value);
};
