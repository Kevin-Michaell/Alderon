// Copyright 2015-2020 Alderon Games Pty Ltd, All Rights Reserved.

#include "UI/StatusEffectBar.h"
#include "Player/IBaseCharacter.h"
#include "Player/Dinosaurs/IDinosaurCharacter.h"
#include "Abilities/POTAbilitySystemGlobals.h"
#include "UI/StatusEffectEntry.h"
#include "Abilities/GameplayEffectUIData_StatEffect.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/HorizontalBox.h"

#include "Engine/Texture2D.h"
#include "Runtime/UMG/Public/Components/Image.h"

void UStatusEffectBar::NativeConstruct()
{
	Super::NativeConstruct();

	OwnerCharacter = GetOwningPlayerPawn<AIBaseCharacter>();
	if (OwnerCharacter.IsValid())
	{
		BindEvents();
		AddInitialEffects();
		bPawnInitialized = true;
	}
}

void UStatusEffectBar::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (OwnerCharacter != GetOwningPlayerPawn())
	{
		UnbindEvents();
		ClearEffects();
		bPawnInitialized = false;

		OwnerCharacter = GetOwningPlayerPawn<AIBaseCharacter>();
		if (OwnerCharacter.IsValid())
		{
			BindEvents();
			AddInitialEffects();
			bPawnInitialized = true;
		}
	}
	else if (bPawnInitialized && (OwnerCharacter == nullptr || !OwnerCharacter->IsAlive()))
	{
		UnbindEvents();
		ClearEffects();
		bPawnInitialized = false;
	}
}

void UStatusEffectBar::UnbindEvents()
{
	if (OwnerCharacter == nullptr)
	{
		return;
	}

	OwnerCharacter->OnUIPausedEffectCreated.RemoveAll(this);
	OwnerCharacter->AbilitySystem->OnUIPausedEffectRemoved.RemoveAll(this);
	OwnerCharacter->AbilitySystem->OnUIEffectHandleUpdated.RemoveAll(this);
	OwnerCharacter->AbilitySystem->OnUIEffectRemoved.RemoveAll(this);
	OwnerCharacter->AbilitySystem->OnUIEffectUpdated.RemoveAll(this);

	OwnerCharacter->OnStatusStart.RemoveAll(this);

	if (AIDinosaurCharacter* DinoChar = Cast<AIDinosaurCharacter>(OwnerCharacter))
	{
		DinoChar->OnGrowthStart.RemoveAll(this);
	}
}

void UStatusEffectBar::OnStatusStart(EDamageEffectType DamageEffectType)
{
	if (const TSubclassOf<UStatusEffectDataProvider>* EffectProvider = EffectDataProviders.Find(DamageEffectType))
	{
		CreateOrUpdateStatusEffect(FActiveGameplayEffectHandle(), *EffectProvider, false);
	}
}

void UStatusEffectBar::BindEvents()
{
	OwnerCharacter->OnUIPausedEffectCreated.AddDynamic(this, &UStatusEffectBar::OnUIPausedEffectCreate);
	OwnerCharacter->AbilitySystem->OnUIPausedEffectRemoved.AddDynamic(this, &UStatusEffectBar::OnUIPausedEffectRemove);
	OwnerCharacter->AbilitySystem->OnUIEffectRemoved.AddDynamic(this, &UStatusEffectBar::OnUIEffectRemove);
	OwnerCharacter->AbilitySystem->OnUIEffectUpdated.AddDynamic(this, &UStatusEffectBar::OnUIEffectUpdate);

	OwnerCharacter->OnStatusStart.AddDynamic(this, &UStatusEffectBar::OnStatusStart);

	if (AIDinosaurCharacter* DinoChar = Cast<AIDinosaurCharacter>(OwnerCharacter))
	{
		DinoChar->OnGrowthStart.AddDynamic(this, &UStatusEffectBar::OnGrowth);
	}
}

void UStatusEffectBar::ClearEffects()
{
	ContainerBox->ClearChildren();
	ActiveStatusEffects.Empty();
	CustomStatusEffects.Empty();
}

