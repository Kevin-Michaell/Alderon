// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayEffectExecutionCalculation.h"
#include "POTGameplayAbility.h"
#include "POTDamageExecution.generated.h"

/**
 * A damage execution, which allows doing damage by combining a raw Damage number with AttackPower and DefensePower
 * Most games will want to implement multiple game-specific executions
 */
UCLASS(Config=Game)
class PATHOFTITANS_API UPOTDamageExecution : public UGameplayEffectExecutionCalculation
{
	GENERATED_BODY()

public:
	UPROPERTY(Config)
	float TramplePower = 3;

public:
	// Constructor and overrides
	UPOTDamageExecution();
	virtual void Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams, OUT FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const override;

};