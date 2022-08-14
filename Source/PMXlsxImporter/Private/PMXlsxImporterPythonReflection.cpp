// Copyright Tianqi Li. All Rights Reserved.


#include "PMXlsxImporterPythonReflection.h"

#include "PMXlsxMetadata.h"


void FPMXlsxWorksheetTypeInfo::ReadStruct(const UStruct* InStruct)
{
	Struct = InStruct;

	FPMXlsxFieldTypeInfo NameField;
	NameField.Index = 0;
	NameField.Type = EPMXlsxFieldType::Others;
	NameField.NameCPP = TEXT("Name");
	NameField.CPPType = TEXT("FName");
	AllFields.Add(NameField);
	TopFields.Add(0);

	InternalReadStruct(Struct, TopFields);
}

EPMXlsxFieldType FPMXlsxWorksheetTypeInfo::GetTypeOfField(FProperty* Property)
{
	if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
	{
		return EPMXlsxFieldType::Enum;
	}
	else if (FNumericProperty *NumProp = CastField<FNumericProperty>(Property))
	{
		return EPMXlsxFieldType::Numeric;
	}
	else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
	{
		return EPMXlsxFieldType::Bool;
	}
	else if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
	{
		return EPMXlsxFieldType::Array;
	}
	else if (FSetProperty* SetProp = CastField<FSetProperty>(Property))
	{
		return EPMXlsxFieldType::Set;
	}
	else if (FMapProperty* MapProp = CastField<FMapProperty>(Property))
	{
		return EPMXlsxFieldType::Map;
	}
	else if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		return EPMXlsxFieldType::Struct;
	}
	else
	{
		return EPMXlsxFieldType::Others;
	}
}

void FPMXlsxWorksheetTypeInfo::InternalReadStruct(const UStruct* InStruct, TArray<int32>& OutIndices)
{
	// Iterate to create a Super first FProperty array
	// By default TFieldIterator will put Subclass first
	TArray<FProperty*> Properties;
	{
		const UStruct* CurrentStruct = InStruct;
		TArray<FProperty*> CurrentStructProperties;
		for (TFieldIterator<FProperty> It(InStruct, EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			if (!It->HasMetaData(FPMXlsxMetadata::IMPORT_FROM_XLSX_METADATA_TAG) /*&& InStruct == Struct*/)
			{
				continue;
			}
			if (It.GetStruct() != CurrentStruct)  // We meet a super struct
			{
				CurrentStruct = It.GetStruct();
				CurrentStructProperties.Append(Properties);
				Properties = MoveTempIfPossible(CurrentStructProperties);
				CurrentStructProperties.Reset();
			}
			CurrentStructProperties.Add(*It);
		}
		CurrentStructProperties.Append(Properties);
		Properties = MoveTempIfPossible(CurrentStructProperties);
	}
	
	for (FProperty* Property : Properties)
	{
		FPMXlsxFieldTypeInfo& FieldTypeInfo = AllFields.Add_GetRef(FPMXlsxFieldTypeInfo());
		OutIndices.Add(AllFields.Num() - 1);
		FieldTypeInfo.Index = AllFields.Num() - 1;

		FieldTypeInfo.NameCPP = Property->GetNameCPP();
		FieldTypeInfo.Type = GetTypeOfField(Property);
		FieldTypeInfo.CPPType = Property->GetCPPType();

		if (Property->HasMetaData("GameplayTagFilter"))
		{
			FieldTypeInfo.GameplayTagFilter = *Property->FindMetaData("GameplayTagFilter");
		}
		
		if (FieldTypeInfo.Type == EPMXlsxFieldType::Array)
		{
			const FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property);
			// FieldTypeInfo.bSplitArray = It->HasMetaData(FPMXlsxMetadata::SPLIT_ARRAY_IN_XLSX_METADATA_TAG);
			FieldTypeInfo.Element_Type = GetTypeOfField(ArrayProp->Inner);
			FieldTypeInfo.Element_CPPType = ArrayProp->Inner->GetCPPType();
		}

		if (FieldTypeInfo.Type == EPMXlsxFieldType::Struct || (FieldTypeInfo.Type == EPMXlsxFieldType::Array && FieldTypeInfo.Element_Type == EPMXlsxFieldType::Struct))
		{
			FieldTypeInfo.bSplitStruct = Property->HasMetaData(FPMXlsxMetadata::SPLIT_STRUCT_IN_XLSX_METADATA_TAG);

			if (FieldTypeInfo.bSplitStruct)
			{
				const FStructProperty* StructProp = nullptr;
				if (FieldTypeInfo.Type == EPMXlsxFieldType::Struct)
				{
					StructProp = CastField<FStructProperty>(Property);
				}
				else
				{
					const FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property);
					StructProp = CastField<FStructProperty>(ArrayProp->Inner);
				}
				InternalReadStruct(StructProp->Struct, FieldTypeInfo.ChildIndices);

				for (const int32 ChildIndex : FieldTypeInfo.ChildIndices)
				{
					AllFields[ChildIndex].ParentIndex = FieldTypeInfo.Index;
				}
			}
		}
	}
}
