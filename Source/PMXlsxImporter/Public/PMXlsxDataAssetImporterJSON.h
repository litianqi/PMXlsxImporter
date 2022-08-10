// Copyright Tianqi Li. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PMXlsxDataAsset.h"

/**
 * 
 */
class PMXLSXIMPORTER_API FPMXlsxDataAssetImporterJSON
{
public:
	FPMXlsxDataAssetImporterJSON(UPMXlsxDataAsset& InDataAsset, const TSharedRef<FJsonObject>& InJSONData, TArray<FString>& OutProblems);

	~FPMXlsxDataAssetImporterJSON();

	bool ReadAsset();

private:
	bool ReadStruct(const TSharedRef<FJsonObject>& InParsedObject, UStruct* InStruct, const FName InRowName, void* InStructData);

	bool ReadStructEntry(const TSharedRef<FJsonValue>& InParsedPropertyValue, const FName InRowName, const FString& InColumnName, const void* InRowData, FProperty* InProperty, void* InPropertyData);

	bool ReadContainerEntry(const TSharedRef<FJsonValue>& InParsedPropertyValue, const FName InRowName, const FString& InColumnName, const int32 InArrayEntryIndex, FProperty* InProperty, void* InPropertyData);

	// ReSharper disable once CppUE4ProbableMemoryIssuesWithUObject
	UPMXlsxDataAsset* DataAsset;
	const TSharedRef<FJsonObject>& JSONData;
	TArray<FString>& ImportProblems;
};