void UStatusEffectBar::AddInitialEffects()
{
	check(OwnerCharacter.IsValid());
	
	// Display reloaded paused effect on UI
	OnUIPausedEffectReload();

	// Non-Removable widgets
	for (auto& EffectDataProvider : EffectDataProviders)
	{
		CreateOrUpdateStatusEffect(FActiveGameplayEffectHandle(), EffectDataProvider.Value, false);
	}
	
	// Removable Widgets
	if (OwnerCharacter->IsGrowing())
	{
		OnGrowth();
	}
	
	if (OwnerCharacter->IsWellRested())
	{
		OnWellRested();
	}
	
	TArray<FActiveGameplayEffectHandle> AllEffects = OwnerCharacter->AbilitySystem->GetActiveEffects(FGameplayEffectQuery());
	for (const FActiveGameplayEffectHandle& Effect : AllEffects)
	{
		if (UPOTAbilitySystemGlobals::GetActiveGameplayEffectUIData(Effect) != nullptr)
		{
			OnUIEffectUpdate(Effect, 1.f, 1);
		}
	}
}

void UStatusEffectBar::OnStatusEffectBeginRemove(const UStatusEffectEntry* Entry)
{
	if (!ensure(IsValid(Entry)) || !ensure(IsValid(Entry->GetDataProvider())) || !Entry->IsRemovable())
	{
		return;
	}

	if (Entry->GetDataProvider()->CustomEffectTag.IsValid() && CustomStatusEffects.Contains(Entry->GetDataProvider()->CustomEffectTag))
	{
		CustomStatusEffects.Remove(Entry->GetDataProvider()->CustomEffectTag);
	}
	else if (ActiveStatusEffects.Contains(Entry->EffectClass))
	{
		ActiveStatusEffects.Remove(Entry->EffectClass);
	}
}

void UStatusEffectBar::OnUIPausedEffectRemove(const UGameplayEffect* Effect)
{
	TArray<UWidget*> TopWidget = ContainerBox->GetAllChildren();

	for (UWidget* Child : TopWidget)
	{
		UPausedStatusEffectEntry* Entry = Cast<UPausedStatusEffectEntry>(Child);

		if (!Entry)
		{
			continue;
		}

		// remove all effects
		if (!Effect)
		{
			Entry->RemoveFromParent();
		}
		// remove single effect
		else if (Entry->EffectName == Effect->GetName())
		{
			Entry->RemoveFromParent();
			break;
		}
	}
}

void UStatusEffectBar::OnUIPausedEffectReload()
{
	// remove existing paused effects from UI
	OnUIPausedEffectRemove();

	// reload paused effects on UI
	for (const FSavedGameplayEffectData& SinglePausedEffect : OwnerCharacter->PausedGameplayEffectsList)
	{
		const UGameplayEffect* Effect = SinglePausedEffect.Effect;
		float EffectRemainingDuration = SinglePausedEffect.Duration;

		// On reloading, remove all the effects with grouped tag
		if (Effect->OngoingTagRequirements.RequireTags.HasTag(FGameplayTag::RequestGameplayTag(FName(TEXT("InGroup")))))
		{
			continue;
		}

		CreateStatusPausedEffectEntry(Effect, EffectRemainingDuration);
	}
}

void UStatusEffectBar::OnUIPausedEffectCreate()
{
	for (const FSavedGameplayEffectData& SinglePausedEffect : OwnerCharacter->PausedGameplayEffectsList)
	{
		OnUIPausedEffectRemove(SinglePausedEffect.Effect);

		CreateStatusPausedEffectEntry(SinglePausedEffect.Effect, SinglePausedEffect.Duration);
	}
}

void UStatusEffectBar::CreateStatusPausedEffectEntry(const UGameplayEffect* Effect, float EffectRemainingDuration)
{
	const UGameplayEffectUIData* EffectUIData = Effect->FindComponent<UGameplayEffectUIData>();
	if (!EffectUIData)
	{
		return;
	}
	const UGameplayEffectUIData_StatEffect* StatEffectUIData = Cast<UGameplayEffectUIData_StatEffect>(EffectUIData);

	UPausedStatusEffectEntry* NewEntry = CreateWidget<UPausedStatusEffectEntry>(this, PausedStatusEffectEntryClass);

	NewEntry->EffectName = Effect->GetName();

	NewEntry->Duration = OwnerCharacter->AbilitySystem->MakeOutgoingSpec(Effect->GetClass(), 1.f, OwnerCharacter->AbilitySystem->MakeEffectContext()).Data.Get()->GetDuration();
	NewEntry->RemainingTime = EffectRemainingDuration;
	NewEntry->SetRemainingTime();

	NewEntry->IconImage->SetBrushFromSoftTexture(StatEffectUIData->Icon.Get());

	UHorizontalBoxSlot* const HBSlot = ContainerBox->AddChildToHorizontalBox(NewEntry);
	HBSlot->SetPadding(5.f);
}

