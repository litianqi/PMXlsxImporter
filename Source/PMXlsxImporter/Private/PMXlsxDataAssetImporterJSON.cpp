// Copyright Tianqi Li. All Rights Reserved.


#include "PMXlsxDataAssetImporterJSON.h"

#include "PMXlsxImporterSettingsEntry.h"
#include "PMXlsxMetadata.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	const TCHAR* JSONTypeToString(const EJson InType)
	{
		switch(InType)
		{
		case EJson::None:
			return TEXT("None");
		case EJson::Null:
			return TEXT("Null");
		case EJson::String:
			return TEXT("String");
		case EJson::Number:
			return TEXT("Number");
		case EJson::Boolean:
			return TEXT("Boolean");
		case EJson::Array:
			return TEXT("Array");
		case EJson::Object:
			return TEXT("Object");
		default:
			return TEXT("Unknown");
		}
	}
}

FPMXlsxDataAssetImporterJSON::FPMXlsxDataAssetImporterJSON(UPMXlsxDataAsset& InDataAsset, const TSharedRef<FJsonObject>& InJSONData, TArray<FString>& OutProblems)
	: DataAsset(&InDataAsset)
	, JSONData(InJSONData)
	, ImportProblems(OutProblems)
{
}

FPMXlsxDataAssetImporterJSON::~FPMXlsxDataAssetImporterJSON()
{
}

bool FPMXlsxDataAssetImporterJSON::ReadAsset()
{
	if (JSONData->Values.IsEmpty())
	{
		ImportProblems.Add(TEXT("Input data is empty."));
		return false;
	}

	// Get row name
	const FString RowKey = TEXT("Name");
	const FName RowName = DataTableUtils::MakeValidName(JSONData->GetStringField(RowKey));

	// Detect any extra fields within the data for this row
	if (!DataAsset->bIgnoreExtraFields)
	{
		TArray<FString> TempPropertyImportNames;
		for (const TPair<FString, TSharedPtr<FJsonValue>>& ParsedPropertyKeyValuePair : JSONData->Values)
		{
			if (ParsedPropertyKeyValuePair.Key == RowKey)
			{
				// Skip the row name, as that doesn't match a property
				continue;
			}

			FName PropName = DataTableUtils::MakeValidName(ParsedPropertyKeyValuePair.Key);
			FProperty* ColumnProp = FindFProperty<FProperty>(DataAsset->GetClass(), PropName);
			for (TFieldIterator<FProperty> It(DataAsset->GetClass()); It && !ColumnProp; ++It)
			{
				DataTableUtils::GetPropertyImportNames(*It, TempPropertyImportNames);
				ColumnProp = TempPropertyImportNames.Contains(ParsedPropertyKeyValuePair.Key) ? *It : nullptr;
			}

			if (!ColumnProp)
			{
				ImportProblems.Add(FString::Printf(TEXT("Property '%s' on row '%s' cannot be found in class '%s'."), *PropName.ToString(), *RowName.ToString(), *DataAsset->GetClass()->GetName()));
			}
		}
	}

	if (ReadStruct(JSONData, DataAsset->GetClass(), RowName, DataAsset))
	{
		DataAsset->Modify(true);
		return true;
	}

	return false;
}

