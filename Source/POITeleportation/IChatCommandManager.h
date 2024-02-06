// Copyright 2019-2022 Alderon Games Pty Ltd, All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Online/IPlayerState.h"
#include "Containers/UnrealString.h"
#include "Delegates/DelegateInstanceInterface.h"
#include "Delegates/DelegateInstancesImpl.h"
#include "Misc/Attribute.h"
#include "ITypes.h"
#include "ChatCommands/IChatCommand.h"
#include "AlderonChatCommandManager.h"

#include "IChatCommandManager.generated.h"

class AIPointOfInterest;
class UWorld;
class UGameInstance;
class AIPlayerCaveBase;
class AIPlayerController;
class AIWater;
class AIWaystone;
class UMapRevealerComponent;

/**
 * 
 */
UCLASS()
class PATHOFTITANS_API AIChatCommandManager : public AAlderonChatCommandManager
{
	GENERATED_BODY()

public:
	static AIChatCommandManager* Get(UObject* WorldContextObject);

	void ProcessBattlEyeCommand(const FString& Command);
	
	virtual void InitChatCommandManager() override;
	
	virtual FChatCommandResponse ProcessChatCommand(AAlderonPlayerController* CallingPlayer, const FString& Command) override;
	virtual FChatCommandResponse NativeProcessStandaloneChatCommand(AAlderonPlayerController* CallingPlayer, const FString& Command, FAsyncChatCommandCallback Callback = FAsyncChatCommandCallback()) override;

	virtual bool CheckAdminAGID(const FString& AGID) const override;
	virtual bool CheckAdmin(const AAlderonPlayerController* PlayerController) const override;

	virtual FText GetUnknownCommandText() override;
	virtual FText GetNullObjectText() override;
	virtual FText GetNoPermissionText() override;
	virtual FText GetInvalidInputText() override;
	virtual FText GetHelpCommandDescriptionText() override;

public:
	// Util
	UFUNCTION(BlueprintCallable, Category = ChatCommands)
	static AActor* GetPoi(UObject* WorldContextObject, const FString& PoiName);
	UFUNCTION(BlueprintCallable, Category = ChatCommands)
	static void TeleportAllLocation(UObject* WorldContextObject, FVector Location, AIPlayerCaveBase* Instance = nullptr);
	UFUNCTION(BlueprintCallable, Category = ChatCommands)
	static void TeleportGroupLocation(UObject* WorldContextObject, TArray<APlayerState*> PlayerStates, FVector Location, AIPlayerCaveBase* Instance = nullptr);
	UFUNCTION(BlueprintCallable, Category = ChatCommands)
	static void TeleportAllPoi(AActor* Poi);

	virtual bool GetLocationFromPoi(FString PoiName, FVector& Location, bool bAllowWater, float ActorHalfHeight = 0.0f);
	virtual bool GetLocationFromPoi(AActor* Poi, FVector& Location, bool bAllowWater, float ActorHalfHeight = 0.0f);

	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;

	static void TeleportLocation(AIPlayerController* IPlayerController, FVector Location, AIPlayerCaveBase* NewInstance = nullptr, bool bAvoidWater = false);
	static void TeleportLocationUnsafe(AIPlayerController* IPlayerController, FVector Location, AIPlayerCaveBase* NewInstance = nullptr);

	const int CommandMarksLimit = 999999999;

private:
	bool InternalGetLocationFromPoi(AActor* Poi, FVector& Location, bool bIsWaterAllowed, float ActorHalfHeight = 0.0f);
protected:
	FChatCommandResponse SinglePlayerGoto(AIPlayerController* CallingPlayer, TArray<FString> Params);
	FChatCommandResponse SinglePlayerTeleport(AIPlayerController* CallingPlayer, TArray<FString> Params);

	void AdminRequestAction(const FString& Identifier, bool bUseName, bool bBan, const FString& Reason);

	virtual void MessageAllPlayers(UObject* WorldContextObject, FText Message);

	virtual AIWater* GetIWater(UWorld* World, const FString& WaterTag);

	virtual AIWaystone* GetIWaystone(UWorld* World, const FString& WaystoneTag);

	virtual int32 SetMarks(AIPlayerController* IPlayerController, int32 Marks, bool bSet = false, bool bAdd = false);
	virtual int32 GetMarks(AIPlayerController* IPlayerController);
	void ClampMarksUserInput(int32 &Marks);