void UStatusEffectBar::OnUIEffectRemove(const FActiveGameplayEffect& ActiveEffect)
{
	if (!ActiveEffect.Spec.Def || !OwnerCharacter.IsValid())
	{
		return;
	}

	const TSubclassOf<UGameplayEffect> EffectClass = ActiveEffect.Spec.Def->GetClass();
	if (!ActiveStatusEffects.Contains(EffectClass))
	{
		return;
	}

	const TWeakObjectPtr<UStatusEffectEntry> UIEffect = ActiveStatusEffects[EffectClass];
	if (!UIEffect.IsValid() || !UIEffect.Get())
	{
		ActiveStatusEffects.Remove(EffectClass);
		return;
	}

	if (UPOTAbilitySystemGlobals::GetActiveGameplayEffectsByClass(OwnerCharacter->AbilitySystem, EffectClass).IsEmpty())
	{
		if (!UIEffect->IsRemovable())
		{
			return;
		}
		
		ActiveStatusEffects.Remove(EffectClass);
		UIEffect->RemoveEntry();
	}
}

void UStatusEffectBar::OnUIEffectUpdate(FActiveGameplayEffectHandle Handle, float Duration, int32 Stacks)
{
	UAbilitySystemComponent* AbilitySystem = Handle.GetOwningAbilitySystemComponent();
	if (!AbilitySystem)
	{
		return;
	}

	const FActiveGameplayEffect* ActiveEffect = AbilitySystem->GetActiveGameplayEffect(Handle);
	if (!ActiveEffect || !ActiveEffect->Spec.Def)
	{
		return;
	}

	float RemainingTime;
	FGameplayEffectContextHandle Context;
	int32 CachedStacks = 0;
	const TSubclassOf<UGameplayEffect> EffectClass = ActiveEffect->Spec.Def->GetClass();

	if (UPOTAbilitySystemGlobals::GetActiveGameplayEffectInfo(Handle, Duration, RemainingTime, CachedStacks, Context))
	{
		const UGameplayEffectUIData_StatEffect* StatUIData = Cast<UGameplayEffectUIData_StatEffect>(UPOTAbilitySystemGlobals::GetActiveGameplayEffectUIData(Handle));
		
		if (StatUIData && Context.IsValid() && UPOTAbilitySystemGlobals::EffectContextGetEventMagnitude(Context) != -1.f)
		{
			const bool bExecutionContextValue = StatUIData->bCombineStacks && Context.GetAbility();
			
			if (UStatusEffectEntry* Entry = GetExistingStatusEffectWidget(EffectClass))
			{
				if (!bExecutionContextValue)
				{
						Entry->UpdateStatusEffect(Stacks);
						return;
					}

					int32 OtherStacks = 0;
					FGameplayEffectQuery const Query = FGameplayEffectQuery::MakeQuery_MatchAllEffectTags(Context.GetAbility()->AbilityTags);
				for (FActiveGameplayEffectHandle EffectHandle : AbilitySystem->GetActiveEffects(Query))
					{
						if (EffectHandle != Handle)
						{
							//Overwrites previous info, but we are not using the original info beyond this point. If we do, we may want to introduce new variables.
							UPOTAbilitySystemGlobals::GetActiveGameplayEffectInfo(EffectHandle, Duration, RemainingTime, CachedStacks, Context);
							if (Context.IsValid())
							{
								OtherStacks++;
							}
						}
					}
					Entry->UpdateStatusEffect(Stacks + OtherStacks);
				}
			else
			{
				if (bExecutionContextValue)
				{
					FGameplayEffectQuery const Query = FGameplayEffectQuery::MakeQuery_MatchAllEffectTags(Context.GetAbility()->AbilityTags);
					for (const FActiveGameplayEffectHandle QueryEffect : AbilitySystem->GetActiveEffects(Query))
						{
						const FActiveGameplayEffect* QueryGameplayEffect = AbilitySystem->GetActiveGameplayEffect(QueryEffect);
							if (!QueryGameplayEffect || !QueryGameplayEffect->Spec.Def)
							{
								continue;
							}
							
							const TSubclassOf<UGameplayEffect> Class = QueryGameplayEffect->Spec.Def->GetClass();
							if (GetExistingStatusEffectWidget(Class))
							{
								//There is another status icon that will display this info.
								return;
							}
						}
					}
				
				TSubclassOf<UStatusEffectDataProvider> ProviderToUse = UStatusEffectDataProvider::StaticClass();
				if (const UGameplayEffect* EffectDef = AbilitySystem->GetGameplayEffectDefForHandle(Handle))
					{
						if (CustomEffectDataProviders.Contains(EffectDef->GetClass()))
						{
							ProviderToUse = CustomEffectDataProviders[EffectDef->GetClass()];
						}
					}

				CreateOrUpdateStatusEffect(Handle, ProviderToUse);
			}
		}
	}
}