bool FPMXlsxDataAssetImporterJSON::ReadStruct(const TSharedRef<FJsonObject>& InParsedObject, UStruct* InStruct, const FName InRowName, void* InStructData)
{
	// Now read in each property
	TArray<FString> TempPropertyImportNames;
	for (TFieldIterator<FProperty> It(InStruct); It; ++It)
	{
		FProperty* BaseProp = *It;
		check(BaseProp);

		if (!BaseProp->HasMetaData(FPMXlsxMetadata::IMPORT_FROM_XLSX_METADATA_TAG))
		{
			continue;
		}

		const FString ColumnName = DataTableUtils::GetPropertyExportName(BaseProp);

		TSharedPtr<FJsonValue> ParsedPropertyValue;
		DataTableUtils::GetPropertyImportNames(BaseProp, TempPropertyImportNames);
		for (const FString& PropertyName : TempPropertyImportNames)
		{
			ParsedPropertyValue = InParsedObject->TryGetField(PropertyName);
			if (ParsedPropertyValue.IsValid())
			{
				break;
			}
		}

		if (!ParsedPropertyValue.IsValid())
		{
#if WITH_EDITOR
			// If the structure has specified the property as optional for import (gameplay code likely doing a custom fix-up or parse of that property),
			// then avoid warning about it
			static const FName DataTableImportOptionalMetadataKey(TEXT("DataTableImportOptional"));
			if (BaseProp->HasMetaData(DataTableImportOptionalMetadataKey))
			{
				continue;
			}
#endif // WITH_EDITOR

			if (!DataAsset->bIgnoreMissingFields)
			{
				ImportProblems.Add(FString::Printf(TEXT("Row '%s' is missing an entry for '%s'."), *InRowName.ToString(), *ColumnName));
			}

			continue;
		}

		if (BaseProp->ArrayDim == 1)
		{
			void* Data = BaseProp->ContainerPtrToValuePtr<void>(InStructData, 0);
			ReadStructEntry(ParsedPropertyValue.ToSharedRef(), InRowName, ColumnName, InStructData, BaseProp, Data);
		}
		else
		{
			const TCHAR* const ParsedPropertyType = JSONTypeToString(ParsedPropertyValue->Type);

			const TArray< TSharedPtr<FJsonValue> >* PropertyValuesPtr;
			if (!ParsedPropertyValue->TryGetArray(PropertyValuesPtr))
			{
				ImportProblems.Add(FString::Printf(TEXT("Property '%s' on row '%s' is the incorrect type. Expected Array, got %s."), *ColumnName, *InRowName.ToString(), ParsedPropertyType));
				return false;
			}

			if (BaseProp->ArrayDim != PropertyValuesPtr->Num())
			{
				ImportProblems.Add(FString::Printf(TEXT("Property '%s' on row '%s' is a static sized array with %d elements, but we have %d values to import"), *ColumnName, *InRowName.ToString(), BaseProp->ArrayDim, PropertyValuesPtr->Num()));
			}

			for (int32 ArrayEntryIndex = 0; ArrayEntryIndex < BaseProp->ArrayDim; ++ArrayEntryIndex)
			{
				if (PropertyValuesPtr->IsValidIndex(ArrayEntryIndex))
				{
					void* Data = BaseProp->ContainerPtrToValuePtr<void>(InStructData, ArrayEntryIndex);
					const TSharedPtr<FJsonValue>& PropertyValueEntry = (*PropertyValuesPtr)[ArrayEntryIndex];
					ReadContainerEntry(PropertyValueEntry.ToSharedRef(), InRowName, ColumnName, ArrayEntryIndex, BaseProp, Data);
				}
			}
		}
	}

	return true;
}

