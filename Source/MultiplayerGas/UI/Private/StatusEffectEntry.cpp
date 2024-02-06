// Copyright 2015-2020 Alderon Games Pty Ltd, All Rights Reserved.


#include "UI/StatusEffectEntry.h"
#include "Abilities/POTAbilitySystemGlobals.h"
#include "Player/IBaseCharacter.h"
#include "Components/RetainerBox.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "UI/StatusEffectBar.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "Abilities/GameplayEffectUIData_StatEffect.h"

UStatusEffectDataProvider::UStatusEffectDataProvider()
	: IconOverride(nullptr)
	, bRemoveOnInvalidEffect(true)
	, CustomEffectTag(FGameplayTag::EmptyTag)
{

}

TSoftObjectPtr<UTexture2D> UStatusEffectDataProvider::GetIcon(const UStatusEffectEntry* StatusEffect) const
{
	if (!IconOverride.IsNull())
	{
		return IconOverride;
	}

	if (const UGameplayEffectUIData_StatEffect* StatUIData = Cast<UGameplayEffectUIData_StatEffect>(UPOTAbilitySystemGlobals::GetActiveGameplayEffectUIData(StatusEffect->EffectHandle)))
	{
		return StatUIData->Icon;
	}

	return nullptr;
}

int32 UStatusEffectDataProvider::GetStackCount_Implementation(const UStatusEffectEntry* StatusEffect) const
{
	return StatusEffect->Stacks;
}

float UStatusEffectDataProvider::GetTimeRemaining_Implementation(const UStatusEffectEntry* StatusEffect) const
{
	if (const UGameplayEffectUIData_StatEffect* StatUIData = Cast<UGameplayEffectUIData_StatEffect>(UPOTAbilitySystemGlobals::GetActiveGameplayEffectUIData(StatusEffect->EffectHandle)))
	{
		if (StatUIData->bHideCooldown)
		{
			return FGameplayEffectConstants::INFINITE_DURATION;
		}
	}
	return UAbilitySystemBlueprintLibrary::GetActiveGameplayEffectRemainingDuration(const_cast<UStatusEffectEntry*>(StatusEffect), StatusEffect->EffectHandle);
}

bool UStatusEffectDataProvider::ShouldRemove_Implementation(const UStatusEffectEntry* StatusEffect) const
{
	TArray<FActiveGameplayEffectHandle> Effects = UPOTAbilitySystemGlobals::GetActiveGameplayEffectsByClass(StatusEffect->EffectHandle.GetOwningAbilitySystemComponent(), StatusEffect->EffectClass);
	if (Effects.IsEmpty())
	{
		return true;
	}
	else if (Effects.Num() > 1)
	{
		return false;
	}

	const FActiveGameplayEffectHandle& EffectHandle = Effects[0];
	FActiveGameplayEffectHandle QueryHandle;

	if (StatusEffect->EffectHandle != EffectHandle)
	{
		// Handle changed. If so, should query using the new one instead.
		QueryHandle = EffectHandle;
	}
	else
	{
		QueryHandle = StatusEffect->EffectHandle;
	}

	const float TimeRemaining = UAbilitySystemBlueprintLibrary::GetActiveGameplayEffectRemainingDuration(const_cast<UStatusEffectEntry*>(StatusEffect), QueryHandle);
	if (TimeRemaining == FGameplayEffectConstants::INFINITE_DURATION)
	{
		return UAbilitySystemBlueprintLibrary::GetActiveGameplayEffectStackCount(QueryHandle) <= 0;
	}

	return TimeRemaining <= UStatusEffectEntry::TimeRemainingRemoveFromUI;
}

bool UStatusEffectDataProvider::ShouldInvertFade(const UStatusEffectEntry* StatusEffect) const
{
	if (const UGameplayEffectUIData_StatEffect* StatUIData = Cast<UGameplayEffectUIData_StatEffect>(UPOTAbilitySystemGlobals::GetActiveGameplayEffectUIData(StatusEffect->EffectHandle)))
	{
		return StatUIData->bInvertFade;
	}
	return false;
}

void UStatusEffectEntry::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	FWidgetAnimationDynamicEvent OnDisappearEnd;
	OnDisappearEnd.BindDynamic(this, &UStatusEffectEntry::OnDisappearAnimationEnd);
	BindToAnimationFinished(Disappear, OnDisappearEnd);
}