	virtual bool GodMode(AIPlayerController* IPlayerController);

	static FString GetReflectedPropertyValueString(const UObject* const TargetObject, const FString& PropertyPath);

	// Server Command functions
	FChatCommandResponse ServerAutoRecord(AIPlayerController* CallingPlayer, TArray<FString> Params);
	FChatCommandResponse ServerAutoRecordRCON(TArray<FString>Params);

	FChatCommandResponse ResetHomeCaveSaveInfoCommand(AIPlayerController* CallingPlayer, TArray<FString> Params );

	FChatCommandResponse ClearCreatorObjectsCommand(TArray<FString> Params );

	FChatCommandResponse EnterHomecaveCommand(AIPlayerController* CallingPlayer, TArray<FString> Params );

	FChatCommandResponse LeaveHomecaveCommand(AIPlayerController* CallingPlayer, TArray<FString> Params );

	FChatCommandResponse HealPlayerCommand(AIPlayerController* CallingPlayer, TArray<FString> Params );
	FChatCommandResponse HealRCONCommand(TArray<FString> Params );

	FChatCommandResponse ListPlayersRCONCommand(TArray<FString> Params );

	FChatCommandResponse ListPlayerPositionsRCONCommand(TArray<FString> Params);
	
	FChatCommandResponse PlayerInfoRCONCommand(TArray<FString> Params );

	FChatCommandResponse HealAllCommand(TArray<FString> Params );

	FChatCommandResponse SetWeatherCommand(TArray<FString> Params );

	FChatCommandResponse PromoteRCONCommand(TArray<FString> Params );
	FChatCommandResponse PromoteCommand(AIPlayerController* CallingPlayer, TArray<FString> Params );

	FChatCommandResponse DemoteRCONCommand(TArray<FString> Params );
	FChatCommandResponse DemoteCommand(AIPlayerController* CallingPlayer, TArray<FString> Params );

	FChatCommandResponse TeleportRCONCommand(TArray<FString> Params );
	FChatCommandResponse TeleportCommand(AIPlayerController* CallingPlayer, TArray<FString> Params );

	FChatCommandResponse BringCommand(AIPlayerController* CallingPlayer, TArray<FString> Params );

	FChatCommandResponse BringAllCommand(AIPlayerController* CallingPlayer, TArray<FString> Params );
	FChatCommandResponse BringAllOfSpeciesCommand(AIPlayerController* CallingPlayer, TArray<FString> Params);
	FChatCommandResponse BringAllOfDietTypeCommand(AIPlayerController* CallingPlayer, TArray<FString> Params);

	FChatCommandResponse GotoCommand(AIPlayerController* CallingPlayer, TArray<FString> Params );

	FChatCommandResponse TeleportAllCommand(TArray<FString> Params );

	FChatCommandResponse SkipShedCommand(AIPlayerController* CallingPlayer, TArray<FString> Params);
	FChatCommandResponse SkipShedRCONCommand(TArray<FString> Params);

	FChatCommandResponse SetMarksRCONCommand(TArray<FString> Params );
	FChatCommandResponse SetMarksCommand(AIPlayerController* CallingPlayer, TArray<FString> Params);
	FChatCommandResponse SetMarksAllRCONCommand(TArray<FString> Params);
	FChatCommandResponse SetMarksAllCommand(AIPlayerController* CallingPlayer, TArray<FString> Params);
	FChatCommandResponse AddMarksRCONCommand(TArray<FString> Params );
	FChatCommandResponse AddMarksAllRCONCommand(TArray<FString> Params);
	FChatCommandResponse AddMarksCommand(AIPlayerController* CallingPlayer, TArray<FString> Params );
	FChatCommandResponse RemoveMarksRCONCommand(TArray<FString> Params );
	FChatCommandResponse RemoveMarksCommand(AIPlayerController* CallingPlayer, TArray<FString> Params );

	FChatCommandResponse GetAttributeRCONCommand(TArray<FString> Params);
	FChatCommandResponse GetAttributeCommand(AIPlayerController* CallingPlayer, TArray<FString> Params );

	FChatCommandResponse GetPropertyCommand(AIPlayerController* CallingPlayer, TArray<FString> Params );
	FChatCommandResponse GetPropertyRCONCommand(TArray<FString> Params);

