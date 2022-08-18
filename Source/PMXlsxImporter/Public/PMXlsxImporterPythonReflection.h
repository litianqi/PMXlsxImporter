// Copyright Tianqi Li. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PMXlsxImporterPythonReflection.generated.h"

// Limitations
// - Array of Struct is supported, Array in Struct is not supported, only top fields can be an Array
// - Struct should either be marked with SplitStructInXLSX, or can be imported from a string
// - Set and Map is not supported
// - two dimensional Array is not supported

// Rules
// - Only field marks with ImportFromXLSX will be imported
// - Only Struct marks with SplitStructInXLSX will be split in excel

UENUM(BlueprintType)
enum class EPMXlsxFieldType : uint8
{
	Enum,
	Numeric,
	Bool,
	Array,
	Set,
	Map,
	Struct,
	Others
};

USTRUCT(Blueprintable, BlueprintType)
struct FPMXlsxFieldTypeInfo
{
	GENERATED_BODY()

public:
	FPMXlsxFieldTypeInfo(): Type(EPMXlsxFieldType::Others), Element_Type(EPMXlsxFieldType::Others), Index(0), ParentIndex(-1)
	{
	}

	UPROPERTY(BlueprintReadWrite, Category = XlsxImporter)
	FString NameCPP;

	UPROPERTY(BlueprintReadWrite, Category = XlsxImporter)
	EPMXlsxFieldType Type;
	
	UPROPERTY(BlueprintReadWrite, Category = XlsxImporter)
	FString CPPType;

	UPROPERTY(BlueprintReadWrite, Category = XlsxImporter)
	EPMXlsxFieldType Element_Type;
	
	UPROPERTY(BlueprintReadWrite, Category = XlsxImporter)
	FString Element_CPPType;

	UPROPERTY(BlueprintReadWrite, Category = XlsxImporter)
	FString GameplayTagFilter;

	UPROPERTY(BlueprintReadWrite, Category = XlsxImporter)
	bool bSplitStruct = false;

	// UPROPERTY(BlueprintReadWrite, Category = XlsxImporter)
	// bool bSplitArray = false;

	UPROPERTY(BlueprintReadWrite, Category = XlsxImporter)
	int32 Index = -1;

	UPROPERTY(BlueprintReadWrite, Category = XlsxImporter)
	int32 ParentIndex = -1;

	UPROPERTY(BlueprintReadWrite, Category = XlsxImporter)
	TArray<int32> ChildIndices;
};

// Reflection data passes to Python
USTRUCT(Blueprintable, BlueprintType)
struct FPMXlsxWorksheetTypeInfo
{
	GENERATED_BODY()

public:
	FPMXlsxWorksheetTypeInfo() {}

	void ReadStruct(const UStruct* InStruct);

	static EPMXlsxFieldType GetTypeOfField(FProperty* Property);

	void InternalReadStruct(const UStruct* InStruct, TArray<int32>& OutIndices);

	UPROPERTY()
	const UStruct* Struct = nullptr;
	
	UPROPERTY(BlueprintReadWrite, Category = XlsxImporter)
	TArray<int32> TopFields;
	
	UPROPERTY(BlueprintReadWrite, Category = XlsxImporter)
	TArray<FPMXlsxFieldTypeInfo> AllFields;
};