void UStatusEffectEntry::NativeConstruct()
{
	Super::NativeConstruct();
	UpdateStatusEffect(1);
	bool bIsInhibited = false;

	if (UAbilitySystemComponent* ASC = EffectHandle.GetOwningAbilitySystemComponent())
	{
		if (const FActiveGameplayEffect* ActiveGE = ASC->GetActiveGameplayEffect(EffectHandle))
		{
			EffectClass = ActiveGE->Spec.Def->GetClass();
			Duration = ActiveGE->GetDuration();

			if (FActiveGameplayEffectEvents* Events = ASC->GetActiveEffectEventSet(ActiveGE->Handle))
			{
				Events->OnInhibitionChanged.AddUObject(this, &UStatusEffectEntry::OnEffectInhibitionChanged);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("Was unable to get the gameplay effect events to bind inhibition changed"));
			}

			bIsInhibited = ActiveGE->bIsInhibited;
		}
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("Could Not Get Ability System Component to bind oninhibition changed"));
	}
	IconImage->SetBrushFromSoftTexture(ExtractIcon());
	SetStackCount(1);

	OnEffectInhibitionChanged(EffectHandle, bIsInhibited);
}

void UStatusEffectEntry::OnEffectInhibitionChanged(FActiveGameplayEffectHandle Handle, bool bIsInhibited)
{
	if (!ParentBar ||
		bIsGettingRemoved ||
		bInhibited == bIsInhibited)
	{
		return;
	}
	
	// Some effects can call this function during slot construction which causes a crash
	if (bSlotInitialized)
	{
		UHorizontalBox* const ContainerBox = ParentBar->ContainerBox;
		const TArray<UPanelSlot*>& Slots = ContainerBox->GetSlots();
		const int32 Index = ContainerBox->GetChildIndex(this);
		const bool bValidIndex = Slots.IsValidIndex(Index);
		UHorizontalBoxSlot* const HBSlot = bValidIndex ? Cast<UHorizontalBoxSlot>(Slots[Index]) : nullptr;
		
		// Double check this is our slot
		if (HBSlot && HBSlot->Content == this)
		{
			float PaddingValue = bIsInhibited ? 0.0f : 5.0f;
			HBSlot->SetPadding(PaddingValue);
		}
	}

	UWidgetAnimation* const AnimToPlay = bIsInhibited ? Disappear : Appear;

	if (!IsAnimationPlaying(AnimToPlay))
	{
		StopAllAnimations();
		PlayAnimation(AnimToPlay);
	}
	
	bInhibited = bIsInhibited;
}

void UStatusEffectEntry::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (bIsGettingRemoved)
	{
		return;
	}
	const bool bShouldRemove = GetDataProvider()->ShouldRemove(this);
	if (bShouldRemove && IsRemovable())
	{
		RemoveEntry();
		return;
	}
	else if(!IsRemovable())
	{
		// bShouldRemove is repurposed here for hiding non-removable widget
		// this way effects that have replication problems still show up (like BleedingRate)
		// otherwise previously some effect widgets will be added then removed immediately on the client before replication
		OnEffectInhibitionChanged(EffectHandle, bShouldRemove);
	}
	
	UpdateStatusEffect(Stacks);
}

void UStatusEffectEntry::RemoveEntry()
{
	if (!IsRemovable()) return;
	
	bIsGettingRemoved = true;

	if (IsValid(ParentBar))
	{
		ParentBar->OnStatusEffectBeginRemove(this);
	}

	if (IsAnimationPlaying(Disappear)) 
	{
		return;
	}

	PlayAnimation(Disappear);
}

float UStatusEffectEntry::SetRemainingTime()
{
	const float RemainingDuration = GetDataProvider()->GetTimeRemaining(this);
	if (RemainingDuration <= -1.f) //This is an infinite effect
	{
		TimeText->SetVisibility(ESlateVisibility::Hidden);
		if (UMaterialInstanceDynamic* DynaMat = IconRetainer->GetEffectMaterial())
		{
			DynaMat->SetScalarParameterValue("FillPercent", 1.f);
		}
	}
	else
	{
		TimeText->SetVisibility(ESlateVisibility::HitTestInvisible);

		float RetainerAlpha = FMath::Clamp(RemainingDuration / Duration, 0.f, 1.f);
		if (GetDataProvider()->ShouldInvertFade(this))
		{
			RetainerAlpha = 1 - RetainerAlpha;
		}
		if (UMaterialInstanceDynamic* DynaMat = IconRetainer->GetEffectMaterial())
		{
			DynaMat->SetScalarParameterValue("FillPercent", RetainerAlpha);
		}
		

		const float DisplayDuration = FMath::Max(RemainingDuration, 1.f);
		const FTimespan DisplaySpan = UKismetMathLibrary::FromSeconds(DisplayDuration);

		static FNumberFormattingOptions TimeStampFormatOptions;
		TimeStampFormatOptions.MinimumIntegralDigits = 2;
		TimeStampFormatOptions.MaximumIntegralDigits = 2;

		const static FText TimestampFormat = FText::FromString(TEXT("{0}:{1}"));

		if (DisplaySpan.GetHours() > 0)
		{
			TimeText->SetText(FText::FormatOrdered(TimestampFormat,
				FText::AsNumber(DisplaySpan.GetHours(), &TimeStampFormatOptions),
				FText::AsNumber(DisplaySpan.GetMinutes(), &TimeStampFormatOptions)));
		}
		else
		{
			TimeText->SetText(FText::FormatOrdered(TimestampFormat,
				FText::AsNumber(DisplaySpan.GetMinutes(), &TimeStampFormatOptions),
				FText::AsNumber(DisplaySpan.GetSeconds(), &TimeStampFormatOptions)));
		}
	}

	return RemainingDuration;
}