	FChatCommandResponse GetAllAttributesCommand(AIPlayerController* CallingPlayer, TArray<FString> Params);
	FChatCommandResponse GetAllAttributesRCONCommand(TArray<FString> Params);

	FChatCommandResponse ListPropertiesCommand(AIPlayerController* CallingPlayer, TArray<FString> Params );
	FChatCommandResponse ListPropertiesRCONCommand(TArray<FString> Params);

	FChatCommandResponse ListCurveValuesCommand(AIPlayerController* CallingPlayer, TArray<FString> Params);
	FChatCommandResponse ListCurveValuesRCONCommand(TArray<FString> Params);

	FChatCommandResponse ListGameplayAbilitiesCommand(AIPlayerController* CallingPlayer, TArray<FString> Params);
	FChatCommandResponse ListGameplayAbilitiesRCONCommand(TArray<FString> Params);

	FChatCommandResponse InspectGameplayAbilityCommand(AIPlayerController* CallingPlayer, TArray<FString> Params);
	FChatCommandResponse InspectGameplayAbilityRCONCommand(TArray<FString> Params);

	static FString GetGameplayEffectExecutionInfo(const class UGameplayEffect* Effect, const FString& LinePrefix);

	FChatCommandResponse SetAttributeCommand(AIPlayerController* CallingPlayer, TArray<FString> Params );

	FChatCommandResponse ModifyAttributeRCONCommand(TArray<FString> Params);
	FChatCommandResponse ModifyAttributeCommand(AIPlayerController* CallingPlayer, TArray<FString> Params);

	FChatCommandResponse SetAttributeAllRCONCommand(TArray<FString> Params);

	FChatCommandResponse AwardGrowthCommand(AIPlayerController* CallingPlayer, TArray<FString> Params );
	FChatCommandResponse AwardWellRestedBuffCommand(AIPlayerController* CallingPlayer, TArray<FString> Params);

	FChatCommandResponse SetTimeCommand(TArray<FString> Params );

	FChatCommandResponse GodModeCommand(AIPlayerController* CallingPlayer, TArray<FString> Params );
	FChatCommandResponse GodModeRCONCommand(TArray<FString> Params);

	FChatCommandResponse KickCommand(AIPlayerController* CallingPlayer, TArray<FString> Params);
	FChatCommandResponse BanCommand(AIPlayerController* CallingPlayer, TArray<FString> Params);
	FChatCommandResponse KickRCONCommand(TArray<FString> Params );
	FChatCommandResponse BanRCONCommand(TArray<FString> Params );

	FChatCommandResponse ServerMuteRCONCommand(TArray<FString> Params);
	FChatCommandResponse ServerUnmuteRCONCommand(TArray<FString> Params);
	FChatCommandResponse WhitelistRCONCommand(TArray<FString> Params);
	FChatCommandResponse DelWhitelistRCONCommand(TArray<FString> Params);
	FChatCommandResponse ReloadBansRCONCommand(TArray<FString> Params);
	FChatCommandResponse ReloadMutesRCONCommand(TArray<FString> Params);
	FChatCommandResponse ReloadWhitelistRCONCommand(TArray<FString> Params);
	FChatCommandResponse ReloadMOTDRCONCommand(TArray<FString> Params);
	FChatCommandResponse ReloadRulesRCONCommand(TArray<FString> Params);


	FChatCommandResponse UnbanCommand(TArray<FString> Params);

	FChatCommandResponse AnnounceCommand(AIPlayerController* CallingPlayer, TArray<FString> Params);
	FChatCommandResponse AnnounceRCONCommand(TArray<FString>Params );

	FChatCommandResponse AnnounceToCommand(AIPlayerController* CallingPlayer, TArray<FString>Params );

	FChatCommandResponse SaveCommand(TArray<FString>Params );

	FChatCommandResponse LoadCommand(TArray<FString>Params);

	FChatCommandResponse SaveCreatorModeCommand(TArray<FString>Params, FAsyncChatCommandCallback& Callback);
	FChatCommandResponse LoadCreatorModeCommand(TArray<FString>Params, FAsyncChatCommandCallback& Callback);
	FChatCommandResponse ResetCreatorModeCommand(TArray<FString>Params);
	FChatCommandResponse RemoveCreatorModeSaveCommand(TArray<FString>Params, FAsyncChatCommandCallback& Callback);
	FChatCommandResponse ListCreatorModeSavesCommand(TArray<FString>Params, FAsyncChatCommandCallback& Callback);

