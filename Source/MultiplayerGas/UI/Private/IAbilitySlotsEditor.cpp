// Copyright 2019-2022 Alderon Games Pty Ltd, All Rights Reserved.


#include "UI/IAbilitySlotsEditor.h"
#include "TitanAssetManager.h"
#include "Player/IBaseCharacter.h"
#include "Abilities/POTAbilityAsset.h"
#include "Engine/StreamableManager.h"

UIAbilitySlotsEditor::UIAbilitySlotsEditor()
	: ActionBarSlotCount(10)
{
	
}



void UIAbilitySlotsEditor::NativeConstruct()
{
	Super::NativeConstruct();

	RequestAllAbilitiesForCurrentDinosaur();	
}

int32 UIAbilitySlotsEditor::RequestAllAbilitiesForCurrentDinosaur()
{

	AIBaseCharacter* CurrentDinosaur = GetOwningPlayerPawn<AIBaseCharacter>();
	
	if (!CurrentDinosaur)
	{
		return -1;
	}

	FString Name = GetSanitizedDinosaurName(CurrentDinosaur);

	TArray<FPrimaryAssetId> AssetIds;
	CurrentDinosaurAttacks.Empty();

	UTitanAssetManager& AssetManager = UTitanAssetManager::Get();
	if (AssetManager.GetPrimaryAssetIdList(UTitanAssetManager::AbilityAssetType, AssetIds))
	{

		for (const FPrimaryAssetId& AbilityAssetId : AssetIds)
		{
			if (!AssetManager.GetPrimaryAssetPath(AbilityAssetId).ToString().Contains(Name))
			{
				continue;
			}

			UPOTAbilityAsset* AbilityAsset = AssetManager.ForceLoadAbility(AbilityAssetId);
			
			if (IsAbilityEnabled(AbilityAsset) &&
				CheckAbilityCompatibilityWithCharacter(CurrentDinosaur, AbilityAsset))
			{
				CurrentDinosaurAttacks.Add(AbilityAssetId);
			}
		}
	}

	FStreamableDelegate LoadedDelegate = FStreamableDelegate::CreateUObject(this, &UIAbilitySlotsEditor::K2_OnAttacksLoaded);
	AssetManager.LoadPrimaryAssets(CurrentDinosaurAttacks, {"UI"}, LoadedDelegate);

	return CurrentDinosaurAttacks.Num();
}

void UIAbilitySlotsEditor::TrySetAbilityInSlot(int32 DesiredSlot, const FPrimaryAssetId& AssetId, EAbilityCategory SlotCategory)
{
	AIBaseCharacter* CurrentDinosaur = GetOwningPlayerPawn<AIBaseCharacter>();
	if (CurrentDinosaur == nullptr)
	{
		return;
	}

	CurrentDinosaur->Server_SetAbilityInCategorySlot(DesiredSlot, AssetId, SlotCategory);
}

void UIAbilitySlotsEditor::TrySetAbilityInActionBar(int32 DesiredSlot, const FPrimaryAssetId& AssetId, int32 OldSlot)
{
	if (DesiredSlot > ActionBarSlotCount - 1)
	{
		return;
	}

	AIBaseCharacter* CurrentDinosaur = GetOwningPlayerPawn<AIBaseCharacter>();
	if (CurrentDinosaur == nullptr)
	{
		return;
	}

	CurrentDinosaur->Server_SetAbilityInActionBar(DesiredSlot, AssetId, OldSlot);
}

void UIAbilitySlotsEditor::OpenAbilityPicker(int32 ClickedSlot)
{
	K2_OnAbilityPickerOpened(ClickedSlot);

}

UTexture2D* UIAbilitySlotsEditor::GetAssetTexture(const FPrimaryAssetId& AssetId) const
{
	if (!AssetId.IsValid()) return nullptr;

	UTitanAssetManager& AssetManager = UTitanAssetManager::Get();
	UPOTAbilityAsset* AbilityAsset = AssetManager.ForceLoadAbility(AssetId);

	if (AbilityAsset == nullptr) return nullptr;

	return Cast<UTexture2D>(AbilityAsset->Icon.GetResourceObject());
}

FString UIAbilitySlotsEditor::GetSanitizedDinosaurName(class AIBaseCharacter* DinoCharacter)
{
	FString RetName = "";
	if (!DinoCharacter) return RetName;

	FString Temp;
	FString Right;
	
	DinoCharacter->GetName().Split("_", &Temp, &Right);

	Right.Split("_", &RetName, &Temp);

	RetName.RemoveFromEnd("Event");
	return RetName;

}

bool UIAbilitySlotsEditor::IsAbilityEnabled(const UPOTAbilityAsset* AbilityAsset)
{
	if (AbilityAsset == nullptr)
	{
		return false;
	}

#if !UE_BUILD_SHIPPING
	if (!AbilityAsset->IsEnabled())
	{
		return AbilityAsset->bEnabledInDevelopment;
	}

	return true;

#endif

	return AbilityAsset->IsEnabled();
}

bool UIAbilitySlotsEditor::CheckAbilityCompatibilityWithCharacter(const AIBaseCharacter* Character, const UPOTAbilityAsset* AbilityAsset)
{
	if (AbilityAsset == nullptr || Character == nullptr)
	{
		return false;
	}

	if (AbilityAsset->ParentCharacterDataId.IsValid() && AbilityAsset->ParentCharacterDataId != Character->CharacterDataAssetId)
	{
		return false;
	}

	return true;
}
