#pragma once

#define POT_GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
	static FGameplayAttribute Get##PropertyName##Attribute() \
	{ \
		static FProperty* Prop = FindFieldChecked<FProperty>(ClassName::StaticClass(), GET_MEMBER_NAME_CHECKED(ClassName, PropertyName)); \
		return Prop; \
	}

#define POT_GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
	FORCEINLINE float Get##PropertyName() const \
	{ \
		return PropertyName.GetCurrentValue(); \
	}

#define POT_GAMEPLAYATTRIBUTE_VALUE_SETTER(ClassName, PropertyName) \
	void Set##PropertyName(float NewVal);

#define POT_GAMEPLAYATTRIBUTE_VALUE_SETTER_BODY(ClassName, PropertyName) \
	void ClassName::Set##PropertyName(float NewVal) \
	{ \
		UAbilitySystemComponent* AbilityComp = GetOwningAbilitySystemComponent(); \
		if (ensure(AbilityComp)) \
		{ \
				if (PropertyName.GetCurrentValue() != NewVal) \
				{ \
					AbilityComp->SetNumericAttributeBase(Get##PropertyName##Attribute(), NewVal); \
					MARK_PROPERTY_DIRTY_FROM_NAME(ClassName, PropertyName, this) \
				} \
		}; \
	}

#define POT_GAMEPLAYATTRIBUTE_VALUE_SETTER_BODY_NO_REP(ClassName, PropertyName) \
	void ClassName::Set##PropertyName(float NewVal) \
	{ \
		UAbilitySystemComponent* AbilityComp = GetOwningAbilitySystemComponent(); \
		if (ensure(AbilityComp)) \
		{ \
				if (PropertyName.GetCurrentValue() != NewVal) \
				{ \
					AbilityComp->SetNumericAttributeBase(Get##PropertyName##Attribute(), NewVal); \
				} \
		}; \
	}

#define POT_GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName) \
	FORCEINLINE void Init##PropertyName(float NewVal) \
	{ \
		PropertyName.SetBaseValue(NewVal); \
		PropertyName.SetCurrentValue(NewVal); \
	}


#define POT_ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \
	POT_GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
	POT_GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
	POT_GAMEPLAYATTRIBUTE_VALUE_SETTER(ClassName, PropertyName) \
	POT_GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

#define POT_ATTRIBUTE_ACCESSORS_BODY(ClassName, PropertyName) \
	POT_GAMEPLAYATTRIBUTE_VALUE_SETTER_BODY(ClassName, PropertyName)


#define POT_ATTRIBUTE_ACCESSORS_BODY_NO_REP(ClassName, PropertyName) \
	POT_GAMEPLAYATTRIBUTE_VALUE_SETTER_BODY_NO_REP(ClassName, PropertyName)

#define ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \
 	GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
 	GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
 	GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
 	GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

// requires "FRepChangedPropertyTracker& ChangedPropertyTracker" to be in scope
#define DOREPLIFETIME_ACTIVE_OVERRIDE_UOBJECT_FAST(Object, ClassName, PropName, bActive) \
{ \
	check(IsValid(Object)); \
	UE::Net::Private::FNetPropertyConditionManager::SetPropertyActiveOverride(ChangedPropertyTracker, Object, (int32)ClassName::ENetFields_Private::PropName, bActive); \
}

#define POT_GAMEPLAYATTRIBUTE_CONDITIONALREP_SETTER(ClassName, PropertyName) \
	bool bCondRepActive_##PropertyName = false;

#define POT_ATTRIBUTE_ACCESSORS_CONDITIONALREP(ClassName, PropertyName) \
	POT_ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \
	POT_GAMEPLAYATTRIBUTE_CONDITIONALREP_SETTER(ClassName, PropertyName)

#define POT_ATTRIBUTE_ACCESSORS_BODY_CONDITONAL(ClassName, PropertyName) \
	POT_ATTRIBUTE_ACCESSORS_BODY(ClassName, PropertyName) \
	void ClassName::CondRep_##PropertyName(bool bActive) \
	{ \
		bHasDirtyConditionalProperty = true; \
		bCondRepActive_##PropertyName = bActive; \
	}

#define DOREPLIFETIME_ACTIVE_OVERRIDE_POT_CONDITIONAL(Object, ClassName, PropName) \
	{ \
		if (!bHasDoneInitialReplication) \
		{ \
			DOREPLIFETIME_ACTIVE_OVERRIDE_UOBJECT_FAST(Object, ClassName, PropName, true); \
		} \
		else \
		{ \
			DOREPLIFETIME_ACTIVE_OVERRIDE_UOBJECT_FAST(Object, ClassName, PropName, bCondRepActive_##PropName); \
		} \
	}