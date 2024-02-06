// Copyright 2015-2020 Alderon Games Pty Ltd, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "POTAbilityTypes.h"
#include "GameplayEffectExecutionCalculation.h"
#include "POTStaminaExecution.generated.h"

/**
 * 
 */
UCLASS()
class PATHOFTITANS_API UPOTStaminaExecution : public UGameplayEffectExecutionCalculation
{
	GENERATED_BODY()

public:
	// Constructor and overrides
	UPOTStaminaExecution();
	virtual void Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams, OUT FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const override;

};
