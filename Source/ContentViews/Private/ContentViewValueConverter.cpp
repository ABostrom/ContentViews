#include "ContentViewValueConverter.h"

#include "ContentViewValue.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/UnrealType.h"

FString FContentViewValueConverter::GetName() const
{
	return TEXT("ContentViewValueConverter");
}

bool FContentViewValueConverter::CanConvertProperty(TNotNull<const FProperty*> Property)
{
	const FStructProperty* Struct = CastField<FStructProperty>(Property);
	return Struct && Struct->Struct == FContentViewValue::StaticStruct();
}

TSharedPtr<FJsonObject> FContentViewValueConverter::PropertyToJsonSchema(TNotNull<const FProperty*> Property)
{
	TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Types;
	for (const TCHAR* Type : {TEXT("string"), TEXT("number"), TEXT("boolean"), TEXT("null")})
	{
		Types.Add(MakeShared<FJsonValueString>(Type));
	}
	Schema->SetArrayField(TEXT("type"), Types);
	Schema->SetStringField(TEXT("description"),
		TEXT("A column's value: a number, a bool or a string (an asset or class path for a reference). Null when the asset has none."));
	return Schema;
}

TSharedPtr<FJsonValue> FContentViewValueConverter::PropertyToDefault(TNotNull<const FProperty*> Property,
	const FString& DefaultString)
{
	return MakeShared<FJsonValueNull>();
}

TSharedPtr<FJsonValue> FContentViewValueConverter::PropertyToJsonData(TNotNull<FProperty*> Property, const void* Value)
{
	return ToJson(*static_cast<const FContentViewValue*>(Value));
}

bool FContentViewValueConverter::JsonDataToProperty(const TSharedPtr<FJsonValue>& JsonValue, TNotNull<FProperty*> Property,
	void* OutValue, UObject* Outer)
{
	FContentViewValue& Value = *static_cast<FContentViewValue*>(OutValue);
	if (!JsonValue.IsValid() || JsonValue->IsNull())
	{
		Value = FContentViewValue();
	}
	else if (JsonValue->Type == EJson::Number)
	{
		Value = FContentViewValue::OfNumber(JsonValue->AsNumber());
	}
	else if (JsonValue->Type == EJson::Boolean)
	{
		Value = FContentViewValue::OfBool(JsonValue->AsBool());
	}
	else
	{
		Value = FContentViewValue::OfString(JsonValue->AsString());
	}
	return true;
}

TSharedPtr<FJsonValue> FContentViewValueConverter::ToJson(const FContentViewValue& Value)
{
	if (!Value.bIsSet)
	{
		return MakeShared<FJsonValueNull>();
	}
	switch (Value.Type)
	{
	case EContentViewValueType::Number:
		return MakeShared<FJsonValueNumber>(Value.Number);
	case EContentViewValueType::Bool:
		return MakeShared<FJsonValueBoolean>(Value.bBool);
	default:
		return MakeShared<FJsonValueString>(Value.String);
	}
}