	FChatCommandResponse RulesCommand(AIPlayerController* CallingPlayer, TArray<FString>Params);

	FChatCommandResponse MotdCommand(AIPlayerController* CallingPlayer, TArray<FString>Params );

	FChatCommandResponse GiveQuestRCONCommand(TArray<FString> Params, FAsyncChatCommandCallback& Callback);
	void OnGiveQuestRCONCommand(FPrimaryAssetId QuestAssetId, FString QuestNameString, AIBaseCharacter* const IBaseCharacter, FAsyncChatCommandCallback Callback);
	FChatCommandResponse GiveQuestCommand(AIPlayerController* CallingPlayer, TArray<FString> Params, FAsyncChatCommandCallback& Callback);

	FChatCommandResponse CompleteQuestCommand(AIPlayerController* CallingPlayer, TArray<FString> Params, FAsyncChatCommandCallback& Callback);
	void OnCompleteQuestCommand(FPrimaryAssetId QuestAssetId, AIPlayerController* CallingPlayer, AIBaseCharacter* const IBaseCharacter, FAsyncChatCommandCallback Callback);

	FChatCommandResponse ProfileCommand(TArray<FString>Params );

	FChatCommandResponse CrashCommand(AIPlayerController* CallingPlayer, TArray<FString>Params);

	FChatCommandResponse CrashServerCommand(TArray<FString>Params );

	FChatCommandResponse ToggleAdminCommand(AIPlayerController* CallingPlayer, TArray<FString>Params );

	FChatCommandResponse RestartCommand(TArray<FString> Params );

	FChatCommandResponse CancelRestartCommand(TArray<FString> Params );

	FChatCommandResponse ClearBodiesCommand(TArray<FString> Params );

	FChatCommandResponse WaystoneCooldownCommand(TArray<FString> Params );

	FChatCommandResponse WaterQualityCommand(TArray<FString> Params );

	FChatCommandResponse EditQuests(AIPlayerController* CallingPlayer, TArray<FString> Params );

	FChatCommandResponse WhisperCommand(AIPlayerController* CallingPlayer, TArray<FString> Params);
	FChatCommandResponse WhisperRCONCommand(TArray<FString> Params);
	FChatCommandResponse WhisperAllCommand(AIPlayerController* CallingPlayer, TArray<FString> Params);
	FChatCommandResponse WhisperAllRCONCommand(TArray<FString> Params);
	FChatCommandResponse WhisperAllRCONExecute(TArray<FString>& Params, AIPlayerState* const CallingPlayer = nullptr);

	FChatCommandResponse ListPOICommand(TArray<FString> Params );

	FChatCommandResponse ListRolesCommand(TArray<FString> Params );

	FChatCommandResponse ListWatersCommand(TArray<FString> Params );

	FChatCommandResponse ListWaystonesCommand(TArray<FString> Params );

	FChatCommandResponse ListQuestsCommand(TArray<FString> Params, FAsyncChatCommandCallback& Callback);

	void OnListQuestsCommand(TArray<FPrimaryAssetId> QuestAssetIds, FAsyncChatCommandCallback Callback);

	// Client/Local Command Functions

	FChatCommandResponse ClearChatLocalCommand(AIPlayerController* CallingPlayer, TArray<FString> Params);

	FChatCommandResponse RespawnLocalCommand(AIPlayerController* CallingPlayer, TArray<FString> Params);

	FChatCommandResponse MapBugLocalCommand(AIPlayerController* CallingPlayer, TArray<FString> Params);

	FChatCommandResponse MutePlayerLocalCommand(AIPlayerController* CallingPlayer, TArray<FString> Params);

	FChatCommandResponse ForceGCLocalCommand(AIPlayerController* CallingPlayer, TArray<FString> Params);

	FChatCommandResponse MemreportCommand(AIPlayerController* CallingPlayer, TArray<FString> Params);

	FChatCommandResponse AudioMemreportCommand(AIPlayerController* CallingPlayer, TArray<FString> Params);

	FChatCommandResponse GiveWoundCommand(AIPlayerController* CallingPlayer, TArray<FString> Params);

