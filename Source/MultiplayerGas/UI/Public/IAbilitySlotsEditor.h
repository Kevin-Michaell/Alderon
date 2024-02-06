// Copyright 2019-2022 Alderon Games Pty Ltd, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Abilities/POTAbilityAsset.h"
#include "Player/IBaseCharacter.h"
#include "UI/IUserWidget.h"
#include "UObject/PrimaryAssetId.h"
#include "IAbilitySlotsEditor.generated.h"

class AIBaseCharacter;
/**
 * 
 */
UCLASS()
class PATHOFTITANS_API UIAbilitySlotsEditor : public UIUserWidget
{
	GENERATED_BODY()
	
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 ActionBarSlotCount;

	UPROPERTY(BlueprintReadOnly)
	TArray<FPrimaryAssetId> CurrentDinosaurAttacks;

public:
	UIAbilitySlotsEditor();

	virtual void NativeConstruct();

	//Returns the number of total attacks for the current dinosaur
	UFUNCTION(BlueprintCallable)
	int32 RequestAllAbilitiesForCurrentDinosaur();

	UFUNCTION(BlueprintImplementableEvent)
	void K2_OnAttacksLoaded();
	
	UFUNCTION(BlueprintCallable)
	void TrySetAbilityInSlot(int32 DesiredSlot, const FPrimaryAssetId& AssetId, EAbilityCategory SlotCategory);

	UFUNCTION(BlueprintCallable)
	void TrySetAbilityInActionBar(int32 DesiredSlot, const FPrimaryAssetId& AssetId, int32 OldSlot = -1);

	UFUNCTION(BlueprintCallable)
	void OpenAbilityPicker(int32 ClickedSlot);

	UFUNCTION(BlueprintImplementableEvent)
	void K2_OnAbilityPickerOpened(int32 ClickedSlot);

	static bool IsAbilityEnabled(const UPOTAbilityAsset* AbilityAsset);
	static bool CheckAbilityCompatibilityWithCharacter(const AIBaseCharacter* Character, const UPOTAbilityAsset* AbilityAsset);

	static FString GetSanitizedDinosaurName(AIBaseCharacter* DinoCharacter);

protected:
	UFUNCTION(BlueprintCallable)
	UTexture2D* GetAssetTexture(const FPrimaryAssetId& AssetId) const;

	
};