bool FPMXlsxDataAssetImporterJSON::ReadStructEntry(const TSharedRef<FJsonValue>& InParsedPropertyValue, const FName InRowName, const FString& InColumnName, const void* InRowData, FProperty* InProperty, void* InPropertyData)
{
	const TCHAR* const ParsedPropertyType = JSONTypeToString(InParsedPropertyValue->Type);

	if (FEnumProperty* EnumProp = CastField<FEnumProperty>(InProperty))
	{
		FString EnumValue;
		if (InParsedPropertyValue->TryGetString(EnumValue))
		{
			FString Error = DataTableUtils::AssignStringToProperty(EnumValue, InProperty, (uint8*)InRowData);
			if (!Error.IsEmpty())
			{
				ImportProblems.Add(FString::Printf(TEXT("Property '%s' on row '%s' has invalid enum value: %s."), *InColumnName, *InRowName.ToString(), *EnumValue));
				return false;
			}
		}
		else
		{
			int64 PropertyValue = 0;
			if (!InParsedPropertyValue->TryGetNumber(PropertyValue))
			{
				ImportProblems.Add(FString::Printf(TEXT("Property '%s' on row '%s' is the incorrect type. Expected Integer, got %s."), *InColumnName, *InRowName.ToString(), ParsedPropertyType));
				return false;
			}

			EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(InPropertyData, PropertyValue);
		}
	}
	else if (FNumericProperty *NumProp = CastField<FNumericProperty>(InProperty))
	{
		FString EnumValue;
		if (NumProp->IsEnum() && InParsedPropertyValue->TryGetString(EnumValue))
		{
			FString Error = DataTableUtils::AssignStringToProperty(EnumValue, InProperty, (uint8*)InRowData);
			if (!Error.IsEmpty())
			{
				ImportProblems.Add(FString::Printf(TEXT("Property '%s' on row '%s' has invalid enum value: %s."), *InColumnName, *InRowName.ToString(), *EnumValue));
				return false;
			}
		}
		else if (NumProp->IsInteger())
		{
			int64 PropertyValue = 0;
			if (!InParsedPropertyValue->TryGetNumber(PropertyValue))
			{
				ImportProblems.Add(FString::Printf(TEXT("Property '%s' on row '%s' is the incorrect type. Expected Integer, got %s."), *InColumnName, *InRowName.ToString(), ParsedPropertyType));
				return false;
			}

			NumProp->SetIntPropertyValue(InPropertyData, PropertyValue);
		}
		else
		{
			double PropertyValue = 0.0;
			if (!InParsedPropertyValue->TryGetNumber(PropertyValue))
			{
				ImportProblems.Add(FString::Printf(TEXT("Property '%s' on row '%s' is the incorrect type. Expected Double, got %s."), *InColumnName, *InRowName.ToString(), ParsedPropertyType));
				return false;
			}

			NumProp->SetFloatingPointPropertyValue(InPropertyData, PropertyValue);
		}
	}
	else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(InProperty))
	{
		bool PropertyValue = false;
		if (!InParsedPropertyValue->TryGetBool(PropertyValue))
		{
			ImportProblems.Add(FString::Printf(TEXT("Property '%s' on row '%s' is the incorrect type. Expected Boolean, got %s."), *InColumnName, *InRowName.ToString(), ParsedPropertyType));
			return false;
		}

		BoolProp->SetPropertyValue(InPropertyData, PropertyValue);
	}
	else if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(InProperty))
	{
		const TArray< TSharedPtr<FJsonValue> >* PropertyValuesPtr;
		if (!InParsedPropertyValue->TryGetArray(PropertyValuesPtr))
		{
			ImportProblems.Add(FString::Printf(TEXT("Property '%s' on row '%s' is the incorrect type. Expected Array, got %s."), *InColumnName, *InRowName.ToString(), ParsedPropertyType));
			return false;
		}

		FScriptArrayHelper ArrayHelper(ArrayProp, InPropertyData);
		ArrayHelper.EmptyValues();
		for (const TSharedPtr<FJsonValue>& PropertyValueEntry : *PropertyValuesPtr)
		{
			const int32 NewEntryIndex = ArrayHelper.AddValue();
			uint8* ArrayEntryData = ArrayHelper.GetRawPtr(NewEntryIndex);
			ReadContainerEntry(PropertyValueEntry.ToSharedRef(), InRowName, InColumnName, NewEntryIndex, ArrayProp->Inner, ArrayEntryData);
		}
	}
	else if (FSetProperty* SetProp = CastField<FSetProperty>(InProperty))
	{
		const TArray< TSharedPtr<FJsonValue> >* PropertyValuesPtr;
		if (!InParsedPropertyValue->TryGetArray(PropertyValuesPtr))
		{
			ImportProblems.Add(FString::Printf(TEXT("Property '%s' on row '%s' is the incorrect type. Expected Array, got %s."), *InColumnName, *InRowName.ToString(), ParsedPropertyType));
			return false;
		}

		FScriptSetHelper SetHelper(SetProp, InPropertyData);
		SetHelper.EmptyElements();
		for (const TSharedPtr<FJsonValue>& PropertyValueEntry : *PropertyValuesPtr)
		{
			const int32 NewEntryIndex = SetHelper.AddDefaultValue_Invalid_NeedsRehash();
			uint8* SetEntryData = SetHelper.GetElementPtr(NewEntryIndex);
			ReadContainerEntry(PropertyValueEntry.ToSharedRef(), InRowName, InColumnName, NewEntryIndex, SetHelper.GetElementProperty(), SetEntryData);
		}
		SetHelper.Rehash();
	}
	else if (FMapProperty* MapProp = CastField<FMapProperty>(InProperty))
	{
		const TSharedPtr<FJsonObject>* PropertyValue;
		if (!InParsedPropertyValue->TryGetObject(PropertyValue))
		{
			ImportProblems.Add(FString::Printf(TEXT("Property '%s' on row '%s' is the incorrect type. Expected Object, got %s."), *InColumnName, *InRowName.ToString(), ParsedPropertyType));
			return false;
		}

		FScriptMapHelper MapHelper(MapProp, InPropertyData);
		MapHelper.EmptyValues();
		for (const auto& PropertyValuePair : (*PropertyValue)->Values)
		{
			const int32 NewEntryIndex = MapHelper.AddDefaultValue_Invalid_NeedsRehash();
			uint8* MapKeyData = MapHelper.GetKeyPtr(NewEntryIndex);
			uint8* MapValueData = MapHelper.GetValuePtr(NewEntryIndex);

			// JSON object keys are always strings
			const FString KeyError = DataTableUtils::AssignStringToPropertyDirect(PropertyValuePair.Key, MapHelper.GetKeyProperty(), MapKeyData);
			if (KeyError.Len() > 0)
			{
				MapHelper.RemoveAt(NewEntryIndex);
				ImportProblems.Add(FString::Printf(TEXT("Problem assigning key '%s' to property '%s' on row '%s' : %s"), *PropertyValuePair.Key, *InColumnName, *InRowName.ToString(), *KeyError));
				return false;
			}

			if (!ReadContainerEntry(PropertyValuePair.Value.ToSharedRef(), InRowName, InColumnName, NewEntryIndex, MapHelper.GetValueProperty(), MapValueData))
			{
				MapHelper.RemoveAt(NewEntryIndex);
				return false;
			}
		}
		MapHelper.Rehash();
	}
	else if (FStructProperty* StructProp = CastField<FStructProperty>(InProperty))
	{
		const TSharedPtr<FJsonObject>* PropertyValue = nullptr;
		if (InParsedPropertyValue->TryGetObject(PropertyValue))
		{
			return ReadStruct(PropertyValue->ToSharedRef(), StructProp->Struct, InRowName, InPropertyData);
		}
		else
		{
			// If the JSON does not contain a JSON object for this struct, we try to use the backwards-compatible string deserialization, same as the "else" block below
			FString PropertyValueString;
			if (!InParsedPropertyValue->TryGetString(PropertyValueString))
			{
				ImportProblems.Add(FString::Printf(TEXT("Property '%s' on row '%s' is the incorrect type. Expected String, got %s."), *InColumnName, *InRowName.ToString(), ParsedPropertyType));
				return false;
			}

			const FString Error = DataTableUtils::AssignStringToProperty(PropertyValueString, InProperty, (uint8*)InRowData);
			if (Error.Len() > 0)
			{
				ImportProblems.Add(FString::Printf(TEXT("Problem assigning string '%s' to property '%s' on row '%s' : %s"), *PropertyValueString, *InColumnName, *InRowName.ToString(), *Error));
				return false;
			}

			return true;
		}
	}
	else
	{
		FString PropertyValue;
		if (!InParsedPropertyValue->TryGetString(PropertyValue))
		{
			ImportProblems.Add(FString::Printf(TEXT("Property '%s' on row '%s' is the incorrect type. Expected String, got %s."), *InColumnName, *InRowName.ToString(), ParsedPropertyType));
			return false;
		}

		const FString Error = DataTableUtils::AssignStringToProperty(PropertyValue, InProperty, (uint8*)InRowData);
		if(Error.Len() > 0)
		{
			ImportProblems.Add(FString::Printf(TEXT("Problem assigning string '%s' to property '%s' on row '%s' : %s"), *PropertyValue, *InColumnName, *InRowName.ToString(), *Error));
			return false;
		}
	}

	return true;
}