	FChatCommandResponse EnableDetachGroundTraces(AIPlayerController* CallingPlayer, TArray<FString> Params);

	FChatCommandResponse EnableDetachFallDamageCheck(AIPlayerController* CallingPlayer, TArray<FString> Params);


#if WITH_EDITOR
	FChatCommandResponse DebugBloodTex(AIPlayerController* CallingPlayer, TArray<FString> Params);
#endif

	FChatCommandResponse CopyStiffBodyLocalCommand(AIPlayerController* CallingPlayer, TArray<FString> Params);

	FChatCommandResponse FixStiffBodyLocalCommand(AIPlayerController* CallingPlayer, TArray<FString> Params);

	FChatCommandResponse DamageParticleLocalCommand(AIPlayerController* CallingPlayer, TArray<FString> Params);

	FChatCommandResponse BugSnapLocalCommand(AIPlayerController* CallingPlayer, TArray<FString> Params);

	FChatCommandResponse FlushLevelStreamingLocalCommand(AIPlayerController* CallingPlayer, TArray<FString> Params);

	FChatCommandResponse DebugAILocalCommand(AIPlayerController* CallingPlayer, TArray<FString> Params);

	FChatCommandResponse ToggleIKLocalCommand(AIPlayerController* CallingPlayer, TArray<FString> Params);

	FChatCommandResponse DemoRecLocalCommand(AIPlayerController* CallingPlayer, TArray<FString> Params);
	FChatCommandResponse DemoDownloadLocalCommand(AIPlayerController* CallingPlayer, TArray<FString> Params);

	FChatCommandResponse DemoStopLocalCommand(AIPlayerController* CallingPlayer, TArray<FString> Params);

	FChatCommandResponse ServerPerfTest(AIPlayerController* CallingPlayer, TArray<FString> Params);

	FChatCommandResponse SetNewMovementCommand(AIPlayerController* CallingPlayer, TArray<FString> Params);

	FChatCommandResponse ClearCooldownsCommand(AIPlayerController* CallingPlayer, TArray<FString> Params);
	// Clear Effects
	FChatCommandResponse ClearEffectsCommand(AIPlayerController* CallingPlayer, TArray<FString> Params);

	FChatCommandResponse ClearMapFog(AIPlayerController* CallingPlayer, TArray<FString> Params);

	void SetMapRevealerDropOffDistance(const TWeakObjectPtr<UMapRevealerComponent> MapRevealComponentWeakPtr, const float RevealDropOffDistance);

	FChatCommandResponse RestoreMapFog(AIPlayerController* CallingPlayer, TArray<FString> Params);

#if WITH_VIVOX
	FChatCommandResponse SwitchVoiceChannelCommand(AIPlayerController* CallingPlayer, TArray<FString> Params);
	FChatCommandResponse VoiceChatVolumeCommand(AIPlayerController* CallingPlayer, TArray<FString> Params);
	FChatCommandResponse VoiceChatMuteCommand(AIPlayerController* CallingPlayer, TArray<FString> Params);
	FChatCommandResponse VoiceChatUnmuteCommand(AIPlayerController* CallingPlayer, TArray<FString> Params);
#endif

	FChatCommandResponse DumpCommandsRCONCommand(TArray<FString> Params);


	// Common ChatCommandResponses
	FChatCommandResponse GetResponseCmdModifyAttributeNoProperty(const FString& AttributeName)
	{
		const FFormatNamedArguments Arguments{
			{ TEXT("Attribute"), FText::FromString(AttributeName) }
		};

		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdModifyAttributeNoProperty"), Arguments);
	}

	FChatCommandResponse GetResponseCmdNullObject(const FString& ObjectName)
	{
		const FFormatNamedArguments Args{
			{ TEXT("Object"), FText::FromString(ObjectName) }
		};

		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdNullObject"), Args);
	}

	FChatCommandResponse GetResponseCmdSetAttribCantParseFloat(const FString& FloatText)
	{
		const FFormatNamedArguments Arguments{
			{ TEXT("Number"), FText::FromString(FloatText) }
		};

		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdSetAttribCantParseFloat"), Arguments);
	}

	FChatCommandResponse GetResponseCmdInvalidUsername(const FString& Username)
	{
		const FFormatNamedArguments Arguments{
			{ TEXT("Username"), FText::FromString(Username) }
		};

		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdInvalidUsername"), Arguments);
	}
};