int32 UStatusEffectEntry::SetStackCount(int32 NewStacks)
{
	if (NewStacks == -1)
	{
		NewStacks = GetDataProvider()->GetStackCount(this);
	}
	StackCount->SetVisibility(NewStacks > 1 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	if (NewStacks > 1)
	{
		StackCount->SetText(FText::AsNumber(NewStacks));
	}

	Stacks = NewStacks;

	return NewStacks;
}

TSoftObjectPtr<UTexture2D> UStatusEffectEntry::ExtractIcon() const
{
	return GetDataProvider()->GetIcon(this);
}

void UStatusEffectEntry::UpdateStatusEffect(int NewStacks)
{
	if (bIsGettingRemoved)
	{
		return;
	}

	if (!UPOTAbilitySystemGlobals::GetActiveGameplayEffect(EffectHandle))
	{
		// No longer an active gameplay effect for our status effect entry. Find a new effect from our effect class or remove this.
		bool bFoundNewEffectHandle = false;
		if (EffectClass)
		{
			const TArray<FActiveGameplayEffectHandle> EffectHandles = UPOTAbilitySystemGlobals::GetActiveGameplayEffectsByClass(EffectHandle.GetOwningAbilitySystemComponent(), EffectClass);
			if (!EffectHandles.IsEmpty())
			{
				EffectHandle = EffectHandles[0];
				bFoundNewEffectHandle = true;
			}
		}

		if (!bFoundNewEffectHandle && GetDataProvider()->bRemoveOnInvalidEffect)
		{
			if (IsRemovable())
			{
				RemoveEntry();
			}
			else
			{
				OnEffectInhibitionChanged(EffectHandle, true);
			}
			return;
		}
	}

	IconImage->SetBrushFromSoftTexture(ExtractIcon());

	SetStackCount(NewStacks);
	SetRemainingTime();
}

void UStatusEffectEntry::OnDisappearAnimationEnd_Implementation()
{
	bIsFadedOut = true;

	if (!bIsGettingRemoved)
	{
		return;
	}

	RemoveFromParent();
}

bool UStatusEffectEntry::IsEffectInhibited()
{
	UAbilitySystemComponent* ASC = EffectHandle.GetOwningAbilitySystemComponent();
	if (ASC)
	{
		if (FActiveGameplayEffect* ActiveGE = const_cast<FActiveGameplayEffect*>(ASC->GetActiveGameplayEffect(EffectHandle)))
		{
			return ActiveGE->bIsInhibited;
		}
	}
	return false;
}

UStatusEffectDataProvider* UStatusEffectEntry::GetDataProvider() const
{
	check(DataProvider);
	return DataProvider->GetDefaultObject<UStatusEffectDataProvider>();
}

void UPausedStatusEffectEntry::SetRemainingTime()
{
	TimeText->SetVisibility(ESlateVisibility::HitTestInvisible);

	const float DisplayDuration = FMath::Max(RemainingTime, 1.f);
	const FTimespan DisplaySpan = UKismetMathLibrary::FromSeconds(DisplayDuration);

	static FNumberFormattingOptions TimeStampFormatOptions = FNumberFormattingOptions().SetMinimumIntegralDigits(2).SetMaximumIntegralDigits(2);

	const static FText TimestampFormat = FText::FromString(TEXT("{0}:{1}"));

	if (DisplaySpan.GetHours() > 0)
	{
		TimeText->SetText(FText::FormatOrdered(TimestampFormat,
			FText::AsNumber(DisplaySpan.GetHours(), &TimeStampFormatOptions),
			FText::AsNumber(DisplaySpan.GetMinutes(), &TimeStampFormatOptions)));
	}
	else
	{
		TimeText->SetText(FText::FormatOrdered(TimestampFormat,
			FText::AsNumber(DisplaySpan.GetMinutes(), &TimeStampFormatOptions),
			FText::AsNumber(DisplaySpan.GetSeconds(), &TimeStampFormatOptions)));
	}
}

void UPausedStatusEffectEntry::NativeOnInitialized()
{
	Super::NativeOnInitialized();
}

void UPausedStatusEffectEntry::NativeConstruct()
{
	Super::NativeConstruct();

	// update image icon based on effect duration
	float RetainerAlpha = FMath::Clamp(RemainingTime / Duration, 0.f, 1.f);
	if (!IconRetainer)
	{
		return;
	}

	if (UMaterialInstanceDynamic* DynaMat = IconRetainer->GetEffectMaterial())
	{
		DynaMat->SetScalarParameterValue("FillPercent", RetainerAlpha);
	}
}

void UPausedStatusEffectEntry::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
}