void UStatusEffectBar::OnGrowth()
{
	if (GrowthEffectDataProvider == nullptr)
	{
		return;
	}
	CreateOrUpdateStatusEffect(FActiveGameplayEffectHandle(), GrowthEffectDataProvider);
}

void UStatusEffectBar::OnWellRested()
{
	if (WellRestedEffectDataProvider == nullptr)
	{
		return;
	}
	CreateOrUpdateStatusEffect(FActiveGameplayEffectHandle(), WellRestedEffectDataProvider);
}

UStatusEffectEntry* UStatusEffectBar::GetExistingStatusEffectWidget(const TSubclassOf<UGameplayEffect> EffectClass) const
{
	if (ActiveStatusEffects.Contains(EffectClass) && ActiveStatusEffects[EffectClass].IsValid())
	{
		return ActiveStatusEffects[EffectClass].Get();
	}

	return nullptr;
}

void UStatusEffectBar::CreateOrUpdateStatusEffect(const FActiveGameplayEffectHandle& Handle, const TSubclassOf<UStatusEffectDataProvider>& DataProviderClass, bool bRemovableWidget)
{
	const UStatusEffectDataProvider* const DProvider = DataProviderClass->GetDefaultObject<UStatusEffectDataProvider>();
	TSubclassOf<UGameplayEffect> EffectClass = nullptr;

	if (DProvider->CustomEffectTag.IsValid() && CustomStatusEffects.Contains(DProvider->CustomEffectTag))
	{
		if (bRemovableWidget)
		{
			return;
		}
		
		TWeakObjectPtr<UStatusEffectEntry> Entry = CustomStatusEffects[DProvider->CustomEffectTag];
		if (Entry.IsValid())
		{
			Entry->EffectHandle = Handle;
			Entry->DataProvider = DataProviderClass;
			Entry->OnEffectInhibitionChanged(Handle, false);
		}
		
		return;
	}

	if (!DProvider->CustomEffectTag.IsValid())
	{
		const UAbilitySystemComponent* const EffectAbilitySystem = Handle.GetOwningAbilitySystemComponent();
		if (!EffectAbilitySystem)
		{
			return;
		}

		const FActiveGameplayEffect* const ActiveEffect = EffectAbilitySystem->GetActiveGameplayEffect(Handle);
		if (!ActiveEffect || !ActiveEffect->Spec.Def)
		{
			return;
		}

		EffectClass = ActiveEffect->Spec.Def->GetClass();
		if (!EffectClass)
		{
			return;
		}

		const TWeakObjectPtr<UStatusEffectEntry>* const ExistingEntry = ActiveStatusEffects.Find(EffectClass);
		if (ExistingEntry && ExistingEntry->IsValid())
		{
			if (bRemovableWidget)
			{
				return;
			}
			
			TWeakObjectPtr<UStatusEffectEntry> Entry = *ExistingEntry;
			if (Entry.IsValid())
			{
				Entry->EffectHandle = Handle;
				Entry->DataProvider = DataProviderClass;
				Entry->OnEffectInhibitionChanged(Handle, false);
			}
			
			return;
		}
	}

	UStatusEffectEntry* const NewEntry = CreateWidget<UStatusEffectEntry>(this, StatusEffectEntryClass);
	NewEntry->ParentBar = this;
	NewEntry->EffectHandle = Handle;
	NewEntry->DataProvider = DataProviderClass;
	NewEntry->SetRemovable(bRemovableWidget);

	UHorizontalBoxSlot* const HBSlot = ContainerBox->AddChildToHorizontalBox(NewEntry);
	NewEntry->bSlotInitialized = true;
	HBSlot->SetPadding(NewEntry->IsEffectInhibited() ? 0.0f : 5.f);

	if (DProvider->CustomEffectTag.IsValid())
	{
		CustomStatusEffects.Add(DProvider->CustomEffectTag, NewEntry);
	}
	else
	{
		ActiveStatusEffects.Add(EffectClass, NewEntry);
	}
}