bool FPMXlsxDataAssetImporterJSON::ReadContainerEntry(const TSharedRef<FJsonValue>& InParsedPropertyValue, const FName InRowName, const FString& InColumnName, const int32 InArrayEntryIndex, FProperty* InProperty, void* InPropertyData)
{
	const TCHAR* const ParsedPropertyType = JSONTypeToString(InParsedPropertyValue->Type);

	if (FEnumProperty* EnumProp = CastField<FEnumProperty>(InProperty))
	{
		FString EnumValue;
		if (InParsedPropertyValue->TryGetString(EnumValue))
		{
			FString Error = DataTableUtils::AssignStringToPropertyDirect(EnumValue, InProperty, (uint8*)InPropertyData);
			if (!Error.IsEmpty())
			{
				ImportProblems.Add(FString::Printf(TEXT("Entry %d on property '%s' on row '%s' has invalid enum value: %s."), InArrayEntryIndex, *InColumnName, *InRowName.ToString(), *EnumValue));
				return false;
			}
		}
		else
		{
			int64 PropertyValue = 0;
			if (!InParsedPropertyValue->TryGetNumber(PropertyValue))
			{
				ImportProblems.Add(FString::Printf(TEXT("Entry %d on property '%s' on row '%s' is the incorrect type. Expected Integer, got %s."), InArrayEntryIndex, *InColumnName, *InRowName.ToString(), ParsedPropertyType));
				return false;
			}

			EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(InPropertyData, PropertyValue);
		}
	}
	else if (FNumericProperty *NumProp = CastField<FNumericProperty>(InProperty))
	{
		FString EnumValue;
		if (NumProp->IsEnum() && InParsedPropertyValue->TryGetString(EnumValue))
		{
			FString Error = DataTableUtils::AssignStringToPropertyDirect(EnumValue, InProperty, (uint8*)InPropertyData);
			if (!Error.IsEmpty())
			{
				ImportProblems.Add(FString::Printf(TEXT("Entry %d on property '%s' on row '%s' has invalid enum value: %s."), InArrayEntryIndex, *InColumnName, *InRowName.ToString(), *EnumValue));
				return false;
			}
		}
		else if(NumProp->IsInteger())
		{
			int64 PropertyValue = 0;
			if (!InParsedPropertyValue->TryGetNumber(PropertyValue))
			{
				ImportProblems.Add(FString::Printf(TEXT("Entry %d on property '%s' on row '%s' is the incorrect type. Expected Integer, got %s."), InArrayEntryIndex, *InColumnName, *InRowName.ToString(), ParsedPropertyType));
				return false;
			}

			NumProp->SetIntPropertyValue(InPropertyData, PropertyValue);
		}
		else
		{
			double PropertyValue = 0.0;
			if (!InParsedPropertyValue->TryGetNumber(PropertyValue))
			{
				ImportProblems.Add(FString::Printf(TEXT("Entry %d on property '%s' on row '%s' is the incorrect type. Expected Double, got %s."), InArrayEntryIndex, *InColumnName, *InRowName.ToString(), ParsedPropertyType));
				return false;
			}

			NumProp->SetFloatingPointPropertyValue(InPropertyData, PropertyValue);
		}
	}
	else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(InProperty))
	{
		bool PropertyValue = false;
		if (!InParsedPropertyValue->TryGetBool(PropertyValue))
		{
			ImportProblems.Add(FString::Printf(TEXT("Entry %d on property '%s' on row '%s' is the incorrect type. Expected Boolean, got %s."), InArrayEntryIndex, *InColumnName, *InRowName.ToString(), ParsedPropertyType));
			return false;
		}

		BoolProp->SetPropertyValue(InPropertyData, PropertyValue);
	}
	else if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(InProperty))
	{
		// Cannot nest arrays
		return false;
	}
	else if (FSetProperty* SetProp = CastField<FSetProperty>(InProperty))
	{
		// Cannot nest sets
		return false;
	}
	else if (FMapProperty* MapProp = CastField<FMapProperty>(InProperty))
	{
		// Cannot nest maps
		return false;
	}
	else if (FStructProperty* StructProp = CastField<FStructProperty>(InProperty))
	{
		const TSharedPtr<FJsonObject>* PropertyValue = nullptr;
		if (InParsedPropertyValue->TryGetObject(PropertyValue))
		{
			return ReadStruct(PropertyValue->ToSharedRef(), StructProp->Struct, InRowName, InPropertyData);
		}
		else
		{
			// If the JSON does not contain a JSON object for this struct, we try to use the backwards-compatible string deserialization, same as the "else" block below
			FString PropertyValueString;
			if (!InParsedPropertyValue->TryGetString(PropertyValueString))
			{
				ImportProblems.Add(FString::Printf(TEXT("Property '%s' on row '%s' is the incorrect type. Expected String, got %s."), *InColumnName, *InRowName.ToString(), ParsedPropertyType));
				return false;
			}

			const FString Error = DataTableUtils::AssignStringToPropertyDirect(PropertyValueString, InProperty, (uint8*)InPropertyData);
			if (Error.Len() > 0)
			{
				ImportProblems.Add(FString::Printf(TEXT("Problem assigning string '%s' to entry %d on property '%s' on row '%s' : %s"), InArrayEntryIndex, *PropertyValueString, *InColumnName, *InRowName.ToString(), *Error));
				return false;
			}

			return true;
		}
	}
	else
	{
		FString PropertyValue;
		if (!InParsedPropertyValue->TryGetString(PropertyValue))
		{
			ImportProblems.Add(FString::Printf(TEXT("Entry %d on property '%s' on row '%s' is the incorrect type. Expected String, got %s."), InArrayEntryIndex, *InColumnName, *InRowName.ToString(), ParsedPropertyType));
			return false;
		}

		const FString Error = DataTableUtils::AssignStringToPropertyDirect(PropertyValue, InProperty, (uint8*)InPropertyData);
		if(Error.Len() > 0)
		{
			ImportProblems.Add(FString::Printf(TEXT("Problem assigning string '%s' to entry %d on property '%s' on row '%s' : %s"), InArrayEntryIndex, *PropertyValue, *InColumnName, *InRowName.ToString(), *Error));
			return false;
		}
	}

	return true;
}