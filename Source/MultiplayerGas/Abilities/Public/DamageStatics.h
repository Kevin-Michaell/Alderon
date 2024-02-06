// Copyright 2019-2023 Alderon Games Pty Ltd, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayEffectExecutionCalculation.h"
#include "ITypes.h"
#include "Abilities/CoreAttributeSet.h"
#include "DamageStatics.generated.h"

USTRUCT()
struct FPOTDamageConfig
{
	GENERATED_BODY()

	FPOTDamageConfig()
	{
	};

	//float (UPOTGameplayAbility::*InScalarBlockFunction)(void) const
	FPOTDamageConfig(FGameplayEffectAttributeCaptureDefinition* InDefinition, FProperty* InIncomingProperty, const bool& AffectedByMagnitude, const bool& AffectedBySpikes, FString InCurveSuffix = "")
	{
		bEventMagnitude = AffectedByMagnitude;
		bSpikes = AffectedBySpikes;
		IncomingProperty = InIncomingProperty;
		Definition = InDefinition;
		CurveSuffix = InCurveSuffix;
	}
	
	bool bSpikes = false;
	bool bEventMagnitude = true;
	//float (UPOTGameplayAbility::*ScalarBlockFunction)(void) const = nullptr;

	FGameplayEffectAttributeCaptureDefinition* Definition = nullptr;
	FProperty* IncomingProperty = nullptr;

	FString CurveSuffix = "";
};

struct POTDamageStatics
{
	DECLARE_ATTRIBUTE_CAPTUREDEF(AttackDamage);
	DECLARE_ATTRIBUTE_CAPTUREDEF(BoneBreakChance);
	DECLARE_ATTRIBUTE_CAPTUREDEF(BoneBreakAmount);
	DECLARE_ATTRIBUTE_CAPTUREDEF(BleedAmount);
	DECLARE_ATTRIBUTE_CAPTUREDEF(PoisonAmount);
	DECLARE_ATTRIBUTE_CAPTUREDEF(VenomAmount);

	DECLARE_ATTRIBUTE_CAPTUREDEF(IncomingDamage);
	DECLARE_ATTRIBUTE_CAPTUREDEF(IncomingBoneBreakAmount);
	DECLARE_ATTRIBUTE_CAPTUREDEF(IncomingBleedingRate);
	DECLARE_ATTRIBUTE_CAPTUREDEF(IncomingPoisonRate);
	DECLARE_ATTRIBUTE_CAPTUREDEF(IncomingVenomRate);
	DECLARE_ATTRIBUTE_CAPTUREDEF(Armor);

	TMap<EDamageEffectType, FPOTDamageConfig> DamageConfig;

	//Have to manually handle combat weight since we need this from both target and source.
	FProperty* CombatWeightProperty;
	FGameplayEffectAttributeCaptureDefinition SourceCombatWeightDef;
	FGameplayEffectAttributeCaptureDefinition TargetCombatWeightDef;
	
	POTDamageStatics()
	{
		DEFINE_ATTRIBUTE_CAPTUREDEF(UCoreAttributeSet, AttackDamage, Source, true);
		DEFINE_ATTRIBUTE_CAPTUREDEF(UCoreAttributeSet, BoneBreakChance, Source, true);
		DEFINE_ATTRIBUTE_CAPTUREDEF(UCoreAttributeSet, BoneBreakAmount, Source, true);
		DEFINE_ATTRIBUTE_CAPTUREDEF(UCoreAttributeSet, BleedAmount, Source, true);
		DEFINE_ATTRIBUTE_CAPTUREDEF(UCoreAttributeSet, PoisonAmount, Source, true);
		DEFINE_ATTRIBUTE_CAPTUREDEF(UCoreAttributeSet, VenomAmount, Source, true);

		DEFINE_ATTRIBUTE_CAPTUREDEF(UCoreAttributeSet, IncomingDamage, Target, false);
		DEFINE_ATTRIBUTE_CAPTUREDEF(UCoreAttributeSet, IncomingBoneBreakAmount, Target, false);
		DEFINE_ATTRIBUTE_CAPTUREDEF(UCoreAttributeSet, IncomingBleedingRate, Target, false);
		DEFINE_ATTRIBUTE_CAPTUREDEF(UCoreAttributeSet, IncomingPoisonRate, Target, false);
		DEFINE_ATTRIBUTE_CAPTUREDEF(UCoreAttributeSet, IncomingVenomRate, Target, false);
		DEFINE_ATTRIBUTE_CAPTUREDEF(UCoreAttributeSet, Armor, Target, false);

		//Manually handle combat weight
		CombatWeightProperty = FindFProperty<FProperty>(UCoreAttributeSet::StaticClass(), GET_MEMBER_NAME_CHECKED(UCoreAttributeSet, CombatWeight));
		SourceCombatWeightDef = FGameplayEffectAttributeCaptureDefinition(CombatWeightProperty, EGameplayEffectAttributeCaptureSource::Source, true);
		TargetCombatWeightDef = FGameplayEffectAttributeCaptureDefinition(CombatWeightProperty, EGameplayEffectAttributeCaptureSource::Target, false);

		DamageConfig =  {
			{EDamageEffectType::BLEED, FPOTDamageConfig(&this->BleedAmountDef, this->IncomingBleedingRateProperty, true, true, "Bleed") },
			{EDamageEffectType::VENOM, FPOTDamageConfig(&this->VenomAmountDef, this->IncomingVenomRateProperty, true, true , "Venom")},
			{EDamageEffectType::POISONED, FPOTDamageConfig(&this->PoisonAmountDef, this->IncomingPoisonRateProperty, true, true, "Poison")},
			{EDamageEffectType::BROKENBONE, FPOTDamageConfig(&this->BoneBreakAmountDef, this->IncomingBoneBreakAmountProperty, true, true, "BoneBreak")},
			{EDamageEffectType::DAMAGED, FPOTDamageConfig(&this->AttackDamageDef, this->IncomingDamageProperty, true, true, "BaseDamage")},
			{EDamageEffectType::KNOCKBACK, FPOTDamageConfig(nullptr, nullptr, true, true, "Knockback")}
		};
	}

	static const POTDamageStatics& DamageStatics()
	{
		static POTDamageStatics DmgStatics;
		return DmgStatics;
	}
};