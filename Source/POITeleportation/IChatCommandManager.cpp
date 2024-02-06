// Copyright 2019-2022 Alderon Games Pty Ltd, All Rights Reserved.

#include "IChatCommandManager.h"

#include "AlderonChat.h"
#include "IGameplayStatics.h"
#include "Player/IBaseCharacter.h"
#include "Abilities/CoreAttributeSet.h"
#include "Abilities/POTAbilitySystemGlobals.h"
#include "Kismet/GameplayStatics.h"
#include "Quests/IPointOfInterest.h"
#include "Quests/IPOI.h"
#include "Components/SphereComponent.h"
#include "Misc/DefaultValueHelper.h"
#include "Online/IGameState.h"
#include "IGameInstance.h"
#include "Abilities/CoreAttributeSet.h"
#include "Abilities/POTAbilitySystemGlobals.h"
#include "Online/IGameSession.h"
#include "GameMode/IGameMode.h"
#include "IWorldSettings.h"
#include "UI/IChatWindow.h"
#include "World/IUltraDynamicSky.h"
#include "UI/IGameHUD.h"
#include "IGameInstance.h"
#include "UI/IMainGameHUDWidget.h"
#include "LandscapeStreamingProxy.h"
#include "World/IWaystone.h"
#include "Player/IAdminCharacter.h"
#include "Items/IMeatChunk.h"
#include "ChatCommands/ChatCommandDataAsset.h"
#include "UI/IQuestEditorWidget.h"
#include "Engine/ActorChannel.h"
#include "AlderonRemoteConfig.h"
#include "Components/ICreatorModeObjectComponent.h"
#include "NavigationSystem.h"
#include "Critters/ICritterPawn.h"
#include "World/ILevelStreamingLoader.h"
#include "AlderonDownload.h"
#include "AlderonCommon.h"
#include "Net/IVoiceSubsystem.h"
#include "TitanAssetManager.h"
#include "Abilities/POTAbilityAsset.h"
#include "Abilities/POTGameplayAbility.h"
#include "Abilities/POTGameplayEffect.h"
#include "Abilities/POTAbilityTypes.h"
#include "MapFog.h"
#include "MapRevealerComponent.h"

#if WITH_BATTLEYE_SERVER
	#include "IBattlEyeServer.h"
#endif

#define COMMAND_HIDDEN true
#define COMMAND_SHOWN false

#define REQ_PERMISSION true
#define NO_PERMISSION false

#define ECC_Terrain ECC_GameTraceChannel5

#define CLAMP_MINMAX 10000000.0f

void AIChatCommandManager::ProcessBattlEyeCommand(const FString& Command)
{
	// TODO: Need poncho to help re-add this one
	return;

	/*

	if (Command.Len() == 0) return;

	FString TrimmedCommand = Command;

	if (Command[0] == '#') TrimmedCommand = Command.RightChop(1);

	FString LogResult = TEXT("");

	if (TrimmedCommand.Len() == 0)
	{
		LogResult = FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdEmptyRConString")).ToString();
	}
	else
	{
		FChatCommandResponse CommandResult;
		if (TrimmedCommand[0] == '$')
		{
			UE_LOG(TitansLog, Log, TEXT("UIChatCommandManager::ProcessBattlEyeCommand(): Specifying Player for command Invocation"));

			TArray<FString> Params;
			TrimmedCommand = TrimmedCommand.RightChop(1); // remove the $
			FString IdString;
			TrimmedCommand.Split(TEXT(" "), &IdString, &TrimmedCommand);  // Break up string into id and command

			AIPlayerController* TargetPlayerController = nullptr;

			UE_LOG(TitansLog, Log, TEXT("UIChatCommandManager::ProcessBattlEyeCommand(): finding player from IdString {%s}"), *IdString);


			if (IdString.IsNumeric()) // id is battleye id
			{
				#if WITH_BATTLEYE_SERVER
					if (IBattlEyeServer::IsAvailable())
					{
						TargetPlayerController = Cast<AIPlayerController>(IBattlEyeServer::Get().GetPlayerFromIndex(FCString::Atoi(*IdString)));
					}
				#endif
			}

			for (AIChatCommandManager* IChatCommandManager : AllChatCommandManagers)
			{
				if (!TargetPlayerController)
				{
					UE_LOG(TitansLog, Log, TEXT("UIChatCommandManager::ProcessBattlEyeCommand(): could not get player from BE Id, using username/AGID with IdString {%s}"), *IdString);

					TargetPlayerController = PlayerControllerFromUsername(IChatCommandManager, IdString); // id is username or AGID
					if (TargetPlayerController) break;
				}
			}

			if (!TargetPlayerController)
			{
				const FFormatNamedArguments Arguments{
					{ TEXT("Id"), FText::FromString(IdString) }
				};
				CommandResult = AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdBattlEyeCommandInvalidId"), Arguments);
			}
			else
			{
				CommandResult = ProcessChatCommand(TargetPlayerController, TrimmedCommand);
			}
		}
		else
		{
			CommandResult = ProcessStandaloneChatCommand(nullptr, TrimmedCommand);
		}


		if (CommandResult.bIsLocalized)
		{
			LogResult = FString::Printf(TEXT("(%s): %s"), *Command, *FText::Format(FText::FromStringTable(CommandResult.TableId, CommandResult.Key), CommandResult.GetArguments()).ToString());
		}
		else
		{
			LogResult = CommandResult.NonLocalizedString;
		}
	}


	UE_LOG(TitansLog, Log, TEXT("UIChatCommandManager::ProcessBattlEyeCommand(): %s"), *LogResult);

#if WITH_BATTLEYE_SERVER
	if (IBattlEyeServer::IsAvailable())
	{
		IBattlEyeServer::Get().SendChatCommandResult(static_cast<int>(EChatChannel::Global), LogResult);
	}
#endif
*/
}

AIChatCommandManager* AIChatCommandManager::Get(UObject* WorldContextObject)
{
	if (!IsValid(WorldContextObject))
	{
		return nullptr;
	}

	UIGameInstance* IGameInstance = UIGameplayStatics::GetIGameInstance(WorldContextObject);
	check(IGameInstance);
	if (!IGameInstance)
	{
		return nullptr;
	}

	for (AIChatCommandManager* IChatCommandManager : IGameInstance->ChatCommandManagers)
	{
		if (!IsValid(IChatCommandManager))
		{
			continue;
		}
		if (IChatCommandManager->GetWorld() == WorldContextObject->GetWorld())
		{
			return IChatCommandManager;
		}
	}

	AIChatCommandManager* IChatCommandManager = nullptr;

	UWorld* WorldContext = WorldContextObject->GetWorld();
	if (IsValid(WorldContext))
	{
		IChatCommandManager = WorldContext->SpawnActor<AIChatCommandManager>();
		IGameInstance->ChatCommandManagers.AddUnique(IChatCommandManager);
	}

	return IChatCommandManager;
}

void AIChatCommandManager::BeginPlay()
{
	UIGameInstance* IGameInstance = UIGameplayStatics::GetIGameInstance(this);
	check(IGameInstance);
	if (!IGameInstance)
	{
		return;
	}

	IGameInstance->ChatCommandManagers.AddUnique(this);
	Super::BeginPlay();
}

void AIChatCommandManager::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	UIGameInstance* IGameInstance = UIGameplayStatics::GetIGameInstance(this);
	check(IGameInstance);
	if (!IGameInstance)
	{
		return;
	}

	IGameInstance->ChatCommandManagers.Remove(this);
	Super::EndPlay(EndPlayReason);
}

void AIChatCommandManager::InitChatCommandManager()
{
	Super::InitChatCommandManager();

	// Global Commands for Everyone (Shown)

	RegisterChatCommand(TEXT("Respawn"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdRespawnDescription")))
		.BindClient(this, &AIChatCommandManager::RespawnLocalCommand);

	RegisterChatCommand(TEXT("MapBug"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdMapBugDescription")))
		.BindClient(this, &AIChatCommandManager::MapBugLocalCommand);

	RegisterChatCommand(TEXT("Mute"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdMuteDescription")))
		.BindClient(this, &AIChatCommandManager::MutePlayerLocalCommand);

	RegisterChatCommand(TEXT("Unmute"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdUnmuteDescription")))
		.BindClient(this, &AIChatCommandManager::MutePlayerLocalCommand);

	RegisterChatCommand(TEXT("Clear"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdClearChatDescription")))
		.BindClient(this, &AIChatCommandManager::ClearChatLocalCommand);

	RegisterChatCommand(TEXT("Rules"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdRulesDescription")))
		.BindServer(this, &AIChatCommandManager::RulesCommand);

	RegisterChatCommand(TEXT("Motd"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdMotdDescription")))
		.BindServer(this, &AIChatCommandManager::MotdCommand);

	RegisterChatCommand(TEXT("Whisper"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdWhisperDesc")))
		.BindServer(this, &AIChatCommandManager::WhisperCommand)
		.BindRCON(this, &AIChatCommandManager::WhisperRCONCommand);

	RegisterChatCommand(TEXT("WhisperAll"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdWhisperAllDesc")))
		.BindServer(this, &AIChatCommandManager::WhisperAllCommand)
		.BindRCON(this, &AIChatCommandManager::WhisperAllRCONCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("W"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdWDesc")))
		.BindServer(this, &AIChatCommandManager::WhisperCommand);

	// Hidden Everyone Debug Commands
	RegisterChatCommand(TEXT("BugSnap"), FText())
		.BindClient(this, &AIChatCommandManager::BugSnapLocalCommand)
		.AddFlags(COMMAND_HIDDEN);

	RegisterChatCommand(TEXT("ForceGC"), FText())
		.BindClient(this, &AIChatCommandManager::ForceGCLocalCommand)
		.AddFlags(COMMAND_HIDDEN);

	RegisterChatCommand(TEXT("CopyStiffBody"), FText())
		.BindClient(this, &AIChatCommandManager::CopyStiffBodyLocalCommand)
		.AddFlags(COMMAND_HIDDEN);

	RegisterChatCommand(TEXT("FixStiffBody"), FText())
		.BindClient(this, &AIChatCommandManager::FixStiffBodyLocalCommand)
		.AddFlags(COMMAND_HIDDEN);

	RegisterChatCommand(TEXT("DamageParticle"), FText())
		.BindClient(this, &AIChatCommandManager::DamageParticleLocalCommand)
		.AddFlags(COMMAND_HIDDEN);

	RegisterChatCommand(TEXT("FlushLevelStreaming"), FText())
		.BindClient(this, &AIChatCommandManager::FlushLevelStreamingLocalCommand)
		.AddFlags(COMMAND_HIDDEN);

	RegisterChatCommand(TEXT("ToggleIK"), FText())
		.BindClient(this, &AIChatCommandManager::ToggleIKLocalCommand)
		.AddFlags(COMMAND_HIDDEN);

	RegisterChatCommand(TEXT("Crash"), FText())
		.BindClient(this, &AIChatCommandManager::CrashCommand)
		.AddFlags(COMMAND_HIDDEN);

	RegisterChatCommand(TEXT("MemReport"), FText())
		.BindClient(this, &AIChatCommandManager::MemreportCommand)
		.AddFlags(COMMAND_HIDDEN);

	RegisterChatCommand(TEXT("AudioMemReport"), FText())
		.BindClient(this, &AIChatCommandManager::AudioMemreportCommand)
		.AddFlags(COMMAND_HIDDEN);

#if WITH_EDITOR
	RegisterChatCommand(TEXT("DebugBloodTex"), FText())
		.BindClient(this, &AIChatCommandManager::DebugBloodTex)
		.AddFlags(COMMAND_HIDDEN);
#endif

	// Demo - Replay Recording
	RegisterChatCommand(TEXT("DemoRec"), FText())
		.BindClient(this, &AIChatCommandManager::DemoRecLocalCommand)
		.AddFlags(COMMAND_HIDDEN);

	RegisterChatCommand(TEXT("DemoDownload"), FText())
		.BindClient(this, &AIChatCommandManager::DemoDownloadLocalCommand)
		.AddFlags(COMMAND_HIDDEN);

	RegisterChatCommand(TEXT("DemoStop"), FText())
		.BindClient(this, &AIChatCommandManager::DemoStopLocalCommand)
		.AddFlags(COMMAND_HIDDEN);

	RegisterChatCommand(TEXT("DebugAI"), FText())
		.BindClient(this, &AIChatCommandManager::DebugAILocalCommand)
		.AddFlags(COMMAND_HIDDEN);

	RegisterChatCommand(TEXT("ServerPerfTest"), FText())
		.BindServer(this, &AIChatCommandManager::ServerPerfTest)
		.AddFlags(COMMAND_HIDDEN);

	// Serverside Admin Commands
	RegisterChatCommand(TEXT("ServerAutoRecord"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdServerAutoRecordDescription")))
		.BindServer(this, &AIChatCommandManager::ServerAutoRecord)
		.BindRCON(this, &AIChatCommandManager::ServerAutoRecordRCON)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("Heal"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdHealDescription")))
		.BindServer(this, &AIChatCommandManager::HealPlayerCommand)
		.BindRCON(this, &AIChatCommandManager::HealRCONCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("Promote"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdPromoteDescription")))
		.BindServer(this, &AIChatCommandManager::PromoteCommand)
		.BindRCON(this, &AIChatCommandManager::PromoteRCONCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION, TEXT("Promote"));

	RegisterChatCommand(TEXT("Demote"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdDemoteDescription")))
		.BindServer(this, &AIChatCommandManager::DemoteCommand)
		.BindRCON(this, &AIChatCommandManager::DemoteRCONCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION, TEXT("Promote"));

	RegisterChatCommand(TEXT("Teleport"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdTeleportDescription")))
		.BindServer(this, &AIChatCommandManager::TeleportCommand)
		.BindRCON(this, &AIChatCommandManager::TeleportRCONCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("Goto"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdGotoDescription")))
		.BindServer(this, &AIChatCommandManager::GotoCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION, TEXT("Teleport"));

	RegisterChatCommand(TEXT("Bring"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdBringDescription")))
		.BindServer(this, &AIChatCommandManager::BringCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION, TEXT("Teleport"));

	RegisterChatCommand(TEXT("BringAll"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdBringAllDescription")))
		.BindServer(this, &AIChatCommandManager::BringAllCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION, TEXT("TeleportAll"));

	RegisterChatCommand(TEXT("BringAllOfSpecies"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdBringAllOfSpeciesDescription")))
		.BindServer(this, &AIChatCommandManager::BringAllOfSpeciesCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION, TEXT("TeleportAllOfSpecies"));

	RegisterChatCommand(TEXT("BringAllOfDietType"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdBringAllOfDietTypeDescription")))
		.BindServer(this, &AIChatCommandManager::BringAllOfDietTypeCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION, TEXT("TeleportAllOfDietType"));

	RegisterChatCommand(TEXT("SkipShed"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdSkipShedDescription")))
		.BindServer(this, &AIChatCommandManager::SkipShedCommand)
		.BindRCON(this, &AIChatCommandManager::SkipShedRCONCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION, TEXT("SkipShed"));

	RegisterChatCommand(TEXT("SetMarks"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdSetMarksDescription")))
		.BindServer(this, &AIChatCommandManager::SetMarksCommand)
		.BindRCON(this, &AIChatCommandManager::SetMarksRCONCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION, TEXT("SetMarks"));

	RegisterChatCommand(TEXT("SetMarksAll"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdSetMarksAllDescription")))
		.BindServer(this, &AIChatCommandManager::SetMarksAllCommand)
		.BindRCON(this, &AIChatCommandManager::SetMarksAllRCONCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION, TEXT("SetMarksAll"));

	RegisterChatCommand(TEXT("AddMarks"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdAddMarksDescription")))
		.BindServer(this, &AIChatCommandManager::AddMarksCommand)
		.BindRCON(this, &AIChatCommandManager::AddMarksRCONCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION, TEXT("SetMarks"));

	RegisterChatCommand(TEXT("AddMarksAll"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdAddMarksAllDescription")))
		.BindRCON(this, &AIChatCommandManager::AddMarksAllRCONCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("RemoveMarks"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdRemoveMarksDescription")))
		.BindServer(this, &AIChatCommandManager::RemoveMarksCommand)
		.BindRCON(this, &AIChatCommandManager::RemoveMarksRCONCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION, TEXT("SetMarks"));

	RegisterChatCommand(TEXT("Health"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdSetHealthDescription")))
		.BindServer(this, &AIChatCommandManager::SetAttributeCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION, TEXT("Modify Attribute"));

	RegisterChatCommand(TEXT("Stamina"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdSetStaminaDescription")))
		.BindServer(this, &AIChatCommandManager::SetAttributeCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION, TEXT("Modify Attribute"));

	RegisterChatCommand(TEXT("Hunger"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdSetHungerDescription")))
		.BindServer(this, &AIChatCommandManager::SetAttributeCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION, TEXT("Modify Attribute"));

	RegisterChatCommand(TEXT("Thirst"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdSetThirstDescription")))
		.BindServer(this, &AIChatCommandManager::SetAttributeCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION, TEXT("Modify Attribute"));

	RegisterChatCommand(TEXT("Oxygen"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdSetOxygenDescription")))
		.BindServer(this, &AIChatCommandManager::SetAttributeCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION, TEXT("Modify Attribute"));

	RegisterChatCommand(TEXT("Godmode"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdGodmodeDescription")))
		.BindServer(this, &AIChatCommandManager::GodModeCommand)
		.BindRCON(this, &AIChatCommandManager::GodModeRCONCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("GiveQuest"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdGiveQuestDescription")))
		.BindServer(this, &AIChatCommandManager::GiveQuestCommand)
		.BindRCON(this, &AIChatCommandManager::GiveQuestRCONCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("CompleteQuest"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdCompleteQuestDescription")))
		.BindServer(this, &AIChatCommandManager::CompleteQuestCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("ModAttr"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdModAttrDescription")))
		.BindServer(this, &AIChatCommandManager::ModifyAttributeCommand)
		.BindRCON(this, &AIChatCommandManager::ModifyAttributeRCONCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION, TEXT("Modify Attribute"));

	RegisterChatCommand(TEXT("GetAttr"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdGetAttrDescription")))
		.BindServer(this, &AIChatCommandManager::GetAttributeCommand)
		.BindRCON(this, &AIChatCommandManager::GetAttributeRCONCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION, TEXT("Get Attribute"));

	RegisterChatCommand(TEXT("GetProp"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdGetPropDescription")))
		.BindServer(this, &AIChatCommandManager::GetPropertyCommand)
		.BindRCON(this, &AIChatCommandManager::GetPropertyRCONCommand)
		.AddFlags(COMMAND_HIDDEN, REQ_PERMISSION, TEXT("Get Property"));

	RegisterChatCommand(TEXT("ListProps"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdListPropsDescription")))
		.BindServer(this, &AIChatCommandManager::ListPropertiesCommand)
		.BindRCON(this, &AIChatCommandManager::ListPropertiesRCONCommand)
		.AddFlags(COMMAND_HIDDEN, REQ_PERMISSION, TEXT("List Property"));

	RegisterChatCommand(TEXT("ListCurves"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdListCurvesDescription")))
		.BindServer(this, &AIChatCommandManager::ListCurveValuesCommand)
		.BindRCON(this, &AIChatCommandManager::ListCurveValuesRCONCommand)
		.AddFlags(COMMAND_HIDDEN, REQ_PERMISSION, TEXT("List Curves"));

	RegisterChatCommand(TEXT("ListAbilities"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdListAbilitiesDescription")))
		.BindServer(this, &AIChatCommandManager::ListGameplayAbilitiesCommand)
		.BindRCON(this, &AIChatCommandManager::ListGameplayAbilitiesRCONCommand)
		.AddFlags(COMMAND_HIDDEN, REQ_PERMISSION, TEXT("List Abilities"));

	RegisterChatCommand(TEXT("InspectAbility"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdInspectAbilityDescription")))
		.BindServer(this, &AIChatCommandManager::InspectGameplayAbilityCommand)
		.BindRCON(this, &AIChatCommandManager::InspectGameplayAbilityRCONCommand)
		.AddFlags(COMMAND_HIDDEN, REQ_PERMISSION, TEXT("Inspect Ability"));

	RegisterChatCommand(TEXT("GetAllAttr"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdGetAllAttrDescription")))
		.BindServer(this, &AIChatCommandManager::GetAllAttributesCommand)
		.BindRCON(this, &AIChatCommandManager::GetAllAttributesRCONCommand)
		.AddFlags(COMMAND_HIDDEN, REQ_PERMISSION, TEXT("Get Attribute"));

	RegisterChatCommand(TEXT("GetAllAttrLocal"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdGetAllAttrLocalDescription")))
		.BindClient(this, &AIChatCommandManager::GetAllAttributesCommand)
		.AddFlags(COMMAND_HIDDEN, REQ_PERMISSION, TEXT("Get Attribute"));

	RegisterChatCommand(TEXT("GetAttrLocal"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdGetAttrLocalDescription")))
		.BindClient(this, &AIChatCommandManager::GetAttributeCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION, TEXT("Get Attribute"));

	RegisterChatCommand(TEXT("SetAttr"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdSetAttrDescription")))
		.BindServer(this, &AIChatCommandManager::ModifyAttributeCommand)
		.BindRCON(this, &AIChatCommandManager::ModifyAttributeRCONCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION, TEXT("Set Attribute"));

	RegisterChatCommand(TEXT("SetAttrAll"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdSetAttrAllDescription")))
		.BindRCON(this, &AIChatCommandManager::SetAttributeAllRCONCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION, TEXT("Set Attribute All"));

	RegisterChatCommand(TEXT("RewardGrowth"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdAwardGrowthDescription")))
		.BindServer(this, &AIChatCommandManager::AwardGrowthCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION, TEXT("Reward Growth"));

	RegisterChatCommand(TEXT("RewardWellRested"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdAwardWellRestedDescription")))
		.BindServer(this, &AIChatCommandManager::AwardWellRestedBuffCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION, TEXT("Reward Well Rested"));

	RegisterChatCommand(TEXT("EditQuests"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdEditQuestsDescription")))
		.BindServer(this, &AIChatCommandManager::EditQuests)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("CrashServer"), FText())
		.BindRCON(this, &AIChatCommandManager::CrashServerCommand)
		.AddFlags(COMMAND_HIDDEN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("ResetHomeCave"), FText())
		.BindServer(this, &AIChatCommandManager::ResetHomeCaveSaveInfoCommand)
		.AddFlags(COMMAND_HIDDEN, REQ_PERMISSION);

	// Standalone commands
	RegisterChatCommand(TEXT("ListPOI"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdListPoiDescription")))
		.BindRCON(this, &AIChatCommandManager::ListPOICommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("ListQuests"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdListQuestDescription")))
		.BindRCON(this, &AIChatCommandManager::ListQuestsCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("ListRoles"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdListRolesDescription")))
		.BindRCON(this, &AIChatCommandManager::ListRolesCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("ListWaters"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdListWatersDescription")))
		.BindRCON(this, &AIChatCommandManager::ListWatersCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("ListWaystones"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdListWaystonesDescription")))
		.BindRCON(this, &AIChatCommandManager::ListWaystonesCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("HealAll"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdHealAllDescription")))
		.BindRCON(this, &AIChatCommandManager::HealAllCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("ProfileServer"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdProfileServerDescription")))
		.BindRCON(this, &AIChatCommandManager::ProfileCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("WaterQuality"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdWaterDescription")))
		.BindRCON(this, &AIChatCommandManager::WaterQualityCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("WaystoneCooldown"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdWaystoneDescription")))
		.BindRCON(this, &AIChatCommandManager::WaystoneCooldownCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("Restart"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdRestartDescription")))
		.BindRCON(this, &AIChatCommandManager::RestartCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("CancelRestart"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdCancelRestartDescription")))
		.BindRCON(this, &AIChatCommandManager::CancelRestartCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION, TEXT("Restart"));

	RegisterChatCommand(TEXT("TimeOfDay"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdSetTimeDescription")))
		.BindRCON(this, &AIChatCommandManager::SetTimeCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION, TEXT("Time of Day"));

	RegisterChatCommand(TEXT("Day"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdDayDescription")))
		.BindRCON(this, &AIChatCommandManager::SetTimeCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION, TEXT("Time of Day"));

	RegisterChatCommand(TEXT("Night"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdNightDescription")))
		.BindRCON(this, &AIChatCommandManager::SetTimeCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION, TEXT("Time of Day"));

	RegisterChatCommand(TEXT("Morning"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdMorningDescription")))
		.BindRCON(this, &AIChatCommandManager::SetTimeCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION, TEXT("Time of Day"));

	RegisterChatCommand(TEXT("ClearBodies"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdClearBodiesDescription")))
		.BindRCON(this, &AIChatCommandManager::ClearBodiesCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("Weather"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdWeatherDescription")))
		.BindRCON(this, &AIChatCommandManager::SetWeatherCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("Save"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdSaveDescription")))
		.BindRCON(this, &AIChatCommandManager::SaveCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("Load"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdLoadDescription")))
		.BindRCON(this, &AIChatCommandManager::LoadCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("Ban"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdBanDescription")))
		.BindServer(this, &AIChatCommandManager::BanCommand)
		.BindRCON(this, &AIChatCommandManager::BanRCONCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("Unban"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdUnbanDescription")))
		.BindRCON(this, &AIChatCommandManager::UnbanCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("Kick"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdKickDescription")))
		.BindServer(this, &AIChatCommandManager::KickCommand)
		.BindRCON(this, &AIChatCommandManager::KickRCONCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("Announce"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdAnnounceDescription")))
		.BindServer(this, &AIChatCommandManager::AnnounceCommand)
		.BindRCON(this, &AIChatCommandManager::AnnounceRCONCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("TeleportAll"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdTeleportAllDescription")))
		.BindRCON(this, &AIChatCommandManager::TeleportAllCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION, TEXT("Teleport All"));

	RegisterChatCommand(TEXT("EnterHomecave"), FText())
		.BindServer(this, &AIChatCommandManager::EnterHomecaveCommand)
		.AddFlags(COMMAND_HIDDEN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("LeaveHomecave"), FText())
		.BindServer(this, &AIChatCommandManager::LeaveHomecaveCommand)
		.AddFlags(COMMAND_HIDDEN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("NewMovement"), FText())
		.BindServer(this, &AIChatCommandManager::SetNewMovementCommand)
		.AddFlags(COMMAND_HIDDEN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("ClearCreatorObjects"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdClearCreatorObjectsDescription")))
		.BindRCON(this, &AIChatCommandManager::ClearCreatorObjectsCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("ListPlayers"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdListPlayersDescription")))
		.BindRCON(this, &AIChatCommandManager::ListPlayersRCONCommand)
		.AddFlags(COMMAND_SHOWN, NO_PERMISSION);

	RegisterChatCommand(TEXT("ListPlayerPositions"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdListPlayerPositionsDescription")))
		.BindRCON(this, &AIChatCommandManager::ListPlayerPositionsRCONCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("PlayerInfo"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdPlayerInfoDescription")))
		.BindRCON(this, &AIChatCommandManager::PlayerInfoRCONCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("ServerMute"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdServerMuteDescription")))
		.BindRCON(this, &AIChatCommandManager::ServerMuteRCONCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("ServerUnmute"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdServerUnmuteDescription")))
		.BindRCON(this, &AIChatCommandManager::ServerUnmuteRCONCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("Whitelist"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdWhitelistDescription")))
		.BindRCON(this, &AIChatCommandManager::WhitelistRCONCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("DelWhitelist"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdDelWhitelistDescription")))
		.BindRCON(this, &AIChatCommandManager::DelWhitelistRCONCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("ReloadBans"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdReloadBansDescription")))
		.BindRCON(this, &AIChatCommandManager::ReloadBansRCONCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("ReloadMutes"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdReloadMutesDescription")))
		.BindRCON(this, &AIChatCommandManager::ReloadMutesRCONCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("ReloadWhitelist"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdReloadWhitelistDescription")))
		.BindRCON(this, &AIChatCommandManager::ReloadWhitelistRCONCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("ReloadRules"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdReloadRulesDescription")))
		.BindRCON(this, &AIChatCommandManager::ReloadRulesRCONCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("ReloadMOTD"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdReloadMOTDDescription")))
		.BindRCON(this, &AIChatCommandManager::ReloadMOTDRCONCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("LoadCreatorMode"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdLoadCreatorDescription")))
		.BindRCON(this, &AIChatCommandManager::LoadCreatorModeCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("RemoveCreatorMode"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdRemoveCreatorSaveDescription")))
		.BindRCON(this, &AIChatCommandManager::RemoveCreatorModeSaveCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("ListCreatorMode"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdListCreatorSavesDescription")))
		.BindRCON(this, &AIChatCommandManager::ListCreatorModeSavesCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("SaveCreatorMode"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdSaveCreatorDescription")))
		.BindRCON(this, &AIChatCommandManager::SaveCreatorModeCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("ResetCreatorMode"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdResetCreatorDescription")))
		.BindRCON(this, &AIChatCommandManager::ResetCreatorModeCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("SetWound"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdGiveWoundDescription")))
		.BindServer(this, &AIChatCommandManager::GiveWoundCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("SetPermaWound"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdGivePermaWoundDescription")))
		.BindServer(this, &AIChatCommandManager::GiveWoundCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("EnableDetachGroundTraces"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdEnableDetachGroundTraces")))
		.BindServer(this, &AIChatCommandManager::EnableDetachGroundTraces)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("EnableDetachFallDamageCheck"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdEnableDetachFallDamageCheck")))
		.BindServer(this, &AIChatCommandManager::EnableDetachFallDamageCheck)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

#if WITH_VIVOX

	RegisterChatCommand(TEXT("SwitchVoiceChannel"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdVoiceChatSwitchChannel")))
		.BindServer(this, &AIChatCommandManager::SwitchVoiceChannelCommand)
		.AddFlags(COMMAND_HIDDEN);
	RegisterChatCommand(TEXT("VoiceChatVolume"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdVoiceChatVolume")))
		.BindServer(this, &AIChatCommandManager::VoiceChatVolumeCommand)
		.AddFlags(COMMAND_HIDDEN);
	RegisterChatCommand(TEXT("VoiceChatMute"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdVoiceChatMute")))
		.BindServer(this, &AIChatCommandManager::VoiceChatMuteCommand)
		.AddFlags(COMMAND_HIDDEN);
	RegisterChatCommand(TEXT("VoiceChatUnmute"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdVoiceChatUnmute")))
		.BindServer(this, &AIChatCommandManager::VoiceChatUnmuteCommand)
		.AddFlags(COMMAND_HIDDEN);

#endif

	// ClearEffects
	RegisterChatCommand(TEXT("ClearEffects"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdClearEffectsDescription")))
		.BindServer(this, &AIChatCommandManager::ClearEffectsCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	// ClearCooldowns
	RegisterChatCommand(TEXT("ClearCooldowns"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdClearCooldownsDescription")))
		.BindServer(this, &AIChatCommandManager::ClearCooldownsCommand)
		.AddFlags(COMMAND_SHOWN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("DumpCommands"), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdDumpCommandsDescription")))
		.BindRCON(this, &AIChatCommandManager::DumpCommandsRCONCommand)
		.AddFlags(COMMAND_HIDDEN, REQ_PERMISSION);

	// MapFog Controls
	RegisterChatCommand(TEXT("ClearMapFog"), FText())
		.BindClient(this, &AIChatCommandManager::ClearMapFog)
		.AddFlags(COMMAND_HIDDEN, REQ_PERMISSION);

	RegisterChatCommand(TEXT("RestoreMapFog"), FText())
		.BindClient(this, &AIChatCommandManager::RestoreMapFog)
		.AddFlags(COMMAND_HIDDEN, REQ_PERMISSION);
}

bool AIChatCommandManager::CheckAdminAGID(const FString& AGID) const
{
	if (AIPlayerController* IPlayerController = Cast<AIPlayerController>(PlayerControllerFromUsername(this, AGID)))
	{
		return CheckAdmin(IPlayerController);
	}

	AIGameMode* IGameMode = Cast<AIGameMode>(UGameplayStatics::GetGameMode(this));
	if (!IGameMode)
	{
		return false;
	}
	AIGameSession* IGameSession = Cast<AIGameSession>(IGameMode->GameSession);
	if (!IGameSession)
	{
		return false;
	}
	return IGameSession->IsAdminID(AGID) || IGameSession->IsDevID(AGID);
}

bool AIChatCommandManager::CheckAdmin(const AAlderonPlayerController* PlayerController) const
{
	if (PlayerController == nullptr)
	{
		return false;
	}

	const AIPlayerController* const IPlayerController = Cast<AIPlayerController>(PlayerController);
	if (!IPlayerController)
	{
		return false;
	}

	const AIPlayerState* const IPlayerState = Cast<AIPlayerState>(IPlayerController->GetPlayerState<AIPlayerState>());
	if (!IPlayerState)
	{
		return false;
	}

	if (IPlayerState->IsServerAdmin())
	{
		return true;
	}

	if (IPlayerState->IsGameDev())
	{
		return true;
	}

	if (!IPlayerController->IsValidatedCheck() && GetNetMode() != ENetMode::NM_Standalone)
	{
		return false;
	}

	const AIGameMode* const IGameMode = Cast<AIGameMode>(UGameplayStatics::GetGameMode(IPlayerController->GetWorld()));
	if (IGameMode == nullptr)
	{
		return false;
	}

	const AIGameSession* const IGameSession = Cast<AIGameSession>(IGameMode->GameSession);
	if (IGameSession == nullptr)
	{
		return false;
	}

	return IGameSession->IsAdmin(IPlayerController);
}

bool AIChatCommandManager::GodMode(AIPlayerController* IPlayerController)
{
	AIBaseCharacter* const IBaseCharacter = Cast<AIBaseCharacter>(IPlayerController->GetPawn());
	if (!IsValid(IBaseCharacter) || IBaseCharacter->GetIsDying())
	{
		return false;
	}

	IBaseCharacter->SetGodmode(IBaseCharacter->CanBeDamaged());
	IBaseCharacter->ResetStats();
	IBaseCharacter->GetDamageWounds_Mutable().Reset();

	if (IBaseCharacter->CanBeDamaged())
	{
		return false;
	}
	else
	{
		return true;
	}
}

FChatCommandResponse AIChatCommandManager::ProfileCommand(TArray<FString> Params)
{
	if (!GEngine)
	{
		return GetResponseCmdNullObject(TEXT("GEngine"));
	}
	if (!GetWorld())
	{
		return GetResponseCmdNullObject(TEXT("GetWorld()"));
	}

	if (Params.Num() != 2)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	FString StartOrStop = Params[1].ToLower();
	if (StartOrStop == "start")
	{
		GEngine->Exec(GetWorld(), TEXT("stat startfile"));
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdProfileStart"));
	}
	else if (StartOrStop == "stop")
	{
		GEngine->Exec(GetWorld(), TEXT("stat stopfile"));
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdProfileStop"));
	}
	else
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdProfileMustBeStartOrStop"));
	}
}

FChatCommandResponse AIChatCommandManager::MemreportCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	GEngine->Exec(CallingPlayer->GetWorld(), TEXT("Memreport -Full -Log"));
	GEngine->Exec(CallingPlayer->GetWorld(), TEXT("Memreport -Full"));
	return AIChatCommand::MakePlainResponse(TEXT("Memreport complete"));
}

FChatCommandResponse AIChatCommandManager::AudioMemreportCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	GEngine->Exec(CallingPlayer->GetWorld(), TEXT("au.debug.AudioMemReport"));
	return AIChatCommand::MakePlainResponse(TEXT("Generated audio mem report. Saved to MemReports folder."));
}

FChatCommandResponse AIChatCommandManager::ForceGCLocalCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	GEngine->ForceGarbageCollection(true);
	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdForceGCSuccess"));
}

FChatCommandResponse AIChatCommandManager::SetWeatherCommand(TArray<FString> Params)
{
	if (!GetWorld())
	{
		return GetResponseCmdNullObject(TEXT("GetWorld()"));
	}
	if (Params.Num() < 2)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}
	FString WeatherTypeStr = Params[1].ToLower();
	EWeatherType RequestedWeatherType = EWeatherType::ClearSky;

	if (WeatherTypeStr == TEXT("clearsky") || WeatherTypeStr == TEXT("none") || WeatherTypeStr == TEXT("clear"))
	{
		RequestedWeatherType = EWeatherType::ClearSky;
	}
	else if (WeatherTypeStr == TEXT("overcast"))
	{
		RequestedWeatherType = EWeatherType::Overcast;
	}
	else if (WeatherTypeStr == TEXT("fog"))
	{
		RequestedWeatherType = EWeatherType::Fog;
	}
	else if (WeatherTypeStr == TEXT("cloudy"))
	{
		RequestedWeatherType = EWeatherType::Cloudy;
	}
	else if (WeatherTypeStr == TEXT("rain"))
	{
		RequestedWeatherType = EWeatherType::Rain;
	}
	else if (WeatherTypeStr == TEXT("storm"))
	{
		RequestedWeatherType = EWeatherType::Storm;
	}
	else if (WeatherTypeStr == TEXT("snow"))
	{
		RequestedWeatherType = EWeatherType::Snow;
	}
	else
	{
		const FFormatNamedArguments Arguments{
			{ TEXT("WeatherType"), FText::FromString(WeatherTypeStr) }
		};

		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdWeatherNotSupported"), Arguments);
	}

	// Validate Accessor Objects
	AIWorldSettings* IWorldSettings = AIWorldSettings::GetWorldSettings(this);
	if (!IWorldSettings)
	{
		return GetResponseCmdNullObject(TEXT("IWorldSettings"));
	}
	AIUltraDynamicSky* Sky = IWorldSettings->UltraDynamicSky;
	if (!Sky)
	{
		return GetResponseCmdNullObject(TEXT("Sky"));
	}

	// Set WeatherTarget on Sky & Replicate it to everyone
	Sky->UpdateWeatherTargetType(RequestedWeatherType);

	const FFormatNamedArguments Arguments{
		{ TEXT("WeatherType"), FText::FromString(WeatherTypeStr) } 
	};

	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdWeatherSuccess"), Arguments);
}

FChatCommandResponse AIChatCommandManager::MotdCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	if (CallingPlayer == nullptr)
	{
		return GetResponseCmdNullObject(TEXT("CallingPlayer"));
	}

	APawn* Pawn = CallingPlayer->GetPawn();

	if (Pawn == nullptr)
	{
		return GetResponseCmdNullObject(TEXT("Pawn"));
	}

	AIPlayerState* IPlayerState = Cast<AIPlayerState>(Pawn->GetPlayerState());

	if (IPlayerState == nullptr)
	{
		return GetResponseCmdNullObject(TEXT("IPlayerState"));
	}

	AIGameMode* IGameMode = Cast<AIGameMode>(UGameplayStatics::GetGameMode(CallingPlayer->GetWorld()));

	if (IGameMode == nullptr)
	{
		return GetResponseCmdNullObject(TEXT("IGameMode"));
	}

	AIGameSession* IGameSession = Cast<AIGameSession>(IGameMode->GameSession);

	if (IGameSession == nullptr)
	{
		return GetResponseCmdNullObject(TEXT("IGameSession"));
	}

	FString Motd = IGameSession->GetServerMOTD();
	FString DiscordKey = IGameSession->GetServerDiscord();

	if (!FText::FromString(Motd).IsEmptyOrWhitespace())
	{
		CallingPlayer->ClientShowMotd(Motd, DiscordKey);
	}
	else
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdMotdNoMotd"));
	}
	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdMotdDisplayingMotd"));
}

FChatCommandResponse AIChatCommandManager::RulesCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	if (CallingPlayer == nullptr)
	{
		return GetResponseCmdNullObject(TEXT("CallingPlayer"));
	}

	APawn* Pawn = CallingPlayer->GetPawn();

	if (Pawn == nullptr)
	{
		return GetResponseCmdNullObject(TEXT("Pawn"));
	}

	AIPlayerState* IPlayerState = Cast<AIPlayerState>(Pawn->GetPlayerState());

	if (IPlayerState == nullptr)
	{
		return GetResponseCmdNullObject(TEXT("IPlayerState"));
	}

	AIGameMode* IGameMode = Cast<AIGameMode>(UGameplayStatics::GetGameMode(CallingPlayer->GetWorld()));

	if (IGameMode == nullptr)
	{
		return GetResponseCmdNullObject(TEXT("IGameMode"));
	}

	AIGameSession* IGameSession = Cast<AIGameSession>(IGameMode->GameSession);

	if (IGameSession == nullptr)
	{
		return GetResponseCmdNullObject(TEXT("IGameSession"));
	}

	FString Rules = IGameSession->GetServerRules();
	if (!FText::FromString(Rules).IsEmptyOrWhitespace())
	{
		CallingPlayer->ClientShowRules(Rules, false);
		IPlayerState->RulesHash = IGameSession->GetServerRulesCrcHash();
	}
	else
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdRulesNoRules"));
	}
	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdRulesDisplayingRules"));
}

FChatCommandResponse AIChatCommandManager::GodModeCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	if (Params.Num() == 1)
	{
		if (GodMode(CallingPlayer))
		{
			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdGodModeSetOn"));
		}
		else
		{
			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdGodModeSetOff"));
		}
	}
	if (Params.Num() == 2)
	{
		FString Username = Params[1];

		AIPlayerController* TargetPlayer = Cast<AIPlayerController>(PlayerControllerFromUsername(CallingPlayer, Username));
		if (TargetPlayer == nullptr)
		{
			return GetResponseCmdInvalidUsername(Username);
		}

		const FFormatNamedArguments Arguments{
			{ TEXT("Username"), FText::FromString(Username) }
		};

		if (GodMode(TargetPlayer))
		{

			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdGodModePlayerSetOn"), Arguments);
		}
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdGodModePlayerSetOff"), Arguments);
	}
	return FChatCommandResponse();
}

FChatCommandResponse AIChatCommandManager::GodModeRCONCommand(TArray<FString> Params)
{
	if (Params.Num() != 2)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	const FString Username = Params[1];
	AIPlayerController* const TargetPlayer = Cast<AIPlayerController>(PlayerControllerFromUsername(this, Username));

	if (TargetPlayer == nullptr)
	{
		return GetResponseCmdInvalidUsername(Username);
	}

	const FFormatNamedArguments Arguments{
		{ TEXT("Username"), FText::FromString(Username) }
	};

	if (GodMode(TargetPlayer))
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdGodModePlayerSetOn"), Arguments);
	}

	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdGodModePlayerSetOff"), Arguments);
}

FChatCommandResponse AIChatCommandManager::ListRolesCommand(TArray<FString> Params)
{
	FString Response = FString("");
	for (const TPair<FString, FPlayerRole>& pair : PlayerRoles)
	{
		Response.Append(pair.Value.Name);
		Response.Append(TEXT(", "));
	}
	return AIChatCommand::MakePlainResponse(Response);
}

FChatCommandResponse AIChatCommandManager::ClearBodiesCommand(TArray<FString> Params)
{
	if (!GetWorld())
	{
		return GetResponseCmdNullObject(TEXT("GetWorld()"));
	}
	if (Params.Num() != 1)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	FTimerManager& TimerManager = GetWorld()->GetTimerManager();

	int32 ClearedBodies = 0;

	for (TActorIterator<AActor> Actor(GetWorld()); Actor; ++Actor)
	{
		if (AIBaseCharacter* IBaseCharacter = Cast<AIBaseCharacter>(*Actor))
		{
			if (!IsValid(IBaseCharacter))
			{
				continue;
			}
			if (IBaseCharacter->GetIsDying())
			{
				TimerManager.ClearAllTimersForObject(IBaseCharacter);
				IBaseCharacter->DestroyBody(true);
				ClearedBodies++;
			}
		}
		else if (AIMeatChunk* IMeatChunk = Cast<AIMeatChunk>(*Actor))
		{
			if (!ICarryInterface::Execute_IsCarried(IMeatChunk))
			{
				IMeatChunk->DestroyBody();
				ClearedBodies++;
			}
		}
		else if (AICritterPawn* ICritter = Cast<AICritterPawn>(*Actor))
		{
			if (!IsValid(ICritter))
			{
				continue;
			}

			ICritter->Destroy();
		}
	}

	const FFormatNamedArguments Arguments{
		{ TEXT("BodyCount"), ClearedBodies }
	};

	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdClearBodies"), Arguments);
}

FChatCommandResponse AIChatCommandManager::ListPOICommand(TArray<FString> Params)
{
	TArray<AActor*> Pois;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AIPointOfInterest::StaticClass(), Pois);
	TArray<AActor*> Pois2;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AIPOI::StaticClass(), Pois2);

	TSet<FString> SentPOIs;

	FString Response = TEXT("");

	for (int32 i = 0; i < Pois.Num(); i++)
	{
		AIPointOfInterest* IPointOfInterest = Cast<AIPointOfInterest>(Pois[i]);
		if (IPointOfInterest != nullptr)
		{
			FString POIText = IPointOfInterest->GetLocationTag().ToString();
			if (!SentPOIs.Contains(POIText))
			{
				SentPOIs.Add(POIText);
				Response.Append(POIText);
				Response.Append(TEXT(", "));
			}
		}
	}

	for (int32 i = 0; i < Pois2.Num(); i++)
	{
		AIPOI* IPointOfInterest = Cast<AIPOI>(Pois2[i]);
		if (IPointOfInterest != nullptr)
		{
			FString POIText = IPointOfInterest->GetLocationTag().ToString();
			if (!SentPOIs.Contains(POIText))
			{
				SentPOIs.Add(POIText);
				Response.Append(POIText);
				Response.Append(TEXT(", "));
			}
		}
	}
	return AIChatCommand::MakePlainResponse(Response);
}

FChatCommandResponse AIChatCommandManager::SetAttributeAllRCONCommand(TArray<FString> Params)
{
	if (Params.Num() != 3)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	float Value = 0;
	FString AttributeName;

	AttributeName = Params[1];
	if (!FDefaultValueHelper::ParseFloat(Params[2], Value))
	{
		const FFormatNamedArguments Arguments{ 
			{ TEXT("Number"), FText::FromString(Params[2]) }
		};

		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdModifyAttributeParseError"), Arguments);
	}

	AIGameState* IGameState = UIGameplayStatics::GetIGameStateChecked(this);
	if (!IGameState)
	{
		return GetResponseCmdNullObject(TEXT("IGameState"));
	}

	FProperty* Property = FindFProperty<FProperty>(UCoreAttributeSet::StaticClass(), *AttributeName);
	if (!Property)
	{
		return GetResponseCmdModifyAttributeNoProperty(AttributeName);
	}

	FGameplayAttribute AttributeToModify(Property);
	if (!AttributeToModify.IsValid())
	{
		return GetResponseCmdModifyAttributeNoProperty(AttributeName);
	}

	Value = FMath::Clamp(Value, -CLAMP_MINMAX, CLAMP_MINMAX);

	int32 Count = 0;
	for (APlayerState* PlayerState : IGameState->PlayerArray)
	{
		AIPlayerController* IPlayerController = Cast<AIPlayerController>(PlayerState->GetPlayerController());
		if (!IPlayerController)
		{
			continue;
		}

		AIBaseCharacter* TargetBaseCharacter = IPlayerController->GetPawn<AIBaseCharacter>();
		if (!TargetBaseCharacter)
		{
			continue;
		}

		if (AttributeToModify == UCoreAttributeSet::GetGrowthAttribute())
		{
			Value = FMath::Max(Value, MIN_GROWTH);
			if (!UIGameplayStatics::IsGrowthEnabled(this))
			{
				return GetResponseCmdModifyAttributeNoProperty(AttributeName);
			}
		}

		UCoreAttributeSet* const CoreAttrSet = TargetBaseCharacter->AbilitySystem->GetAttributeSet_Mutable<UCoreAttributeSet>();
		if (ensureAlways(CoreAttrSet))
		{
			CoreAttrSet->SetConditionalAttributeReplication(AttributeName, true);
		}

		UPOTAbilitySystemGlobals::ApplyDynamicGameplayEffect(TargetBaseCharacter, AttributeToModify, Value, EGameplayModOp::Override);

		Count++;
	}

	const FFormatNamedArguments Arguments{
		{ TEXT("Attribute"), FText::FromString(AttributeName) },
		{ TEXT("Number"), Value },
		{ TEXT("Count"), Count }
	};

	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdSetAttrAllSuccess"), Arguments);
}

FChatCommandResponse AIChatCommandManager::ModifyAttributeRCONCommand(TArray<FString> Params)
{
	bool bSet = false;
	if (Params[0].ToLower() == TEXT("setattr"))
	{
		bSet = true;
	}
	float Value = 0;
	FString AttributeName;
	AIPlayerController* TargetPlayerController = nullptr;

	if (Params.Num() == 4) // Act on other
	{
		FString Username = Params[1];
		TargetPlayerController = Cast<AIPlayerController>(PlayerControllerFromUsername(this, Username));
		if (TargetPlayerController == nullptr)
		{
			return GetResponseCmdInvalidUsername(Username);
		}

		AttributeName = Params[2].ToLower();
		if (!FDefaultValueHelper::ParseFloat(Params[3], Value))
		{
			const FFormatNamedArguments Arguments{
				{ TEXT("Number"), FText::FromString(Params[3]) } 
			};

			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdModifyAttributeParseError"), Arguments);
		}
	}
	else
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	if (FProperty* Property = FindFProperty<FProperty>(UCoreAttributeSet::StaticClass(), *AttributeName))
	{
		FGameplayAttribute AttributeToModify(Property);
		if (AttributeToModify.IsValid())
		{
			APawn* TargetPawn = TargetPlayerController->GetPawn();
			if (!TargetPawn)
			{
				return GetResponseCmdNullObject(TEXT("TargetPawn"));
			}
			AIBaseCharacter* TargetBaseCharacter = Cast<AIBaseCharacter>(TargetPawn);
			if (!IsValid(TargetBaseCharacter))
			{
				return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdModifyAttributeCantApply"));
			}

			if (AttributeToModify == UCoreAttributeSet::GetGrowthAttribute())
			{
				Value = FMath::Max(Value, MIN_GROWTH);
				if (!UIGameplayStatics::IsGrowthEnabled(this))
				{
					return GetResponseCmdModifyAttributeNoProperty(AttributeName);
				}
			}

			Value = FMath::Clamp(Value, -CLAMP_MINMAX, CLAMP_MINMAX);

			UCoreAttributeSet* const CoreAttrSet = TargetBaseCharacter->AbilitySystem->GetAttributeSet_Mutable<UCoreAttributeSet>();
			if (ensureAlways(CoreAttrSet))
			{
				CoreAttrSet->SetConditionalAttributeReplication(AttributeName, true);
			}

			if (bSet)
			{
				UPOTAbilitySystemGlobals::ApplyDynamicGameplayEffect(TargetBaseCharacter, AttributeToModify, Value, EGameplayModOp::Override);
			}
			else
			{
				UPOTAbilitySystemGlobals::ApplyDynamicGameplayEffect(TargetBaseCharacter, AttributeToModify, Value);
			}
		}
		else
		{
			return GetResponseCmdModifyAttributeNoProperty(AttributeName);
		}
	}
	else
	{
		return GetResponseCmdModifyAttributeNoProperty(AttributeName);
	}
	if (bSet)
	{
		const FFormatNamedArguments Arguments{
			{ TEXT("Attribute"), FText::FromString(AttributeName) },
			{ TEXT("Number"), Value }
		};

		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdModifyAttributeSetSuccess"), Arguments);
	}
	else
	{
		const FFormatNamedArguments Arguments{
			{ TEXT("Attribute"), FText::FromString(AttributeName) },
			{ TEXT("Number"), Value }
		};

		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdModifyAttributeAddSuccess"), Arguments);
	}
}

FChatCommandResponse AIChatCommandManager::ModifyAttributeCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	bool bSet = false;
	if (Params[0].ToLower() == TEXT("setattr"))
	{
		bSet = true;
	}
	float Value = 0;
	FString AttributeName;
	AIPlayerController* TargetPlayerController = nullptr;
	if (Params.Num() == 3) // Act on self
	{
		TargetPlayerController = CallingPlayer;
		AttributeName = Params[1].ToLower();
		if (!FDefaultValueHelper::ParseFloat(Params[2], Value))
		{
			const FFormatNamedArguments Arguments{ 
				{ TEXT("Number"), FText::FromString(Params[2]) }
			};

			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdModifyAttributeParseError"), Arguments);
		}
	}
	else if (Params.Num() == 4) // Act on other
	{
		FString Username = Params[1];
		TargetPlayerController = Cast<AIPlayerController>(PlayerControllerFromUsername(CallingPlayer, Username));
		if (TargetPlayerController == nullptr)
		{
			return GetResponseCmdInvalidUsername(Username);
		}

		AttributeName = Params[2].ToLower();
		if (!FDefaultValueHelper::ParseFloat(Params[3], Value))
		{
			const FFormatNamedArguments Arguments{ 
				{ TEXT("Number"), FText::FromString(Params[3]) }
			};

			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdModifyAttributeParseError"), Arguments);
		}
	}
	else
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	if (FProperty* Property = FindFProperty<FProperty>(UCoreAttributeSet::StaticClass(), *AttributeName))
	{
		FGameplayAttribute AttributeToModify(Property);
		if (AttributeToModify.IsValid())
		{
			APawn* TargetPawn = TargetPlayerController->GetPawn();
			if (!TargetPawn)
			{
				return GetResponseCmdNullObject(TEXT("TargetPawn"));
			}
			AIBaseCharacter* TargetBaseCharacter = Cast<AIBaseCharacter>(TargetPawn);
			if (!IsValid(TargetBaseCharacter) || !IsValid(TargetBaseCharacter->AbilitySystem))
			{
				return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdModifyAttributeCantApply"));
			}

			if (AttributeToModify == UCoreAttributeSet::GetGrowthAttribute())
			{
				Value = FMath::Max(Value, MIN_GROWTH);
				if (!UIGameplayStatics::IsGrowthEnabled(CallingPlayer->GetWorld()))
				{
					return GetResponseCmdModifyAttributeNoProperty(AttributeName);
				}
			}
			else if (AttributeToModify == UCoreAttributeSet::GetWetnessAttribute())
			{
				if (AIDinosaurCharacter* DinoCharacter = Cast<AIDinosaurCharacter>(TargetBaseCharacter))
				{
					if (Value <= 0)
					{
						DinoCharacter->ClearWetness();
					}
					else
					{
						DinoCharacter->ApplyWetness();
					}
				}
			}

			Value = FMath::Clamp(Value, -CLAMP_MINMAX, CLAMP_MINMAX);

			UCoreAttributeSet* const CoreAttrSet = TargetBaseCharacter->AbilitySystem->GetAttributeSet_Mutable<UCoreAttributeSet>();
			if (ensureAlways(CoreAttrSet))
			{
				CoreAttrSet->SetConditionalAttributeReplication(AttributeName, true);
			}

			if (bSet)
			{
				UPOTAbilitySystemGlobals::ApplyDynamicGameplayEffect(TargetBaseCharacter, AttributeToModify, Value, EGameplayModOp::Override);
			}
			else
			{
				UPOTAbilitySystemGlobals::ApplyDynamicGameplayEffect(TargetBaseCharacter, AttributeToModify, Value);
			}
		}
		else
		{
			return GetResponseCmdModifyAttributeNoProperty(AttributeName);
		}
	}
	else
	{
		return GetResponseCmdModifyAttributeNoProperty(AttributeName);
	}
	if (bSet)
	{
		const FFormatNamedArguments Arguments{
			{ TEXT("Attribute"), FText::FromString(AttributeName) },
			{ TEXT("Number"), Value }
		};

		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdModifyAttributeSetSuccess"), Arguments);
	}
	else
	{
		const FFormatNamedArguments Arguments{
			{ TEXT("Attribute"), FText::FromString(AttributeName) },
			{ TEXT("Number"), Value }
		};

		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdModifyAttributeAddSuccess"), Arguments);
	}
}

FChatCommandResponse AIChatCommandManager::AwardGrowthCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	float Value = 0.f;
	AIBaseCharacter* IBaseCharacter = nullptr;
	if (Params.Num() == 2) // Act on self
	{
		APawn* PlayerPawn = CallingPlayer->GetPawn();
		if (PlayerPawn == nullptr)
		{
			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdSetAttribNoPlayerPawn"));
		}
		IBaseCharacter = Cast<AIBaseCharacter>(PlayerPawn);
		if (!FDefaultValueHelper::ParseFloat(Params[1], Value))
		{
			return GetResponseCmdSetAttribCantParseFloat(Params[1]);
		}
	}
	else if (Params.Num() == 3) // Act on target
	{
		FString Username = Params[1];
		AIPlayerController* TargetPlayer = Cast<AIPlayerController>(PlayerControllerFromUsername(CallingPlayer, Username));
		if (TargetPlayer == nullptr)
		{
			return GetResponseCmdInvalidUsername(Username);
		}
		APawn* PlayerPawn = TargetPlayer->GetPawn();
		if (PlayerPawn == nullptr)
		{
			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdSetAttribNoPlayerPawn"));
		}
		IBaseCharacter = Cast<AIBaseCharacter>(PlayerPawn);
		if (!FDefaultValueHelper::ParseFloat(Params[2], Value))
		{
			return GetResponseCmdSetAttribCantParseFloat(Params[2]);
		}
	}
	else
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	if (IBaseCharacter != nullptr)
	{
		float GrowthPerSecond = 0.001f;
		if (AIWorldSettings* WS = AIWorldSettings::GetWorldSettings(IBaseCharacter))
		{
			if (AIQuestManager* QuestMgr = WS->QuestManager)
			{
				GrowthPerSecond = QuestMgr->GrowthRewardRatePerMinute / 60.f;
			}
		}

		Value = FMath::Clamp(Value, -CLAMP_MINMAX, CLAMP_MINMAX);

		UPOTAbilitySystemGlobals::RewardGrowthConstantRate(IBaseCharacter, Value, GrowthPerSecond);
		const FFormatNamedArguments Arguments{
			{ TEXT("Number"), Value },
			{ TEXT("Duration"), Value / GrowthPerSecond }
		};

		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdSetAwardGrowthSuccess"), Arguments);
	}

	return FChatCommandResponse();
}

FChatCommandResponse AIChatCommandManager::AwardWellRestedBuffCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	float Value = 0.f;
	AIBaseCharacter* IBaseCharacter = nullptr;
	if (Params.Num() == 2) // Act on self
	{
		APawn* PlayerPawn = CallingPlayer->GetPawn();
		if (PlayerPawn == nullptr)
		{
			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdSetAttribNoPlayerPawn"));
		}
		IBaseCharacter = Cast<AIBaseCharacter>(PlayerPawn);
		if (!FDefaultValueHelper::ParseFloat(Params[1], Value))
		{
			return GetResponseCmdSetAttribCantParseFloat(Params[1]);
		}
	}
	else if (Params.Num() == 3) // Act on target
	{
		FString Username = Params[1];
		AIPlayerController* TargetPlayer = Cast<AIPlayerController>(PlayerControllerFromUsername(CallingPlayer, Username));
		if (TargetPlayer == nullptr)
		{
			return GetResponseCmdInvalidUsername(Username);
		}
		APawn* PlayerPawn = TargetPlayer->GetPawn();
		if (PlayerPawn == nullptr)
		{
			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdSetAttribNoPlayerPawn"));
		}
		IBaseCharacter = Cast<AIBaseCharacter>(PlayerPawn);
		if (!FDefaultValueHelper::ParseFloat(Params[2], Value))
		{
			return GetResponseCmdSetAttribCantParseFloat(Params[2]);
		}
	}
	else
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	Value = FMath::Clamp(Value, -CLAMP_MINMAX, CLAMP_MINMAX);

	if (IBaseCharacter != nullptr)
	{
		// Reward Well Rested
		AIGameState* IGameState = UIGameplayStatics::GetIGameStateChecked(this);
		check(IGameState);

		if (UIGameplayStatics::IsGrowthEnabled(this) && IGameState->GetGameStateFlags().bGrowthWellRestedBuff)
		{
			if (AIDinosaurCharacter* DinoCharacter = Cast<AIDinosaurCharacter>(IBaseCharacter))
			{
				AIPlayerState* IPlayerState = IBaseCharacter->GetPlayerState<AIPlayerState>();
				FCharacterData& CharacterData = IPlayerState->CharactersData[IBaseCharacter->GetCharacterID()];

				if ((CharacterData.bHasFinishedHatchlingTutorial || !UIGameplayStatics::AreHatchlingCavesEnabled(this)))
				{
					if (FMath::Clamp(Value, 0.0f, 1.0f) > DinoCharacter->GetGrowthPercent())
					{
						UPOTAbilitySystemGlobals::ApplyDynamicGameplayEffect(DinoCharacter, UCoreAttributeSet::GetWellRestedBonusStartedGrowthAttribute(), DinoCharacter->GetGrowthPercent(), EGameplayModOp::Override);
						UPOTAbilitySystemGlobals::ApplyDynamicGameplayEffect(DinoCharacter, UCoreAttributeSet::GetWellRestedBonusEndGrowthAttribute(), FMath::Clamp(Value, 0.0f, 1.0f), EGameplayModOp::Override);

						DinoCharacter->ApplyWellRestedBonusMultiplier();
					}
					else
					{
						if (DinoCharacter->GetGE_WellRestedBonus().IsValid())
						{
							UPOTAbilitySystemGlobals::ApplyDynamicGameplayEffect(DinoCharacter, UCoreAttributeSet::GetWellRestedBonusStartedGrowthAttribute(), 0.0f, EGameplayModOp::Override);
							UPOTAbilitySystemGlobals::ApplyDynamicGameplayEffect(DinoCharacter, UCoreAttributeSet::GetWellRestedBonusEndGrowthAttribute(), 0.0f, EGameplayModOp::Override);
							DinoCharacter->AbilitySystem->RemoveActiveGameplayEffect(DinoCharacter->GetGE_WellRestedBonus(), -1);
							DinoCharacter->GetGE_WellRestedBonus_Mutable().Invalidate();
						}
					}
				}
				else
				{
					return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdWellRestedNotAllowed"));
				}
			}
			else
			{
				return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdWellRestedNotAllowed"));
			}
		}
		else
		{
			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdWellRestedNotAllowed"));
		}

		const FFormatNamedArguments Arguments{
			{ TEXT("Number"), Value }
		};

		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdSetAwardWellRestedSuccess"), Arguments);
	}

	return FChatCommandResponse();
}

FChatCommandResponse AIChatCommandManager::GetAttributeRCONCommand(TArray<FString> Params)
{
	FString AttributeName;
	AIBaseCharacter* IBaseCharacter = nullptr;

	if (Params.Num() == 3) // Act on target
	{
		FString Username = Params[1];
		AIPlayerController* TargetPlayer = Cast<AIPlayerController>(PlayerControllerFromUsername(this, Username));
		if (TargetPlayer == nullptr)
		{
			return GetResponseCmdInvalidUsername(Username);
		}
		APawn* PlayerPawn = TargetPlayer->GetPawn();
		if (PlayerPawn == nullptr)
		{
			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdSetAttribNoPlayerPawn"));
		}
		IBaseCharacter = Cast<AIBaseCharacter>(PlayerPawn);
		AttributeName = Params[2].ToLower();
	}
	else
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	if (FProperty* Property = FindFProperty<FProperty>(UCoreAttributeSet::StaticClass(), *AttributeName))
	{
		FGameplayAttribute Attribute(Property);
		if (Attribute.IsValid())
		{
			if (IBaseCharacter && IBaseCharacter->AbilitySystem)
			{
				float CurrentValue = IBaseCharacter->AbilitySystem->GetNumericAttribute(Attribute);

				const FFormatNamedArguments Arguments{
					{ TEXT("Attribute"), FText::FromString(AttributeName) },
					{ TEXT("Number"), CurrentValue }
				};

				return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdGetAttribSuccess"), Arguments);
			}
		}
		else
		{
			return GetResponseCmdModifyAttributeNoProperty(AttributeName);
		}
	}
	else
	{
		return GetResponseCmdModifyAttributeNoProperty(AttributeName);
	}

	return FChatCommandResponse();
}

FChatCommandResponse AIChatCommandManager::GetAttributeCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	FString AttributeName;
	AIBaseCharacter* IBaseCharacter = nullptr;
	if (Params.Num() == 2) // Act on self
	{
		APawn* PlayerPawn = CallingPlayer->GetPawn();
		if (PlayerPawn == nullptr)
		{
			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdSetAttribNoPlayerPawn"));
		}
		IBaseCharacter = Cast<AIBaseCharacter>(PlayerPawn);
		AttributeName = Params[1].ToLower();
	}
	else if (Params.Num() == 3) // Act on target
	{
		FString Username = Params[1];
		AIPlayerController* TargetPlayer = Cast<AIPlayerController>(PlayerControllerFromUsername(CallingPlayer, Username));
		if (TargetPlayer == nullptr)
		{
			return GetResponseCmdInvalidUsername(Username);
		}
		APawn* PlayerPawn = TargetPlayer->GetPawn();
		if (PlayerPawn == nullptr)
		{
			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdSetAttribNoPlayerPawn"));
		}
		IBaseCharacter = Cast<AIBaseCharacter>(PlayerPawn);
		AttributeName = Params[2].ToLower();
	}
	else
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	if (FProperty* Property = FindFProperty<FProperty>(UCoreAttributeSet::StaticClass(), *AttributeName))
	{
		FGameplayAttribute Attribute(Property);
		if (Attribute.IsValid())
		{
			if (IBaseCharacter && IBaseCharacter->AbilitySystem)
			{
				float CurrentValue = IBaseCharacter->AbilitySystem->GetNumericAttribute(Attribute);

				const FFormatNamedArguments Arguments{
					{ TEXT("Attribute"), FText::FromString(AttributeName) },
					{ TEXT("Number"), CurrentValue }
				};

				return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdGetAttribSuccess"), Arguments);
			}
		}
		else
		{
			return GetResponseCmdModifyAttributeNoProperty(AttributeName);
		}
	}
	else
	{
		return GetResponseCmdModifyAttributeNoProperty(AttributeName);
	}

	return FChatCommandResponse();
}

FString AIChatCommandManager::GetReflectedPropertyValueString(const UObject* const TargetObject, const FString& PropertyPath)
{
	if (!TargetObject || PropertyPath.IsEmpty())
	{
		return TEXT("");
	}

	TArray<FString> ReflectionPath{};
	ReflectionPath.Reserve(4);
	if (!PropertyPath.ParseIntoArray(ReflectionPath, TEXT(".")))
	{
		ReflectionPath.Add(PropertyPath);
	}

	const UObject* CurrentObject = TargetObject;
	while (!ReflectionPath.IsEmpty() && CurrentObject != nullptr)
	{
		const FString CurrentProperty = ReflectionPath[0];
		ReflectionPath.RemoveAt(0);

		const FProperty* const Property = FindFProperty<FProperty>(CurrentObject->GetClass(), *CurrentProperty);
		if (!Property)
		{
			return FString::Printf(TEXT("Could not find property (%s)"), *CurrentProperty);
		}

		const void* Value = Property->ContainerPtrToValuePtr<uint8>(CurrentObject);

		if (!ReflectionPath.IsEmpty())
		{
			const FObjectPropertyBase* const ObjectProperty = CastField<FObjectPropertyBase>(Property);
			if (!ObjectProperty)
			{
				return FString::Printf(TEXT("Not at end of reflection path but property (%s) is not a UObject"), *CurrentProperty);
			}
			const UObject* const NewUObject = ObjectProperty->GetObjectPropertyValue(Value);
			if (!NewUObject)
			{
				return FString::Printf(TEXT("Failed to get UObject property (%s)"), *CurrentProperty);
			}
			CurrentObject = NewUObject;
			continue;
		}

		// we are at the final reflected property. display it.
		if (const FNumericProperty* const NumberProperty = CastField<FNumericProperty>(Property))
		{
			const FString ValueString = NumberProperty->GetNumericPropertyValueToString(Value);
			return FString::Printf(TEXT("%s = %s"), *PropertyPath, *ValueString);
		}
		else
		{
			return FString::Printf(TEXT("Unsupported property type (%s)"), *CurrentProperty);
		}
	}

	return TEXT("");
}

FChatCommandResponse AIChatCommandManager::GetPropertyRCONCommand(TArray<FString> Params)
{
	const AIBaseCharacter* IBaseCharacter = nullptr;

	if (Params.IsValidIndex(1))
	{
		const FString Username = Params[1];
		const AIPlayerController* const TargetPlayer = Cast<AIPlayerController>(PlayerControllerFromUsername(this, Username));
		if (!TargetPlayer)
		{
			return GetResponseCmdInvalidUsername(Username);
		}
		IBaseCharacter = Cast<AIBaseCharacter>(TargetPlayer->GetPawn());
		if (!IBaseCharacter)
		{
			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdSetAttribNoPlayerPawn"));
		}
	}

	if (!Params.IsValidIndex(2))
	{
		return AIChatCommand::MakePlainResponse(TEXT("Empty Property name"));
	}

	FString Result = TEXT("");
	Result.Reserve(1000);
	for (int32 Index = 2; Index < Params.Num(); Index++)
	{
		const FString AttributeName = Params[Index];
		const FString AttribResult = GetReflectedPropertyValueString(IBaseCharacter, AttributeName);
		if (AttribResult.IsEmpty())
		{
			continue;
		}
		if (Result.IsEmpty())
		{
			Result = AttribResult;
		}
		else
		{
			Result.Append(FString::Printf(TEXT(", %s"), *AttribResult));
		}
	}

	return AIChatCommand::MakePlainResponse(Result);
}

FChatCommandResponse AIChatCommandManager::GetPropertyCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	if (!Params.IsValidIndex(1))
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	TArray<FString> RconParams{};
	RconParams.Reserve(Params.Num() + 1);

	if (PlayerControllerFromUsername(CallingPlayer, Params[1]))
	{
		RconParams.Append(Params);
	}
	else
	{
		if (!CallingPlayer)
		{
			return FChatCommandResponse();
		}

		const AIPlayerState* const IPlayerState = CallingPlayer->GetPlayerState<AIPlayerState>();
		if (!IPlayerState)
		{
			return FChatCommandResponse();
		}

		RconParams.Add(Params[0]);
		RconParams.Add(IPlayerState->GetAlderonID().ToDisplayString());
		Params.RemoveAt(0);
		RconParams.Append(Params);
	}

	return GetPropertyRCONCommand(RconParams);
}

FChatCommandResponse AIChatCommandManager::ListPropertiesRCONCommand(TArray<FString> Params)
{
	const AIBaseCharacter* IBaseCharacter = nullptr;
	FString AttributeName = TEXT("");

	if (Params.IsValidIndex(1))
	{
		const FString Username = Params[1];
		const AIPlayerController* const TargetPlayer = Cast<AIPlayerController>(PlayerControllerFromUsername(this, Username));
		if (!TargetPlayer)
		{
			return GetResponseCmdInvalidUsername(Username);
		}
		IBaseCharacter = Cast<AIBaseCharacter>(TargetPlayer->GetPawn());
		if (!IBaseCharacter)
		{
			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdSetAttribNoPlayerPawn"));
		}
	}

	if (Params.IsValidIndex(2))
	{
		AttributeName = Params[2];
	}

	const UObject* TargetObject = IBaseCharacter;
	if (!AttributeName.IsEmpty())
	{
		TArray<FString> ReflectionPath{};
		ReflectionPath.Reserve(4);
		if (!AttributeName.ParseIntoArray(ReflectionPath, TEXT(".")))
		{
			ReflectionPath.Add(AttributeName);
		}

		while (!ReflectionPath.IsEmpty() && TargetObject != nullptr)
		{
			const FString CurrentProperty = ReflectionPath[0];
			ReflectionPath.RemoveAt(0);

			const FProperty* const Property = FindFProperty<FProperty>(TargetObject->GetClass(), *CurrentProperty);
			if (!Property)
			{
				return AIChatCommand::MakePlainResponse(FString::Printf(TEXT("Could not find property (%s)"), *CurrentProperty));
			}

			const FObjectPropertyBase* const ObjectProperty = CastField<FObjectPropertyBase>(Property);

			if (!ObjectProperty)
			{
				return AIChatCommand::MakePlainResponse(FString::Printf(TEXT("(%s) is not a UObject"), *CurrentProperty));
			}

			const void* const Value = Property->ContainerPtrToValuePtr<uint8>(TargetObject);

			const UObject* const NewUObject = ObjectProperty->GetObjectPropertyValue(Value);
			if (!NewUObject)
			{
				return AIChatCommand::MakePlainResponse(FString::Printf(TEXT("Failed to get UObject property (%s)"), *CurrentProperty));
			}

			TargetObject = NewUObject;
		}
	}

	FString Result = TEXT("");
	Result.Reserve(2000);

	for (TFieldIterator<FProperty> It(TargetObject->GetClass(), EFieldIterationFlags::IncludeSuper); It; ++It)
	{
		const FString PropertyString = It->GetName();
		if (Result.IsEmpty())
		{
			Result = PropertyString;
		}
		else
		{
			Result.Append(FString::Printf(TEXT(", %s"), *PropertyString));
		}
	}

	return AIChatCommand::MakePlainResponse(Result);
}

FChatCommandResponse AIChatCommandManager::ListPropertiesCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	TArray<FString> RconParams{};
	RconParams.Reserve(Params.Num() + 1);

	if (Params.IsValidIndex(1) && PlayerControllerFromUsername(CallingPlayer, Params[1]))
	{
		RconParams.Append(Params);
	}
	else
	{
		if (!CallingPlayer)
		{
			return FChatCommandResponse();
		}

		const AIPlayerState* const IPlayerState = CallingPlayer->GetPlayerState<AIPlayerState>();
		if (!IPlayerState)
		{
			return FChatCommandResponse();
		}

		RconParams.Add(Params[0]);
		RconParams.Add(IPlayerState->GetAlderonID().ToDisplayString());
		if (Params.IsValidIndex(1))
		{
			RconParams.Add(Params[1]);
		}
	}

	return ListPropertiesRCONCommand(RconParams);
}

FChatCommandResponse AIChatCommandManager::ListGameplayAbilitiesRCONCommand(TArray<FString> Params)
{
	if (!Params.IsValidIndex(1))
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	const FString Username = Params[1];
	const AIPlayerController* const TargetPlayer = Cast<AIPlayerController>(PlayerControllerFromUsername(this, Username));
	if (!TargetPlayer)
	{
		return GetResponseCmdInvalidUsername(Username);
	}

	const AIBaseCharacter* const TargetBaseCharacter = Cast<AIBaseCharacter>(TargetPlayer->GetPawn());
	if (!TargetBaseCharacter || !TargetBaseCharacter->AbilitySystem)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdSetAttribNoPlayerPawn"));
	}

	UTitanAssetManager& AssetManager = static_cast<UTitanAssetManager&>(UAssetManager::Get());

	FString Result = TEXT("");
	Result.Reserve(2000);

	for (const FSlottedAbilities& SlottedAbilities : TargetBaseCharacter->GetSlottedAbilityAssetsArray())
	{
		for (const FPrimaryAssetId& AbilityId : SlottedAbilities.SlottedAbilities)
		{
			const UPOTAbilityAsset* const LoadedAbility = AssetManager.ForceLoadAbility(AbilityId);

			if (!LoadedAbility)
			{
				continue;
			}

			const FString AbilityNameString = LoadedAbility->Name.ToString();
			const FString AbilityCategoryString = UEnum::GetValueAsString(LoadedAbility->AbilityCategory);
			Result.Append(FString::Printf(TEXT("\n%s (%s)"), *AbilityNameString, *AbilityCategoryString));
		}
	}

	if (Result.IsEmpty())
	{
		Result.Append(TEXT("None"));
	}

	return AIChatCommand::MakePlainResponse(Result);
}

FChatCommandResponse AIChatCommandManager::ListGameplayAbilitiesCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	TArray<FString> RconParams{};
	RconParams.Reserve(Params.Num() + 1);

	if (Params.IsValidIndex(1) && PlayerControllerFromUsername(CallingPlayer, Params[1]))
	{
		RconParams.Append(Params);
	}
	else
	{
		if (!CallingPlayer)
		{
			return FChatCommandResponse();
		}

		const AIPlayerState* const IPlayerState = CallingPlayer->GetPlayerState<AIPlayerState>();
		if (!IPlayerState)
		{
			return FChatCommandResponse();
		}

		RconParams.Add(Params[0]);
		RconParams.Add(IPlayerState->GetAlderonID().ToDisplayString());
	}

	return ListGameplayAbilitiesRCONCommand(RconParams);
}

FChatCommandResponse AIChatCommandManager::InspectGameplayAbilityCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	TArray<FString> RconParams{};
	RconParams.Reserve(Params.Num() + 1);

	if (Params.IsValidIndex(1) && PlayerControllerFromUsername(CallingPlayer, Params[1]))
	{
		RconParams.Append(Params);
	}
	else
	{
		if (!CallingPlayer)
		{
			return FChatCommandResponse();
		}

		const AIPlayerState* const IPlayerState = CallingPlayer->GetPlayerState<AIPlayerState>();
		if (!IPlayerState)
		{
			return FChatCommandResponse();
		}

		RconParams.Add(Params[0]);
		RconParams.Add(IPlayerState->GetAlderonID().ToDisplayString());
		if (Params.IsValidIndex(1))
		{
			RconParams.Add(Params[1]);
		}
	}

	return InspectGameplayAbilityRCONCommand(RconParams);
}

FChatCommandResponse AIChatCommandManager::InspectGameplayAbilityRCONCommand(TArray<FString> Params)
{
	if (!Params.IsValidIndex(2))
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	const FString Username = Params[1];
	const AIPlayerController* const TargetPlayer = Cast<AIPlayerController>(PlayerControllerFromUsername(this, Username));
	if (!TargetPlayer)
	{
		return GetResponseCmdInvalidUsername(Username);
	}

	const AIBaseCharacter* const TargetBaseCharacter = Cast<AIBaseCharacter>(TargetPlayer->GetPawn());
	if (!TargetBaseCharacter || !TargetBaseCharacter->AbilitySystem)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdSetAttribNoPlayerPawn"));
	}

	const FString AbilityName = Params[2];
	UTitanAssetManager& AssetManager = static_cast<UTitanAssetManager&>(UAssetManager::Get());

	for (const FSlottedAbilities& SlottedAbilities : TargetBaseCharacter->GetSlottedAbilityAssetsArray())
	{
		for (int32 i = 0; i < SlottedAbilities.SlottedAbilities.Num(); i++)
		{
			const FPrimaryAssetId& AbilityId = SlottedAbilities.SlottedAbilities[i];
			const UPOTAbilityAsset* const AbilityAsset = AssetManager.ForceLoadAbility(AbilityId);

			if (!AbilityAsset)
			{
				continue;
			}

			const FString AbilityNameString = AbilityAsset->Name.ToString();

			if (AbilityNameString != AbilityName)
			{
				continue;
			}

			FString Result = TEXT("");
			Result.Reserve(2000);

			Result.Append(FString::Printf(TEXT("Name: %s\n"), *AbilityNameString));

			const FString AbilityCategoryString = UEnum::GetValueAsString(AbilityAsset->AbilityCategory);
			Result.Append(FString::Printf(TEXT("Category: %s\n"), *AbilityCategoryString));

			const FString DefaultPrefix = TEXT("Default__");
			const FString CSuffix = TEXT("_C");

			if (UPOTGameplayAbility* GameplayAbility = AbilityAsset->GrantedAbility.GetDefaultObject())
			{
				{
					FString NameString = GameplayAbility->GetName();
					NameString.RemoveFromStart(DefaultPrefix);
					NameString.RemoveFromEnd(CSuffix);
					Result.Append(FString::Printf(TEXT("GameplayAbility: %s\n"), *NameString));
				}
				for (const TPair<FGameplayTag, FPOTGameplayEffectContainer>& Pair : GameplayAbility->EffectContainerMap)
				{
					const FPOTGameplayEffectContainer& EffectContainer = Pair.Value;
					for (const FPOTGameplayEffectContainerEntry& EffectContainerEntry : EffectContainer.ContainerEntries)
					{
						for (const FPOTGameplayEffectEntry& EffectEntry : EffectContainerEntry.TargetGameplayEffects)
						{
							for (const TSubclassOf<UGameplayEffect>& EffectClass : EffectEntry.TargetGameplayEffectClasses)
							{
								const UGameplayEffect* const DefaultEffect = EffectClass.GetDefaultObject();
								if (!EffectClass.Get())
								{
									continue;
								}
								FString NameString = DefaultEffect->GetName();
								NameString.RemoveFromStart(DefaultPrefix);
								NameString.RemoveFromEnd(CSuffix);
								Result.Append(FString::Printf(TEXT("--- Effect: %s\n"), *NameString));

								Result.Append(FString::Printf(TEXT("%s\n"), *GetGameplayEffectExecutionInfo(DefaultEffect, TEXT("--- --- "))));
							}
						}
					}
				}

				if (const UGameplayEffect* const DefaultCostEffect = GameplayAbility->GetCostGameplayEffect())
				{
					FString NameString = DefaultCostEffect->GetName();
					NameString.RemoveFromStart(DefaultPrefix);
					NameString.RemoveFromEnd(CSuffix);
					Result.Append(FString::Printf(TEXT("--- Cost: %s\n"), *NameString));

					Result.Append(FString::Printf(TEXT("%s\n"), *GetGameplayEffectExecutionInfo(DefaultCostEffect, TEXT("--- --- "))));
				}

				if (const UGameplayEffect* const DefaultCooldownEffect = GameplayAbility->GetCooldownGameplayEffect())
				{
					FString NameString = DefaultCooldownEffect->GetName();
					NameString.RemoveFromStart(DefaultPrefix);
					NameString.RemoveFromEnd(CSuffix);
					Result.Append(FString::Printf(TEXT("--- Cooldown: %s\n"), *NameString));

					Result.Append(FString::Printf(TEXT("%s\n"), *GetGameplayEffectExecutionInfo(DefaultCooldownEffect, TEXT("--- --- "))));
				}
			}

			for (const TSubclassOf<UGameplayEffect>& PassiveEffectClass : AbilityAsset->GrantedPassiveEffects)
			{
				const UGameplayEffect* const DefaultEffect = PassiveEffectClass.GetDefaultObject();
				if (!DefaultEffect)
				{
					continue;
				}

				FString NameString = DefaultEffect->GetName();
				NameString.RemoveFromStart(DefaultPrefix);
				NameString.RemoveFromEnd(CSuffix);
				Result.Append(FString::Printf(TEXT("Passive Effect: (%s)\n"), *NameString));

				Result.Append(FString::Printf(TEXT("%s\n"), *GetGameplayEffectExecutionInfo(DefaultEffect, TEXT("--- --- "))));
			}

			return AIChatCommand::MakePlainResponse(Result);
		}
	}

	const FFormatNamedArguments Args{
		{ TEXT("AbilityName"), FText::FromString(AbilityName) }
	};

	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdInspectAbilityNoneFound"), Args);
}

FString AIChatCommandManager::GetGameplayEffectExecutionInfo(const UGameplayEffect* Effect, const FString& LinePrefix)
{
	if (!Effect)
	{
		return TEXT("Invalid");
	}

	const FString ScalableFloatMagnitudePropName = TEXT("ScalableFloatMagnitude");

	FString Result = TEXT("");
	Result.Reserve(2000);

	const FString DurationPolicyString = UEnum::GetValueAsString(Effect->DurationPolicy);
	Result.Append(FString::Printf(TEXT("%sDuration Policy (%s)\n"), *LinePrefix, *DurationPolicyString));

	if (Effect->DurationPolicy == EGameplayEffectDurationType::HasDuration)
	{
		FString DurationString = TEXT("");
		FString DurationMultiplierString = TEXT("");
		if (Effect->DurationMagnitude.GetMagnitudeCalculationType() == EGameplayEffectMagnitudeCalculation::ScalableFloat)
		{
			// Need to get the value via reflection because it's protected and has no getter.
			const FScalableFloat* const ScalableFloat = UIGameplayStatics::GetPtrToReflectedProperty<FScalableFloat>(&Effect->DurationMagnitude, ScalableFloatMagnitudePropName);
			if (ensureAlways(ScalableFloat))
			{
				FString CurveFloatString = TEXT("");

				bool bFirst = true;
				const FRealCurve* const Curve = ScalableFloat->Curve.GetCurve(TEXT("ChatCommand"));
				for (auto It = Curve->GetKeyHandleIterator(); It; ++It)
				{
					const FKeyHandle& Handle = *It;
					const TPair<float, float> TimeValue = Curve->GetKeyTimeValuePair(Handle);

					if (bFirst)
					{
						CurveFloatString.Append(FString::Printf(TEXT("%s"), *FString::SanitizeFloat(TimeValue.Value)));
					}
					else
					{
						CurveFloatString.Append(FString::Printf(TEXT(",%s"), *FString::SanitizeFloat(TimeValue.Value)));
					}
					bFirst = false;
				}

				DurationString = FString::Printf(TEXT("%s (%s)"), *ScalableFloat->Curve.RowName.ToString(), *CurveFloatString);

				DurationMultiplierString = FString::SanitizeFloat(ScalableFloat->Value);
			}
		}
		else
		{
			DurationString = UEnum::GetValueAsString(Effect->DurationMagnitude.GetMagnitudeCalculationType());
		}

		Result.Append(FString::Printf(TEXT("%s--- Duration %s\n"), *LinePrefix, *DurationString));

		if (!DurationMultiplierString.IsEmpty())
		{
			Result.Append(FString::Printf(TEXT("%s--- Multiplier %s\n"), *LinePrefix, *DurationMultiplierString));
		}
	}

	for (const FGameplayEffectExecutionDefinition& ExecutionDefinition : Effect->Executions)
	{
		Result.Append(FString::Printf(TEXT("%sExecutions:\n"), *LinePrefix));
		for (const FGameplayEffectExecutionScopedModifierInfo& ExecutionModInfo : ExecutionDefinition.CalculationModifiers)
		{
			const FString CapturedAttributeString = ExecutionModInfo.CapturedAttribute.AttributeToCapture.GetName();
			Result.Append(FString::Printf(TEXT("%s--- Name %s\n"), *LinePrefix, *CapturedAttributeString));

			const FString ModifierTypeString = UEnum::GetValueAsString<EGameplayModOp::Type>(ExecutionModInfo.ModifierOp);
			Result.Append(FString::Printf(TEXT("%s--- --- Type %s\n"), *LinePrefix, *ModifierTypeString));

			FString MagnitudeString = TEXT("");
			FString MagnitudeMultiplierString = TEXT("");
			if (ExecutionModInfo.ModifierMagnitude.GetMagnitudeCalculationType() == EGameplayEffectMagnitudeCalculation::ScalableFloat)
			{
				// Need to get the value via reflection because it's protected and has no getter.
				const FScalableFloat* const ScalableFloat = UIGameplayStatics::GetPtrToReflectedProperty<FScalableFloat>(&ExecutionModInfo.ModifierMagnitude, ScalableFloatMagnitudePropName);
				if (ensureAlways(ScalableFloat))
				{
					FString CurveFloatString = TEXT("");

					bool bFirst = true;
					const FRealCurve* const Curve = ScalableFloat->Curve.GetCurve(TEXT("ChatCommand"));
					for (auto It = Curve->GetKeyHandleIterator(); It; ++It)
					{
						const FKeyHandle& Handle = *It;
						const TPair<float, float> TimeValue = Curve->GetKeyTimeValuePair(Handle);

						if (bFirst)
						{
							CurveFloatString.Append(FString::Printf(TEXT("%s"), *FString::SanitizeFloat(TimeValue.Value)));
						}
						else
						{
							CurveFloatString.Append(FString::Printf(TEXT(",%s"), *FString::SanitizeFloat(TimeValue.Value)));
						}
						bFirst = false;
					}

					MagnitudeString = FString::Printf(TEXT("%s (%s)"), *ScalableFloat->Curve.RowName.ToString(), *CurveFloatString);

					MagnitudeMultiplierString = FString::SanitizeFloat(ScalableFloat->Value);
				}
			}
			else
			{
				MagnitudeString = UEnum::GetValueAsString(ExecutionModInfo.ModifierMagnitude.GetMagnitudeCalculationType());
			}

			Result.Append(FString::Printf(TEXT("%s--- --- Magnitude %s\n"), *LinePrefix, *MagnitudeString));

			if (!MagnitudeMultiplierString.IsEmpty())
			{
				Result.Append(FString::Printf(TEXT("%s--- --- Multiplier %s\n"), *LinePrefix, *MagnitudeMultiplierString));
			}
		}
	}

	Result.Append(FString::Printf(TEXT("%sModifiers:\n"), *LinePrefix));

	for (const FGameplayModifierInfo& Modifier : Effect->Modifiers)
	{
		const FString CapturedAttributeString = Modifier.Attribute.GetName();
		Result.Append(FString::Printf(TEXT("%s--- Name %s\n"), *LinePrefix, *CapturedAttributeString));

		const FString ModifierTypeString = UEnum::GetValueAsString<EGameplayModOp::Type>(Modifier.ModifierOp);
		Result.Append(FString::Printf(TEXT("%s--- --- Type %s\n"), *LinePrefix, *ModifierTypeString));

		FString MagnitudeString = TEXT("");
		FString MagnitudeMultiplierString = TEXT("");
		if (Modifier.ModifierMagnitude.GetMagnitudeCalculationType() == EGameplayEffectMagnitudeCalculation::ScalableFloat)
		{
			// Need to get the value via reflection because it's protected and has no getter.
			const FScalableFloat* const ScalableFloat = UIGameplayStatics::GetPtrToReflectedProperty<FScalableFloat>(&Modifier.ModifierMagnitude, ScalableFloatMagnitudePropName);
			if (ensureAlways(ScalableFloat))
			{
				FString CurveFloatString = TEXT("");

				bool bFirst = true;
				const FRealCurve* const Curve = ScalableFloat->Curve.GetCurve(TEXT("ChatCommand"));
				if (Curve)
				{
					for (auto It = Curve->GetKeyHandleIterator(); It; ++It)
					{
						const FKeyHandle& Handle = *It;
						const TPair<float, float> TimeValue = Curve->GetKeyTimeValuePair(Handle);

						if (bFirst)
						{
							CurveFloatString.Append(FString::Printf(TEXT("%s"), *FString::SanitizeFloat(TimeValue.Value)));
						}
						else
						{
							CurveFloatString.Append(FString::Printf(TEXT(",%s"), *FString::SanitizeFloat(TimeValue.Value)));
						}
						bFirst = false;
					}
				}
				MagnitudeString = FString::Printf(TEXT("%s (%s)"), *ScalableFloat->Curve.RowName.ToString(), *CurveFloatString);

				MagnitudeMultiplierString = FString::SanitizeFloat(ScalableFloat->Value);
			}
		}
		else
		{
			MagnitudeString = UEnum::GetValueAsString(Modifier.ModifierMagnitude.GetMagnitudeCalculationType());
		}

		Result.Append(FString::Printf(TEXT("%s--- --- Magnitude %s\n"), *LinePrefix, *MagnitudeString));

		if (!MagnitudeMultiplierString.IsEmpty())
		{
			Result.Append(FString::Printf(TEXT("%s--- --- Multiplier %s\n"), *LinePrefix, *MagnitudeMultiplierString));
		}
	}

	return Result;
}

FChatCommandResponse AIChatCommandManager::GetAllAttributesRCONCommand(TArray<FString> Params)
{
	const AIBaseCharacter* IBaseCharacter = nullptr;

	if (Params.IsValidIndex(1))
	{
		const FString Username = Params[1];
		const AIPlayerController* const TargetPlayer = Cast<AIPlayerController>(PlayerControllerFromUsername(this, Username));
		if (!TargetPlayer)
		{
			return GetResponseCmdInvalidUsername(Username);
		}
		IBaseCharacter = Cast<AIBaseCharacter>(TargetPlayer->GetPawn());
		if (!IBaseCharacter || !IBaseCharacter->AbilitySystem)
		{
			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdSetAttribNoPlayerPawn"));
		}
	}

	FString Result = TEXT("");
	Result.Reserve(2000);

	for (TFieldIterator<FProperty> It(UCoreAttributeSet::StaticClass(), EFieldIterationFlags::IncludeSuper); It; ++It)
	{
		FProperty* const Property = *It;
		const FGameplayAttribute Attribute(Property);
		if (!Attribute.IsValid())
		{
			continue;
		}

		const float CurrentValue = IBaseCharacter->AbilitySystem->GetNumericAttribute(Attribute);
		const FString PropertyString = Property->GetName();

		if (Result.IsEmpty())
		{
			Result = FString::Printf(TEXT("%s=%f"), *PropertyString, CurrentValue);
		}
		else
		{
			Result.Append(FString::Printf(TEXT(", %s=%f"), *PropertyString, CurrentValue));
		}
	}

	return AIChatCommand::MakePlainResponse(Result);
}

FChatCommandResponse AIChatCommandManager::GetAllAttributesCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	TArray<FString> RconParams{};
	RconParams.Reserve(Params.Num() + 1);

	if (Params.IsValidIndex(1) && PlayerControllerFromUsername(CallingPlayer, Params[1]))
	{
		RconParams.Append(Params);
	}
	else
	{
		if (!CallingPlayer)
		{
			return FChatCommandResponse();
		}

		const AIPlayerState* const IPlayerState = CallingPlayer->GetPlayerState<AIPlayerState>();
		if (!IPlayerState)
		{
			return FChatCommandResponse();
		}

		RconParams.Add(Params[0]);
		RconParams.Add(IPlayerState->GetAlderonID().ToDisplayString());
		if (Params.IsValidIndex(1))
		{
			RconParams.Add(Params[1]);
		}
	}

	return GetAllAttributesRCONCommand(RconParams);
}

FChatCommandResponse AIChatCommandManager::ListCurveValuesRCONCommand(TArray<FString> Params)
{
	if (!Params.IsValidIndex(1))
	{
		return AIChatCommand::MakePlainResponse(TEXT("no CurveTableFilter"));
	}

	const FString CurveTableFilter = Params[1];
	FString CurveValueFilter = TEXT("");

	if (Params.IsValidIndex(2))
	{
		CurveValueFilter = Params[2];
	}

	FString Result = TEXT("");
	Result.Reserve(2000);

	for (TObjectIterator<UCurveTable> TableIt; TableIt; ++TableIt)
	{
		const UCurveTable* const CurveTable = *TableIt;
		if (!CurveTable)
		{
			continue;
		}
		if (!CurveTable->GetName().Contains(CurveTableFilter))
		{
			continue;
		}

		const TMap<FName, FRealCurve*>& RowMap = CurveTable->GetRowMap();
		for (const TPair<FName, FRealCurve*>& CurvePair : RowMap)
		{
			const FString CurveNameString = CurvePair.Key.ToString();

			FString CurveNameLast = TEXT("");
			if (!CurveNameString.Split(TEXT("."), nullptr, &CurveNameLast, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
			{
				CurveNameLast = CurveNameString;
			}

			if (CurveValueFilter.IsEmpty() || CurveNameLast == CurveValueFilter)
			{
				FString ShortenedCurveNameString = TEXT("");
				if (!CurveNameString.Split(TEXT("."), nullptr, &ShortenedCurveNameString))
				{
					ShortenedCurveNameString = CurveNameString;
				}
				if (Result.IsEmpty())
				{
					Result = FString::Printf(TEXT("%s: ("), *ShortenedCurveNameString);
				}
				else
				{
					Result.Append(FString::Printf(TEXT("), %s: ("), *ShortenedCurveNameString));
				}

				bool bFirst = true;
				const FRealCurve* const Curve = CurvePair.Value;
				for (auto It = Curve->GetKeyHandleIterator(); It; ++It)
				{
					const FKeyHandle& Handle = *It;
					const TPair<float, float> TimeValue = Curve->GetKeyTimeValuePair(Handle);

					if (bFirst)
					{
						Result.Append(FString::Printf(TEXT("%s"), *FString::SanitizeFloat(TimeValue.Value)));
					}
					else
					{
						Result.Append(FString::Printf(TEXT(",%s"), *FString::SanitizeFloat(TimeValue.Value)));
					}
					bFirst = false;
				}
			}
		}
	}

	if (Result.IsEmpty())
	{
		Result = TEXT("No values");
	}
	else
	{
		Result.Append(TEXT(")"));
	}

	return AIChatCommand::MakePlainResponse(Result);
}

FChatCommandResponse AIChatCommandManager::ListCurveValuesCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	return ListCurveValuesRCONCommand(Params);
}

FChatCommandResponse AIChatCommandManager::SetAttributeCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	float Value;
	FString AttributeName;
	AIBaseCharacter* IBaseCharacter = nullptr;
	if (Params.Num() == 2) // Act on self
	{
		APawn* PlayerPawn = CallingPlayer->GetPawn();
		if (PlayerPawn == nullptr)
		{
			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdSetAttribNoPlayerPawn"));
		}
		IBaseCharacter = Cast<AIBaseCharacter>(PlayerPawn);
		AttributeName = Params[0].ToLower();
		if (!FDefaultValueHelper::ParseFloat(Params[1], Value))
		{
			return GetResponseCmdSetAttribCantParseFloat(Params[1]);
		}
	}
	else if (Params.Num() == 3) // Act on target
	{
		FString Username = Params[1];
		AIPlayerController* TargetPlayer = Cast<AIPlayerController>(PlayerControllerFromUsername(CallingPlayer, Username));
		if (TargetPlayer == nullptr)
		{
			return GetResponseCmdInvalidUsername(Username);
		}
		APawn* PlayerPawn = TargetPlayer->GetPawn();
		if (PlayerPawn == nullptr)
		{
			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdSetAttribNoPlayerPawn"));
		}
		IBaseCharacter = Cast<AIBaseCharacter>(PlayerPawn);
		AttributeName = Params[0].ToLower();
		if (!FDefaultValueHelper::ParseFloat(Params[2], Value))
		{
			return GetResponseCmdSetAttribCantParseFloat(Params[2]);
		}
	}
	else
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	FGameplayAttribute Attribute;
	if (AttributeName == TEXT("health"))
	{
		Attribute = UCoreAttributeSet::GetHealthAttribute();
	}
	else if (AttributeName == TEXT("growth"))
	{
		Attribute = UCoreAttributeSet::GetGrowthAttribute();
	}
	else if (AttributeName == TEXT("stamina"))
	{
		Attribute = UCoreAttributeSet::GetStaminaAttribute();
	}
	else if (AttributeName == TEXT("hunger"))
	{
		Attribute = UCoreAttributeSet::GetHungerAttribute();
	}
	else if (AttributeName == TEXT("oxygen"))
	{
		Attribute = UCoreAttributeSet::GetOxygenAttribute();
	}
	else if (AttributeName == TEXT("thirst"))
	{
		Attribute = UCoreAttributeSet::GetThirstAttribute();
	}
	else if (AttributeName == TEXT("wetness"))
	{
		Attribute = UCoreAttributeSet::GetWetnessAttribute();
	}

	Value = FMath::Clamp(Value, -CLAMP_MINMAX, CLAMP_MINMAX);

	if (IBaseCharacter != nullptr)
	{
		UPOTAbilitySystemGlobals::ApplyDynamicGameplayEffect(IBaseCharacter, Attribute, Value, EGameplayModOp::Override);
		const FFormatNamedArguments Arguments{
			{ TEXT("Attribute"), FText::FromString(AttributeName) },
			{ TEXT("Number"), Value }
		};

		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdSetAttribSuccess"), Arguments);
	}
	return FChatCommandResponse();
}

int32 AIChatCommandManager::SetMarks(AIPlayerController* IPlayerController, int32 Marks, bool bSet /*= false*/, bool bAdd /*= false*/)
{
	AIBaseCharacter* IBaseCharacter = Cast<AIBaseCharacter>(IPlayerController->GetCharacter());
	if (!IsValid(IBaseCharacter))
	{
		return 0;
	}

	int32 NewMarks = Marks;
	int32 OriginalMarks = IBaseCharacter->GetMarks();
	if (!bSet)
	{
		if (bAdd)
		{
			NewMarks = IBaseCharacter->GetMarks() + Marks;
		}
		else
		{
			NewMarks = IBaseCharacter->GetMarks() - Marks;
		}
	}
	NewMarks = FMath::Clamp(NewMarks, 0, CommandMarksLimit);
	IBaseCharacter->SetMarks(NewMarks);

	if (!bSet)
	{
		// return difference for /addmarks/removemarks chat output
		return abs(NewMarks - OriginalMarks);
	}
	return NewMarks;
}

int32 AIChatCommandManager::GetMarks(AIPlayerController* IPlayerController)
{
	AIBaseCharacter* IBaseCharacter = Cast<AIBaseCharacter>(IPlayerController->GetCharacter());
	if (!IsValid(IBaseCharacter))
	{
		return 0;
	}

	return IBaseCharacter->GetMarks();
}

void AIChatCommandManager::ClampMarksUserInput(int32& Marks)
{
	// if user inputs a huge number ParseInt sets it to -1
	if (Marks < 0)
	{
		Marks = CommandMarksLimit;
	}
	else
	{
		Marks = FMath::Clamp(Marks, 0, CommandMarksLimit);
	}
}

FChatCommandResponse AIChatCommandManager::SetMarksRCONCommand(TArray<FString> Params)
{
	if (Params.Num() != 3)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}
	FString Username = Params[1];

	AIPlayerController* IPlayerController = Cast<AIPlayerController>(PlayerControllerFromUsername(this, Username));
	if (IPlayerController == nullptr)
	{
		return GetResponseCmdInvalidUsername(Username);
	}

	int32 Marks = 0;
	if (FDefaultValueHelper::ParseInt(Params[2], Marks))
	{
		ClampMarksUserInput(Marks);
		SetMarks(IPlayerController, Marks, true);
		const FFormatNamedArguments Arguments{
			{ TEXT("Player"), FText::FromString(Username) },
			{ TEXT("Marks"), Marks }
		};

		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdSetMarksOtherSuccess"), Arguments);
	}
	else
	{
		const FFormatNamedArguments Arguments{ 
			{ TEXT("Number"), FText::FromString(Params[2]) } 
		};

		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdSetMarksCantParseInt"), Arguments);
	}
}

FChatCommandResponse AIChatCommandManager::SkipShedCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	AIPlayerController* TargetPlayerController = nullptr;

	FString Username = TEXT("");

	if (Params.Num() == 1) // self
	{
		if (CallingPlayer && CallingPlayer->GetPlayerState<APlayerState>())
		{
			Username = CallingPlayer->GetPlayerState<APlayerState>()->GetPlayerName();
		}
		TargetPlayerController = CallingPlayer;
	}
	else if (Params.Num() == 2) // other
	{
		Username = Params[1];
		TargetPlayerController = Cast<AIPlayerController>(PlayerControllerFromUsername(this, Username));
	}
	else
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	if (!TargetPlayerController)
	{
		return GetResponseCmdInvalidUsername(Username);
	}

	if (AIDinosaurCharacter* IDinoCharacter = TargetPlayerController->GetPawn<AIDinosaurCharacter>())
	{
		if (IDinoCharacter->GetSheddingProgressRaw() > 0)
		{
			IDinoCharacter->SetSheddingProgress(0);
			const FFormatNamedArguments Arguments{
				{ TEXT("Username"), FText::FromString(Username) } 
			};

			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdSkipShedSuccess"), Arguments);
		}
		else
		{
			const FFormatNamedArguments Arguments{
				{ TEXT("Username"), FText::FromString(Username) }
			};

			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdSkipShedNotShedding"), Arguments);
		}
	}

	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdSkipShedNoPawn"));
}

FChatCommandResponse AIChatCommandManager::SkipShedRCONCommand(TArray<FString> Params)
{
	if (Params.Num() != 2) // other
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	const FString Username = Params[1];
	AIPlayerController* const TargetPlayerController = Cast<AIPlayerController>(PlayerControllerFromUsername(this, Username));

	if (!TargetPlayerController)
	{
		return GetResponseCmdInvalidUsername(Username);
	}

	AIDinosaurCharacter* const IDinoCharacter = TargetPlayerController->GetPawn<AIDinosaurCharacter>();

	if (!IDinoCharacter)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdSkipShedNoPawn"));
	}

	const FFormatNamedArguments Arguments{
		{ TEXT("Username"), FText::FromString(Username) }
	};

	if (IDinoCharacter->GetSheddingProgressRaw() <= 0)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdSkipShedNotShedding"), Arguments);
	}

	IDinoCharacter->SetSheddingProgress(0);
	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdSkipShedSuccess"), Arguments);
}

FChatCommandResponse AIChatCommandManager::SetMarksAllRCONCommand(TArray<FString> Params)
{
	return SetMarksAllCommand(nullptr, Params);
}

FChatCommandResponse AIChatCommandManager::SetMarksAllCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	if (Params.Num() != 2)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	int32 Marks = 0;
	if (FDefaultValueHelper::ParseInt(Params[1], Marks))
	{
		ClampMarksUserInput(Marks);
		AIGameState* IGameState = UIGameplayStatics::GetIGameStateChecked(this);
		if (!IGameState)
		{
			return GetResponseCmdNullObject(TEXT("IGameState"));
		}

		int32 Count = 0;

		for (APlayerState* PlayerState : IGameState->PlayerArray)
		{
			if (AIPlayerController* IPlayerController = Cast<AIPlayerController>(PlayerState->GetPlayerController()))
			{
				if (IPlayerController->GetPawn())
				{
					Count++;
					SetMarks(IPlayerController, Marks, true);
				}
			}
		}
		const FFormatNamedArguments Arguments{
			{ TEXT("Marks"), Marks },
			{ TEXT("Count"), Count }
		};

		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdSetMarksAllSuccess"), Arguments);
	}

	const FFormatNamedArguments Arguments{
		{ TEXT("Number"), FText::FromString(Params[1]) }
	};

	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdSetMarksCantParseInt"), Arguments);
}

FChatCommandResponse AIChatCommandManager::SetMarksCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	if (Params.Num() == 2) // Setting own marks
	{
		int32 Marks = 0;
		if (FDefaultValueHelper::ParseInt(Params[1], Marks))
		{
			ClampMarksUserInput(Marks);
			SetMarks(CallingPlayer, Marks, true);
			const FFormatNamedArguments Arguments{
				{ TEXT("Marks"), Marks }
			};

			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdSetMarksSuccess"), Arguments);
		}
		else
		{
			const FFormatNamedArguments Arguments{
				{ TEXT("Number"), FText::FromString(Params[1]) }
			};

			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdSetMarksCantParseInt"), Arguments);
		}
	}
	else if (Params.Num() == 3) // Set marks of username
	{
		FString Username = Params[1];

		AIPlayerController* IPlayerController = Cast<AIPlayerController>(PlayerControllerFromUsername(CallingPlayer, Username));
		if (IPlayerController == nullptr)
		{
			return GetResponseCmdInvalidUsername(Username);
		}

		int32 Marks = 0;
		if (FDefaultValueHelper::ParseInt(Params[2], Marks))
		{
			ClampMarksUserInput(Marks);
			SetMarks(IPlayerController, Marks, true);
			const FFormatNamedArguments Arguments{
				{ TEXT("Player"), FText::FromString(Username) },
				{ TEXT("Marks"), Marks }
			};

			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdSetMarksOtherSuccess"), Arguments);
		}
		else
		{
			const FFormatNamedArguments Arguments{ 
				{ TEXT("Number"), FText::FromString(Params[2]) }
			};

			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdSetMarksCantParseInt"), Arguments);
		}
	}
	return FChatCommandResponse();
}

FChatCommandResponse AIChatCommandManager::AddMarksRCONCommand(TArray<FString> Params)
{
	if (Params.Num() != 3)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}
	FString Username = Params[1];

	AIPlayerController* IPlayerController = Cast<AIPlayerController>(PlayerControllerFromUsername(this, Username));
	if (IPlayerController == nullptr)
	{
		return GetResponseCmdInvalidUsername(Username);
	}

	int32 Marks = 0;
	if (FDefaultValueHelper::ParseInt(Params[2], Marks))
	{
		ClampMarksUserInput(Marks);
		Marks = SetMarks(IPlayerController, Marks, false, true);
		const FFormatNamedArguments Arguments{
			{ TEXT("Player"), FText::FromString(Username) },
			{ TEXT("Marks"), Marks },
			{ TEXT("CurrentMarks"), GetMarks(IPlayerController) }
		};

		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdAddMarksOtherSuccess"), Arguments);
	}
	else
	{
		const FFormatNamedArguments Arguments{
			{ TEXT("Number"), FText::FromString(Params[2]) }
		};

		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdSetMarksCantParseInt"), Arguments);
	}
}

FChatCommandResponse AIChatCommandManager::AddMarksAllRCONCommand(TArray<FString> Params)
{
	if (Params.Num() != 2)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	int32 Marks = 0;
	if (!FDefaultValueHelper::ParseInt(Params[1], Marks))
	{
		const FFormatNamedArguments Arguments{
			{ TEXT("Number"), FText::FromString(Params[2]) }
		};
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdSetMarksCantParseInt"), Arguments);
	}

	ClampMarksUserInput(Marks);

	int32 PlayersGivenMarks = 0;

	for (FConstPlayerControllerIterator PlayerIt = GetWorld()->GetPlayerControllerIterator(); PlayerIt; ++PlayerIt)
	{
		AIPlayerController* const IPlayerController = Cast<AIPlayerController>(*PlayerIt);
		if (!IPlayerController)
		{
			continue;
		}
		SetMarks(IPlayerController, Marks, false, true);
		PlayersGivenMarks++;
	}

	const FFormatNamedArguments Arguments{
		{ TEXT("Marks"), Marks },
		{ TEXT("PlayerNum"), PlayersGivenMarks }
	};

	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdAddMarksAllSuccess"), Arguments);
}

FChatCommandResponse AIChatCommandManager::AddMarksCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	if (Params.Num() == 2) // Setting own marks
	{
		int32 Marks = 0;
		if (FDefaultValueHelper::ParseInt(Params[1], Marks))
		{
			ClampMarksUserInput(Marks);
			Marks = SetMarks(CallingPlayer, Marks, false, true);
			const FFormatNamedArguments Arguments{
				{ TEXT("Marks"), Marks },
				{ TEXT("CurrentMarks"), GetMarks(CallingPlayer) }
			};

			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdAddMarksSuccess"), Arguments);
		}
		else
		{
			const FFormatNamedArguments Arguments{
				{ TEXT("Number"), FText::FromString(Params[1]) }
			};

			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdSetMarksCantParseInt"), Arguments);
		}
	}
	else if (Params.Num() == 3) // Set marks of username
	{
		FString Username = Params[1];

		AIPlayerController* IPlayerController = Cast<AIPlayerController>(PlayerControllerFromUsername(CallingPlayer, Username));
		if (IPlayerController == nullptr)
		{
			return GetResponseCmdInvalidUsername(Username);
		}

		int32 Marks = 0;
		if (FDefaultValueHelper::ParseInt(Params[2], Marks))
		{
			ClampMarksUserInput(Marks);
			Marks = SetMarks(IPlayerController, Marks, false, true);
			const FFormatNamedArguments Arguments{
				{ TEXT("Player"), FText::FromString(Username) },
				{ TEXT("Marks"), Marks },
				{ TEXT("CurrentMarks"), GetMarks(IPlayerController) }
			};

			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdAddMarksOtherSuccess"), Arguments);
		}
		else
		{
			const FFormatNamedArguments Arguments{
				{ TEXT("Number"), FText::FromString(Params[2]) }
			};

			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdSetMarksCantParseInt"), Arguments);
		}
	}
	return FChatCommandResponse();
}

FChatCommandResponse AIChatCommandManager::RemoveMarksRCONCommand(TArray<FString> Params)
{
	if (Params.Num() != 3)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}
	FString Username = Params[1];

	AIPlayerController* IPlayerController = Cast<AIPlayerController>(PlayerControllerFromUsername(this, Username));
	if (IPlayerController == nullptr)
	{
		return GetResponseCmdInvalidUsername(Username);
	}

	int32 Marks = 0;
	if (FDefaultValueHelper::ParseInt(Params[2], Marks))
	{
		ClampMarksUserInput(Marks);
		Marks = SetMarks(IPlayerController, Marks, false, false);

		const FFormatNamedArguments Arguments{
			{ TEXT("Player"), FText::FromString(Username) },
			{ TEXT("Marks"), Marks },
			{ TEXT("CurrentMarks"), GetMarks(IPlayerController) }
		};

		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdRemoveMarksOtherSuccess"), Arguments);
	}
	else
	{
		const FFormatNamedArguments Arguments{ 
			{ TEXT("Number"), FText::FromString(Params[2]) }
		};

		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdSetMarksCantParseInt"), Arguments);
	}
}

FChatCommandResponse AIChatCommandManager::RemoveMarksCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	if (Params.Num() == 2) // Setting own marks
	{
		int32 Marks = 0;
		if (FDefaultValueHelper::ParseInt(Params[1], Marks))
		{
			ClampMarksUserInput(Marks);
			Marks = SetMarks(CallingPlayer, Marks, false, false);
			const FFormatNamedArguments Arguments{
				{ TEXT("Marks"), Marks },
				{ TEXT("CurrentMarks"), GetMarks(CallingPlayer) }
			};

			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdRemoveMarksSuccess"), Arguments);
		}
		else
		{
			const FFormatNamedArguments Arguments{ 
				{ TEXT("Number"), FText::FromString(Params[1]) }
			};

			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdSetMarksCantParseInt"), Arguments);
		}
	}
	else if (Params.Num() == 3) // Set marks of username
	{
		FString Username = Params[1];

		AIPlayerController* IPlayerController = Cast<AIPlayerController>(PlayerControllerFromUsername(CallingPlayer, Username));
		if (IPlayerController == nullptr)
		{
			return GetResponseCmdInvalidUsername(Username);
		}

		int32 Marks = 0;
		if (FDefaultValueHelper::ParseInt(Params[2], Marks))
		{
			ClampMarksUserInput(Marks);
			Marks = SetMarks(IPlayerController, Marks, false, false);
			const FFormatNamedArguments Arguments{
				{ TEXT("Player"), FText::FromString(Username) },
				{ TEXT("Marks"), Marks },
				{ TEXT("CurrentMarks"), GetMarks(CallingPlayer) }
			};

			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdRemoveMarksOtherSuccess"), Arguments);
		}
		else
		{
			const FFormatNamedArguments Arguments{ 
				{ TEXT("Number"), FText::FromString(Params[2]) } 
			};

			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdSetMarksCantParseInt"), Arguments);
		}
	}
	return FChatCommandResponse();
}

AIWater* AIChatCommandManager::GetIWater(UWorld* World, const FString& WaterTag)
{
	TArray<AActor*> Waters;
	UGameplayStatics::GetAllActorsOfClass(World, AIWater::StaticClass(), Waters);

	for (int32 i = 0; i < Waters.Num(); i++)
	{
		AIWater* const IWater = Cast<AIWater>(Waters[i]);

		if (IWater->GetIndentifier().ToString().Equals(WaterTag, ESearchCase::IgnoreCase))
		{
			return IWater;
		}
	}
	return nullptr;
}

AIWaystone* AIChatCommandManager::GetIWaystone(UWorld* World, const FString& WaystoneTag)
{
	TArray<AActor*> Waystones;
	UGameplayStatics::GetAllActorsOfClass(World, AIWaystone::StaticClass(), Waystones);

	for (int32 i = 0; i < Waystones.Num(); i++)
	{
		AIWaystone* IWaystone = Cast<AIWaystone>(Waystones[i]);

		if (IWaystone->WaystoneTag.ToString().ToLower() == WaystoneTag.ToLower())
		{
			return IWaystone;
		}
	}
	return nullptr;
}

AActor* AIChatCommandManager::GetPoi(UObject* WorldContextObject, const FString& PoiName)
{
	TArray<AActor*> Pois;
	TArray<AActor*> Pois2;
	UGameplayStatics::GetAllActorsOfClass(AIChatCommandManager::Get(WorldContextObject), AIPointOfInterest::StaticClass(), Pois);
	UGameplayStatics::GetAllActorsOfClass(AIChatCommandManager::Get(WorldContextObject), AIPOI::StaticClass(), Pois2);

	TArray<AActor*> PossiblePois;

	for (int32 i = 0; i < Pois.Num(); i++)
	{
		if (AIPointOfInterest* IPointOfInterest = Cast<AIPointOfInterest>(Pois[i]))
		{
			if (IPointOfInterest->GetLocationTag().ToString().ToLower() == PoiName.ToLower())
			{
				PossiblePois.Add(IPointOfInterest);
			}
		}
	}

	for (int32 i = 0; i < Pois2.Num(); i++)
	{
		if (AIPOI* IPointOfInterest = Cast<AIPOI>(Pois2[i]))
		{
			if (IPointOfInterest->GetLocationTag().ToString().ToLower() == PoiName.ToLower())
			{
				PossiblePois.Add(IPointOfInterest);
			}
		}
	}

	if (PossiblePois.Num() > 0)
	{
		return PossiblePois[FMath::RandRange(0, PossiblePois.Num() - 1)];
	}

	return nullptr;
}

bool AIChatCommandManager::GetLocationFromPoi(FString PoiName, FVector& Location, bool bAllowWater, float ActorHalfHeight)
{
	TArray<AActor*> Pois;
	TArray<AActor*> Pois2;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AIPointOfInterest::StaticClass(), Pois);
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AIPOI::StaticClass(), Pois2);

	UE_LOG(TitansLog, Log, TEXT("UIChatCommandManager::GetLocationFromPoi(): Found %i Points of interest"), Pois.Num() + Pois2.Num());

	for (int32 i = 0; i < Pois.Num(); i++)
	{
		if (AIPointOfInterest* IPointOfInterest = Cast<AIPointOfInterest>(Pois[i]))
		{
			if (IPointOfInterest->GetLocationTag().ToString().ToLower() == PoiName.ToLower())
			{
				return GetLocationFromPoi(IPointOfInterest, Location, bAllowWater, ActorHalfHeight);
			}
		}
	}
	for (int32 i = 0; i < Pois2.Num(); i++)
	{
		if (AIPOI* IPoi = Cast<AIPOI>(Pois2[i]))
		{
			if (IPoi->GetLocationTag().ToString().ToLower() == PoiName.ToLower())
			{
				return GetLocationFromPoi(IPoi, Location, bAllowWater, ActorHalfHeight);
			}
		}
	}
	UE_LOG(TitansLog, Log, TEXT("UIChatCommandManager::GetLocationFromPoi(): Could not find safe location"));

	return false;
}

bool AIChatCommandManager::GetLocationFromPoi(AActor* Poi, FVector& Location, bool bAllowWater, float ActorHalfHeight)
{
	for (int32 Tries = 0; Tries < 25; Tries++)
	{
		FVector TempLocation = Location;
		if (InternalGetLocationFromPoi(Poi, TempLocation, bAllowWater))
		{
			Location = TempLocation;
			return true;
		}
	}

	if (!bAllowWater)
	{
		// If water is not allowed but we couldnt find land, try again and just go into water
		for (int32 Tries = 0; Tries < 5; Tries++)
		{
			FVector TempLocation = Location;
			if (InternalGetLocationFromPoi(Poi, TempLocation, true, ActorHalfHeight))
			{
				Location = TempLocation;
				return true;
			}
		}
	}

	return false;
}

bool AIChatCommandManager::InternalGetLocationFromPoi(AActor* Poi, FVector& Location, bool bIsWaterAllowed, float ActorHalfHeight)
{
	FVector UnsafeLocation;
	float PoiRadius = 1000;
	bool bFoundRadius = false;

	if (AIPointOfInterest* IPointOfInterest = Cast<AIPointOfInterest>(Poi))
	{
		UShapeComponent* ShapeComponent = IPointOfInterest->GetCollisionComponent();
		if (ShapeComponent != nullptr)
		{
			USphereComponent* TriggerSphere = Cast<USphereComponent>(ShapeComponent);
			if (TriggerSphere != nullptr)
			{
				PoiRadius = TriggerSphere->GetScaledSphereRadius();
				bFoundRadius = true;
			}
		}
	}

	if (!bFoundRadius) // POI did not have a sphere component, we can try to get the bounds of a static mesh
	{
		if (AIPOI* IPointOfInterest = Cast<AIPOI>(Poi))
		{
			if (IPointOfInterest->GetMesh())
			{
				FVector Min, Max;
				IPointOfInterest->GetMesh()->GetLocalBounds(Min, Max);
				FVector Difference = Max - Min;
				if (Difference.Size() > KINDA_SMALL_NUMBER)
				{
					PoiRadius = FMath::Max3(Difference.X, Difference.Y, Difference.Z) / 4.0f;
					bFoundRadius = true;
				}
			}
		}
	}

	// Get a random location on the X and Y plane of the POI location within its radius
	UnsafeLocation = Poi->GetActorLocation() + ((FMath::VRand() * FVector{ 1, 1, 0 }) * PoiRadius);

	Location = UnsafeLocation;

	TArray<FHitResult> UnsortedHitResults;

	FVector StartVector = UnsafeLocation + FVector(0, 0, 100000);
	FVector EndVector = UnsafeLocation - FVector(0, 0, 100000);
	UnsortedHitResults = UIGameplayStatics::LineTraceAllByChannel(GetWorld(), StartVector, EndVector, COLLISION_DINOCAPSULE);
	UnsortedHitResults.Append(UIGameplayStatics::LineTraceAllByChannel(GetWorld(), StartVector, EndVector, ECC_WorldStatic));

	Algo::Sort(UnsortedHitResults, [StartVector](FHitResult& Lhs, FHitResult& Rhs) {
		return FVector::Distance(StartVector, Lhs.ImpactPoint) < FVector::Distance(StartVector, Rhs.ImpactPoint);
	});

	for (FHitResult Result : UnsortedHitResults)
	{
		if (Cast<ABlockingVolume>(Result.GetActor()))
		{
			continue;
		}
		if (!bIsWaterAllowed && Cast<AIWater>(Result.GetActor()))
		{
			return false; // if water is hit before land then return false
		}
		if (Cast<ALandscapeProxy>(Result.GetActor()) || Cast<AStaticMeshActor>(Result.GetActor()) || (bIsWaterAllowed && Cast<AIWater>(Result.GetActor())))
		{
			if (Cast<AIWater>(Result.GetActor()))
			{
				Location = Result.ImpactPoint;
			}
			else
			{
				Location = Result.ImpactPoint + FVector(0, 0, ActorHalfHeight);
			}

			// Make sure that there is no blocking volume between the player spawn location and the POI
			TArray<UClass*> Classes;
			Classes.Add(ABlockingVolume::StaticClass());
			if (UIGameplayStatics::LineTraceClassesByChannel(Poi->GetWorld(), Poi->GetActorLocation(), Location, ECC_WorldStatic, Classes).Num() > 0)
			{
				return false;
			}

			return true;
		}
	}

	return false;
}

FChatCommandResponse AIChatCommandManager::ToggleAdminCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{

	AIGameMode* IGameMode = Cast<AIGameMode>(UGameplayStatics::GetGameMode(CallingPlayer->GetWorld()));
	if (IGameMode != nullptr)
	{
		AIGameSession* IGameSession = Cast<AIGameSession>(IGameMode->GameSession);
		if (IGameSession != nullptr)
		{
			if (IGameSession->IsAdmin(CallingPlayer))
			{
				IGameSession->RemoveAdmin(CallingPlayer);
				return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdToggleAdminYes"));
			}
			else
			{
				IGameSession->AddAdmin(CallingPlayer);
				return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdToggleAdminNo"));
			}
		}
	}
	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdToggleAdminFailed"));
}

FChatCommandResponse AIChatCommandManager::SetTimeCommand(TArray<FString> Params)
{
	if (!GetWorld())
	{
		return GetResponseCmdNullObject(TEXT("GetWorld()"));
	}
	FString TimeString = TEXT("");
	if (Params.Num() == 1)
	{
		TimeString = Params[0].ToLower();
	}
	else if (Params.Num() == 2)
	{
		TimeString = Params[1].ToLower();
	}
	else
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	float TimeValue = 0;

	if (TimeString == TEXT("morning"))
	{
		TimeValue = 900;
	}
	else if (TimeString == TEXT("day"))
	{
		TimeValue = 1200;
	}
	else if (TimeString == TEXT("night"))
	{
		TimeValue = 1800;
	}
	else if (TimeString.IsNumeric())
	{
		TimeValue = FCString::Atof(*TimeString);
	}
	else
	{
		const FFormatNamedArguments Arguments{
			{ TEXT("TimeString"), FText::FromString(TimeString) }
		};

		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdSetTimeCantParseInput"), Arguments);
	}

	// Validate Accessor Objects
	AIWorldSettings* IWorldSettings = AIWorldSettings::GetWorldSettings(this);
	if (!IWorldSettings)
	{
		return GetResponseCmdNullObject(TEXT("IWorldSettings"));
	}
	AIUltraDynamicSky* Sky = IWorldSettings->UltraDynamicSky;
	if (!Sky)
	{
		return GetResponseCmdNullObject(TEXT("Sky"));
	}
	AIGameState* IGameState = UIGameplayStatics::GetIGameState(this);
	if (!IGameState)
	{
		return GetResponseCmdNullObject(TEXT("IGameState"));
	}

	// Clamp Value between valid time of day values
	TimeValue = FMath::Clamp(TimeValue, 0.f, 2400.f);

	// Fix for TimeValue not replicating if value is equal to previous
	if (Sky->GetSkipToTime() == TimeValue)
	{
		TimeValue += TimeValue == 2400.f ? -1.0f : 1.0f;
	}

	// Set Time of Day on Sky & Replicate it to everyone
	Sky->SetSkipToTime(TimeValue);
	Sky->OnRep_TimeSkip();

	const FFormatNamedArguments Arguments{ 
		{ TEXT("TimeString"), TimeValue }
	};

	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdSetTimeSuccess"), Arguments);
}

FChatCommandResponse AIChatCommandManager::BringAllCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	if (Params.Num() != 1)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	check(CallingPlayer);
	if (!CallingPlayer)
	{
		return FChatCommandResponse();
	}
	AIBaseCharacter* Pawn = CallingPlayer->GetPawn<AIBaseCharacter>();
	if (!Pawn)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdBringAllNoPawn"));
	}

	TeleportAllLocation(CallingPlayer, Pawn->GetActorLocation() + FVector(200, 200, 0), Pawn->GetCurrentInstance());
	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdBringAllSuccess"));
}

FChatCommandResponse AIChatCommandManager::BringAllOfSpeciesCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	FString Result(TEXT("CmdBringAllOfSpeciesSuccess"));

	if (Params.Num() != 2)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	check(CallingPlayer);
	if (!CallingPlayer)
	{
		return FChatCommandResponse();
	}

	const AIGameState* const IGameState = UIGameplayStatics::GetIGameState(AIChatCommandManager::Get(CallingPlayer));
	check(IGameState);
	if (!IGameState)
	{
		return FChatCommandResponse();
	}

	AIBaseCharacter* const Pawn = CallingPlayer->GetPawn<AIBaseCharacter>();
	if (!Pawn)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdBringAllNoPawn"));
	}

	const FString SpeciesString = Params[1];

	TArray<APlayerState*> PlayerStatesToTeleport = IGameState->PlayerArray.FilterByPredicate([&SpeciesString](const TObjectPtr<APlayerState> PlayerState) {
		const AIDinosaurCharacter* const Dino = Cast<AIDinosaurCharacter>(PlayerState->GetPawn());
		return Dino && SpeciesString.Equals(Dino->SpeciesName.ToString(), ESearchCase::IgnoreCase);
	});

	if (!PlayerStatesToTeleport.IsEmpty())
	{
		TeleportGroupLocation(CallingPlayer, PlayerStatesToTeleport, Pawn->GetActorLocation() + FVector(200, 200, 0), Pawn->GetCurrentInstance());
	}
	else
	{
		Result = FString(TEXT("CmdBringAllOfSpeciesFailure"));
	}

	const FFormatNamedArguments Arguments{ 
		{ TEXT("Species"), FText::FromString(Params[1]) }
	};
	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), Result, Arguments);
}

FChatCommandResponse AIChatCommandManager::BringAllOfDietTypeCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	FString Result = TEXT("CmdBringAllOfDietTypeSuccess");

	if (Params.Num() != 2)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	check(CallingPlayer);
	if (!CallingPlayer)
	{
		return FChatCommandResponse();
	}

	const AIGameState* const IGameState = UIGameplayStatics::GetIGameState(AIChatCommandManager::Get(CallingPlayer));
	check(IGameState);
	if (!IGameState)
	{
		return FChatCommandResponse();
	}

	AIBaseCharacter* const Pawn = CallingPlayer->GetPawn<AIBaseCharacter>();
	if (!Pawn)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdBringAllNoPawn"));
	}

	TArray<APlayerState*> PlayerStatesToTeleport{};
	const FName Param = FName(*Params[1]);

	const int64 FoundDietType = StaticEnum<EDietaryRequirements>()->GetValueByName(Param);
	if (FoundDietType != INDEX_NONE)
	{
		PlayerStatesToTeleport = IGameState->PlayerArray.FilterByPredicate([&FoundDietType](const TObjectPtr<APlayerState> PlayerState) {
			const AIDinosaurCharacter* const Dino = Cast<AIDinosaurCharacter>(PlayerState->GetPawn());
			return Dino && Dino->DietRequirements == static_cast<EDietaryRequirements>(FoundDietType);
		});
	}

	if (!PlayerStatesToTeleport.IsEmpty())
	{
		TeleportGroupLocation(CallingPlayer, PlayerStatesToTeleport, Pawn->GetActorLocation() + FVector(200, 200, 0), Pawn->GetCurrentInstance());
	}
	else
	{
		Result = FString(TEXT("CmdBringAllOfDietTypeFailure"));
	}

	const FFormatNamedArguments Arguments{ 
		{ TEXT("DietType"), FText::FromString(Params[1]) }
	};
	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), Result, Arguments);
}

FChatCommandResponse AIChatCommandManager::BringCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	bool bRequiresSafeTeleport = true;
	for (int32 i = 1; i < Params.Num(); i++)
	{
		if (Params[i].Equals(TEXT("unsafe"), ESearchCase::IgnoreCase))
		{
			bRequiresSafeTeleport = false;
			Params.RemoveAt(i, 1, true);
			continue;
		}
	}

	if (Params.Num() != 2)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	AIPlayerController* TargetPlayerController = Cast<AIPlayerController>(PlayerControllerFromUsername(CallingPlayer, Params[1]));
	if (!TargetPlayerController)
	{
		return GetResponseCmdInvalidUsername(Params[1]);
	}

	AIBaseCharacter* TargetPawn = Cast<AIBaseCharacter>(CallingPlayer->GetPawn());
	if (!TargetPawn)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdBringInvalidPawn"));
	}
	if (!TargetPawn->HasLeftHatchlingCave() && TargetPawn->GetCurrentInstance())
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdCannotBringToHatchling"));
	}

	bool bTeleported = false;

	if (bRequiresSafeTeleport)
	{
		TeleportLocation(TargetPlayerController, TargetPawn->GetActorLocation() + FVector{ 400, 0, 0 }, TargetPawn->GetCurrentInstance(), false);
		bTeleported = true;
	}
	else
	{
		TeleportLocationUnsafe(TargetPlayerController, TargetPawn->GetActorLocation() + FVector{ 400, 0, 0 }, TargetPawn->GetCurrentInstance());
		bTeleported = true;
	}
	const FFormatNamedArguments Arguments{
		{ TEXT("Username"), FText::FromString(Params[1]) }
	};

	if (bTeleported)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdBringSuccess"), Arguments);
	}
	else
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdBringFailed"), Arguments);
	}
}

FChatCommandResponse AIChatCommandManager::GotoCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	check(CallingPlayer);
	if (!CallingPlayer)
	{
		return FChatCommandResponse();
	}

#if !UE_SERVER
	if (CallingPlayer->IsLocalPlayerController() && (CallingPlayer->GetNetMode() == ENetMode::NM_ListenServer || CallingPlayer->GetNetMode() == ENetMode::NM_Standalone))
	{
		// Need to handle teleporting in singleplayer differently as it needs to account for the world not being fully loaded.
		return SinglePlayerGoto(CallingPlayer, Params);
	}
#endif
	// Find optional keyword "unsafe" and remove it so we can process command normally
	bool bRequiresSafeTeleport = true;
	for (int32 i = 1; i < Params.Num(); i++)
	{
		if (Params[i].Equals(TEXT("unsafe"), ESearchCase::IgnoreCase))
		{
			bRequiresSafeTeleport = false;
			Params.RemoveAt(i, 1, true);
			continue;
		}
	}

	if (Params.Num() != 2)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}
	AIPlayerCaveBase* TargetInstance = nullptr;

	AIBaseCharacter* CallerPawn = CallingPlayer->GetPawn<AIBaseCharacter>();
	if (!CallerPawn)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdNoPawn"));
	}

	FString LocationString = Params[1];
	FVector TargetLocation;
	bool bIsValidLocation = (LocationString.Contains(TEXT("(X="), ESearchCase::IgnoreCase, ESearchDir::FromStart) && TargetLocation.InitFromString(LocationString));

	if (!bIsValidLocation)
	{
		bIsValidLocation = GetLocationFromPoi(LocationString, TargetLocation, CallerPawn->IsAquatic());
	}

	if (!bIsValidLocation)
	{
		if (AIPlayerController* LocationPlayerController = Cast<AIPlayerController>(PlayerControllerFromUsername(CallingPlayer, LocationString)))
		{
			if (AIBaseCharacter* LocationPawn = Cast<AIBaseCharacter>(LocationPlayerController->GetPawn()))
			{
				TargetLocation = LocationPawn->GetActorLocation() + FVector{ 400, 0, 0 };
				TargetInstance = LocationPawn->GetCurrentInstance();
				bIsValidLocation = true;
			}
		}
	}
	const FFormatNamedArguments Arguments{
		{ TEXT("Location"), FText::FromString(LocationString) }
	};

	if (!bIsValidLocation)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdTeleportInvalidLocation"), Arguments);
	}

	bool bTeleported = false;

	if (bRequiresSafeTeleport)
	{
		TeleportLocation(CallingPlayer, TargetLocation, TargetInstance, false);
		bTeleported = true;
	}
	else
	{
		TeleportLocationUnsafe(CallingPlayer, TargetLocation, TargetInstance);
		bTeleported = true;
	}

	if (bTeleported)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdGotoSuccess"), Arguments);
	}
	else
	{

		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdGotoFailed"), Arguments);
	}
}

FChatCommandResponse AIChatCommandManager::TeleportRCONCommand(TArray<FString> Params)
{
	FVector TargetLocation;
	FString LocationString;
	FString UsernameString;
	AIPlayerController* TargetPlayerController;
	bool bRequiresSafeTeleport = true;

	for (int32 i = 1; i < Params.Num(); i++)
	{
		if (Params[i].Equals(TEXT("unsafe"), ESearchCase::IgnoreCase))
		{
			bRequiresSafeTeleport = false;
			Params.RemoveAt(i, 1, true);
			continue;
		}
	}

	if (Params.Num() == 3)
	{
		LocationString = Params[2];
		TargetPlayerController = Cast<AIPlayerController>(PlayerControllerFromUsername(this, Params[1]));

		if (!TargetPlayerController)
		{
			return GetResponseCmdInvalidUsername(Params[1]);
		}
	}
	else
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	AIBaseCharacter* CallerPawn = TargetPlayerController->GetPawn<AIBaseCharacter>();
	if (!CallerPawn)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdNoPawn"));
	}

	AIPlayerCaveBase* TargetInstance = nullptr;
	bool bUsingPoi = false;
	if (LocationString.Contains(TEXT("(X="), ESearchCase::IgnoreCase, ESearchDir::FromStart) && TargetLocation.InitFromString(LocationString))
	{
	}
	else if (AIPlayerController* LocationPlayerController = Cast<AIPlayerController>(PlayerControllerFromUsername(this, LocationString)))
	{
		if (AIBaseCharacter* LocationPawn = Cast<AIBaseCharacter>(LocationPlayerController->GetPawn()))
		{
			TargetLocation = LocationPawn->GetActorLocation() + FVector{ 200, 0, 0 };
			TargetInstance = LocationPawn->GetCurrentInstance();
		}
	}
	else if (GetLocationFromPoi(LocationString, TargetLocation, CallerPawn->IsAquatic()))
	{
		bUsingPoi = true;
	}
	else
	{
		const FFormatNamedArguments Arguments{
			{ TEXT("Location"), FText::FromString(LocationString) }
		};

		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdTeleportInvalidLocation"), Arguments);
	}

	if (bRequiresSafeTeleport)
	{
		TeleportLocation(TargetPlayerController, TargetLocation, TargetInstance, false);
	}
	else
	{
		TeleportLocationUnsafe(TargetPlayerController, TargetLocation, TargetInstance);
	}

	FFormatNamedArguments Arguments{ { TEXT("Username"), FText::FromString(TEXT("Player")) } };

	if (APawn* Pawn = TargetPlayerController->GetPawn())
	{
		if (APlayerState* PlayerState = Pawn->GetPlayerState())
		{
			Arguments.Remove(TEXT("Username"));
			Arguments.Add(TEXT("Username"), FText::FromString(PlayerState->GetPlayerName()));
		}
	}

	Arguments.Add(TEXT("Location"), FText::FromString(LocationString));

	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdTeleportSuccess"), Arguments);
}

FChatCommandResponse AIChatCommandManager::TeleportCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	check(CallingPlayer);
	if (!CallingPlayer)
	{
		return FChatCommandResponse();
	}
#if !UE_SERVER
	if (CallingPlayer->IsLocalPlayerController() && (CallingPlayer->GetNetMode() == ENetMode::NM_ListenServer || CallingPlayer->GetNetMode() == ENetMode::NM_Standalone))
	{
		// Need to handle teleporting in singleplayer differently as it needs to account for the world not being fully loaded.
		return SinglePlayerTeleport(CallingPlayer, Params);
	}
#endif

	FVector TargetLocation;
	FString LocationString;
	FString UsernameString;
	AIPlayerController* TargetPlayerController;
	bool bRequiresSafeTeleport = true;

	for (int32 i = 1; i < Params.Num(); i++)
	{
		if (Params[i].Equals(TEXT("unsafe"), ESearchCase::IgnoreCase))
		{
			bRequiresSafeTeleport = false;
			Params.RemoveAt(i, 1, true);
			continue;
		}
	}

	if (Params.Num() == 2)
	{
		LocationString = Params[1];
		TargetPlayerController = CallingPlayer;
	}
	else if (Params.Num() == 3)
	{
		LocationString = Params[2];
		TargetPlayerController = Cast<AIPlayerController>(PlayerControllerFromUsername(CallingPlayer, Params[1]));

		if (!TargetPlayerController)
		{
			return GetResponseCmdInvalidUsername(Params[1]);
		}
	}
	else
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	AIBaseCharacter* CallerPawn = CallingPlayer->GetPawn<AIBaseCharacter>();
	if (!CallerPawn)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdNoPawn"));
	}

	AIPlayerCaveBase* TargetInstance = nullptr;
	bool bUsingPoi = false;
	if (LocationString.Contains(TEXT("(X="), ESearchCase::IgnoreCase, ESearchDir::FromStart) && TargetLocation.InitFromString(LocationString))
	{
	}
	else if (AIPlayerController* LocationPlayerController = Cast<AIPlayerController>(PlayerControllerFromUsername(CallingPlayer, LocationString)))
	{
		if (AIBaseCharacter* LocationPawn = Cast<AIBaseCharacter>(LocationPlayerController->GetPawn()))
		{
			TargetLocation = LocationPawn->GetActorLocation() + FVector{ 200, 0, 0 };
			TargetInstance = LocationPawn->GetCurrentInstance();
		}
	}
	else if (GetLocationFromPoi(LocationString, TargetLocation, CallerPawn->IsAquatic()))
	{
		bUsingPoi = true;
	}
	else
	{
		const FFormatNamedArguments Arguments{
			{ TEXT("Location"), FText::FromString(LocationString) }
		};

		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdTeleportInvalidLocation"), Arguments);
	}

	check(TargetPlayerController);
	if (!TargetPlayerController)
	{
		return FChatCommandResponse();
	}

	bool bTeleported = false;

	if (bRequiresSafeTeleport)
	{
		TeleportLocation(TargetPlayerController, TargetLocation, TargetInstance, false);
		bTeleported = true;
	}
	else
	{
		TeleportLocationUnsafe(TargetPlayerController, TargetLocation, TargetInstance);
		bTeleported = true;
	}

	FFormatNamedArguments Arguments{ { TEXT("Username"), FText::FromString(TEXT("Player")) } };

	if (APawn* Pawn = TargetPlayerController->GetPawn())
	{
		if (APlayerState* PlayerState = Pawn->GetPlayerState())
		{
			Arguments.Remove(TEXT("Username"));
			Arguments.Add(TEXT("Username"), FText::FromString(PlayerState->GetPlayerName()));
		}
	}

	Arguments.Add(TEXT("Location"), FText::FromString(LocationString));
	if (bTeleported)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdTeleportSuccess"), Arguments);
	}
	else
	{

		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdTeleportFailed"), Arguments);
	}
}

void AIChatCommandManager::TeleportLocationUnsafe(AIPlayerController* IPlayerController, FVector Location, AIPlayerCaveBase* NewInstance /*= nullptr */)
{

	AIBaseCharacter* PlayerPawn = Cast<AIBaseCharacter>(IPlayerController->GetPawn());
	check(PlayerPawn);
	if (!PlayerPawn)
	{
		return;
	}

	AIPlayerCaveBase* PlayerInstance = PlayerPawn->GetCurrentInstance();

	if (!NewInstance)
	{
		Location = AIWorldSettings::Get(IPlayerController)->AdjustForWorldBounds(Location, 100);
	}

	if (PlayerInstance)
	{
		// Player is in instance
		if (NewInstance)
		{
			if (NewInstance == PlayerInstance)
			{
				// Teleporting within the same instance
				PlayerPawn->TeleportToFromCommand(Location, PlayerPawn->GetActorRotation(), false, true);
			}
			else
			{
				// teleporting to new instance from current instance
				FTransform NewTPTransform = FTransform();
				NewTPTransform.SetLocation(Location);
				PlayerPawn->ExitInstance(FTransform());
				PlayerPawn->ServerEnterInstance(NewInstance->Tile, NewTPTransform);
			}
		}
		else
		{
			// teleporting out of instance from instance
			FTransform NewTPTransform = FTransform();
			NewTPTransform.SetLocation(Location);
			PlayerPawn->ExitInstance(NewTPTransform);
		}
	}
	else
	{
		// Player is not in instance
		if (NewInstance)
		{
			// Teleporting into new instance
			FTransform NewTPTransform = FTransform();
			NewTPTransform.SetLocation(Location);
			PlayerPawn->ServerEnterInstance(NewInstance->Tile, NewTPTransform);
		}
		else
		{
			// Teleporting to normal location
			PlayerPawn->TeleportToFromCommand(Location, PlayerPawn->GetActorRotation(), false, true);
		}
	}
	PlayerPawn->UpdateQuestsFromTeleport();
}

void AIChatCommandManager::TeleportLocation(AIPlayerController* IPlayerController, FVector Location, AIPlayerCaveBase* NewInstance /*= nullptr */, bool bAvoidWater /* = false*/)
{
	AIBaseCharacter* PlayerPawn = Cast<AIBaseCharacter>(IPlayerController->GetPawn());
	check(PlayerPawn);
	if (!PlayerPawn || (!PlayerPawn->HasLeftHatchlingCave() && PlayerPawn->GetCurrentInstance()))
	{
		return;
	}
	UWorld* World = PlayerPawn->GetWorld();
	check(World);
	if (!World)
	{
		return;
	}

	// If there is a new instance, then we dont need to do any ray casting to find safe locations.
	if (!NewInstance)
	{
		Location = AIWorldSettings::Get(IPlayerController)->AdjustForWorldBounds(Location, 100);

		// Check for water if avoid water is specified
		if (bAvoidWater)
		{
			FVector JiggleLocation = Location;
			bool bFoundGoodLocation = false;
			if (UIGameplayStatics::CheckForWater(World, JiggleLocation, PlayerPawn))
			{
				for (int32 i = 0; i < 15; i++)
				{
					// Jiggle location in all directions to find no water
					JiggleLocation = Location;
					JiggleLocation += FVector{ 600.f * i, 0, 0 }; // right
					if (!UIGameplayStatics::CheckForWater(World, JiggleLocation, PlayerPawn))
					{
						bFoundGoodLocation = true;
						break;
					}
					JiggleLocation += FVector{ -1200.f * i, 0, 0 }; // left
					if (!UIGameplayStatics::CheckForWater(World, JiggleLocation, PlayerPawn))
					{
						bFoundGoodLocation = true;
						break;
					}
					JiggleLocation += FVector{ 600.f * i, 600.f * i, 0 }; // forward
					if (!UIGameplayStatics::CheckForWater(World, JiggleLocation, PlayerPawn))
					{
						bFoundGoodLocation = true;
						break;
					}
					JiggleLocation += FVector{ 0, -1200.f * i, 0 }; // backward
					if (!UIGameplayStatics::CheckForWater(World, JiggleLocation, PlayerPawn))
					{
						bFoundGoodLocation = true;
						break;
					}
				}
			}
			if (bFoundGoodLocation)
			{
				Location = JiggleLocation;
			}
		}

		// Put the player on the ground
		{
			FHitResult HitResult;
			FCollisionQueryParams CollisionQueryParams;
			PlayerPawn->GetWorld()->LineTraceSingleByChannel(HitResult, Location + FVector(0, 0, 500), Location - FVector(0, 0, 10000), COLLISION_DINOCAPSULE, CollisionQueryParams);
			if (HitResult.bBlockingHit)
			{
				Location = HitResult.ImpactPoint;
				if (UCapsuleComponent* Capsule = PlayerPawn->GetCapsuleComponent())
				{
					// Need to add the half height after line trace
					Location += FVector(0, 0, Capsule->GetScaledCapsuleHalfHeight());
				}
			}
		}

		// Find a safe location from the grounded location just incase the player
		// is now inside something
		{
			bool bSafeTeleportLocation;
			Location = PlayerPawn->GetSafeTeleportLocation(Location, bSafeTeleportLocation);
		}
	}
	World->FindTeleportSpot(PlayerPawn, Location, PlayerPawn->GetActorRotation()); // Will update Location if safe teleport is found.

	TeleportLocationUnsafe(IPlayerController, Location, NewInstance);
}

void AIChatCommandManager::TeleportAllLocation(UObject* WorldContextObject, FVector Location, AIPlayerCaveBase* Instance /* = nullptr */)
{
	if (AIGameState* IGameState = UIGameplayStatics::GetIGameState(AIChatCommandManager::Get(WorldContextObject)))
	{
		TeleportGroupLocation(WorldContextObject, IGameState->PlayerArray, Location, Instance);
	}
}

void AIChatCommandManager::TeleportGroupLocation(UObject* WorldContextObject, TArray<APlayerState*> PlayerStates, FVector Location, AIPlayerCaveBase* Instance /* = nullptr */)
{
	int32 TeleportedPlayers = 0;
	int32 PlayerCount = PlayerStates.Num();
	int32 GridSize = (int)ceil(sqrt(PlayerStates.Num())); // Get the sqrt and round up
	int32 CellSize = 750;								  // Perhaps large enough for all dinos

	if (!Instance)
	{
		int32 GridTotalSize = CellSize * GridSize;
		Location = AIWorldSettings::Get(WorldContextObject)->AdjustForWorldBounds(Location, GridTotalSize);
	}

	for (int32 X = 0; X < GridSize; X++)
	{
		for (int32 Y = 0; Y < GridSize; Y++)
		{
			int32 PlayerIndex = Y + X * GridSize;
			if (PlayerIndex >= PlayerCount)
			{
				X = GridSize; // Do this so the X loop breaks as well
				break;
			}
			if (AIPlayerState* OtherIPlayerState = Cast<AIPlayerState>(PlayerStates[PlayerIndex]))
			{
				AIBaseCharacter* OtherPawn = Cast<AIBaseCharacter>(OtherIPlayerState->GetPawn());

				if (IsValid(OtherPawn))
				{
					if (!OtherPawn->HasLeftHatchlingCave() && OtherPawn->GetCurrentInstance())
					{
						continue;
					}
					FVector DesiredLocation = Location + FVector{ (float)(X * CellSize) - (CellSize / 2), (float)(Y * CellSize) - (CellSize / 2), 0 };

					if (FVector::Distance(OtherPawn->GetActorLocation(), DesiredLocation) < CellSize)
					{
						continue; // Don't teleport if already here
					}

					AIPlayerController* OtherIPlayerController = Cast<AIPlayerController>(OtherPawn->GetController());
					if (OtherIPlayerController != nullptr)
					{
						TeleportLocation(OtherIPlayerController, DesiredLocation, Instance, true);
					}
				}
			}
		}
	}
}

void AIChatCommandManager::TeleportAllPoi(AActor* Poi)
{
	check(Poi);
	if (!Poi)
	{
		return;
	}
	FVector TargetLocation;

	if (AIChatCommandManager::Get(Poi)->GetLocationFromPoi(Poi, TargetLocation, true))
	{
		TeleportAllLocation(Poi->GetWorld(), TargetLocation);
	}
}

void AIChatCommandManager::MessageAllPlayers(UObject* WorldContextObject, FText Message)
{
	if (AIGameState* IGameState = UIGameplayStatics::GetIGameState(WorldContextObject))
	{
		FGameChatMessage ChatMessage;
		ChatMessage.Channel = EChatChannel::Global;
		ChatMessage.Message = Message.ToString();

		int32 PlayerCount = IGameState->PlayerArray.Num();
		for (int32 i = 0; i < PlayerCount; i++)
		{
			if (AIPlayerState* OtherIPlayerState = Cast<AIPlayerState>(IGameState->PlayerArray[i]))
			{
				APawn* Pawn = OtherIPlayerState->GetPawn();
				if (Pawn != nullptr)
				{
					AIPlayerController* IPlayerController = Cast<AIPlayerController>(Pawn->GetController());
					if (IPlayerController != nullptr)
					{
						IPlayerController->ClientRecieveChatMessage(ChatMessage);
					}
				}
			}
		}
	}
}

FChatCommandResponse AIChatCommandManager::NativeProcessStandaloneChatCommand(AAlderonPlayerController* CallingPlayer, const FString& Command, FAsyncChatCommandCallback ExistingCallback /* = FAsyncChatCommandCallback() */)
{
	TArray<FString> Params;
	Command.ParseIntoArrayWS(Params);

	if (Params.Num() == 0)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdNoParams"));
	}

	FString CommandName = Params[0].ToLower();
	bool bExcludeWebhook = CommandName == TEXT("w") || CommandName == TEXT("whisper");

	AAlderonPlayerState* IPlayerState = nullptr;
	if (CallingPlayer)
	{
		IPlayerState = CallingPlayer->GetPlayerState<AAlderonPlayerState>();
	}

	// Webhook for Standalone Commands
	if (AIGameSession::UseWebHooks(WEBHOOK_AdminCommand) && !bExcludeWebhook)
	{

		if (IPlayerState)
		{
			TMap<FString, TSharedPtr<FJsonValue>> WebHookProperties{
				{ TEXT("AdminName"), MakeShareable(new FJsonValueString(IPlayerState->GetPlayerName())) },
				{ TEXT("AdminAlderonId"), MakeShareable(new FJsonValueString(IPlayerState->GetAlderonID().ToDisplayString())) },
				{ TEXT("Role"), MakeShareable(new FJsonValueString(IPlayerState->GetPlayerRole().Name)) },
				{ TEXT("Command"), MakeShareable(new FJsonValueString(Command)) }
			};
			AIGameSession::TriggerWebHookFromContext(this, WEBHOOK_AdminCommand, WebHookProperties);
		}
		else
		{
			TMap<FString, TSharedPtr<FJsonValue>> WebHookProperties{
				{ TEXT("AdminName"), MakeShareable(new FJsonValueString(TEXT("Remotely executed"))) },
				{ TEXT("Command"), MakeShareable(new FJsonValueString(Command)) },
			};
			AIGameSession::TriggerWebHookFromContext(this, WEBHOOK_AdminCommand, WebHookProperties);
		}
	}

	return Super::NativeProcessStandaloneChatCommand(CallingPlayer, Command, ExistingCallback);
}

FChatCommandResponse AIChatCommandManager::ProcessChatCommand(AAlderonPlayerController* CallingPlayer, const FString& Command)
{
	TArray<FString> Params;
	Command.ParseIntoArrayWS(Params);

	if (Params.Num() == 0)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdNoParams"));
	}

	FString CommandName = Params[0].ToLower();
	bool bExcludeWebhook = CommandName == TEXT("w") || CommandName == TEXT("whisper");

	AAlderonPlayerState* IPlayerState = nullptr;
	if (CallingPlayer)
	{
		IPlayerState = CallingPlayer->GetPlayerState<AAlderonPlayerState>();
	}

	// Webhook for Server Commands
	if (AIGameSession::UseWebHooks(WEBHOOK_AdminCommand) && !bExcludeWebhook && IPlayerState)
	{
		TMap<FString, TSharedPtr<FJsonValue>> WebHookProperties{
			{ TEXT("AdminName"), MakeShareable(new FJsonValueString(IPlayerState->GetPlayerName())) },
			{ TEXT("AdminAlderonId"), MakeShareable(new FJsonValueString(IPlayerState->GetAlderonID().ToDisplayString())) },
			{ TEXT("Role"), MakeShareable(new FJsonValueString(IPlayerState->GetPlayerRole().Name)) },
			{ TEXT("Command"), MakeShareable(new FJsonValueString(Command)) },
		};
		AIGameSession::TriggerWebHookFromContext(CallingPlayer, WEBHOOK_AdminCommand, WebHookProperties);
	}

	return Super::ProcessChatCommand(CallingPlayer, Command);
}

FChatCommandResponse AIChatCommandManager::AnnounceToCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	if (!GetWorld())
	{
		return GetResponseCmdNullObject(TEXT("GetWorld()"));
	}
	if (Params.Num() <= 2)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	FString Username = Params[1];

	FString Announcement;
	for (int32 i = 2; i < Params.Num(); i++)
	{
		Announcement.Append(Params[i]);
		Announcement.AppendChar(' ');
	}

	AIPlayerState* IPlayerState = Cast<AIPlayerState>(PlayerStateFromUsername(CallingPlayer, Username));
	if (IPlayerState == nullptr)
	{
		return GetResponseCmdInvalidUsername(Username);
	}

	CallingPlayer->ClientRequestToSendAnnouncement(Announcement, IPlayerState, false);

	const FFormatNamedArguments Arguments{
		{ TEXT("Username"), FText::FromString(Username) }
	};

	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdAnnounceToSuccess"), Arguments);
}

FChatCommandResponse AIChatCommandManager::AnnounceCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	UIGameInstance* GameInstance = UIGameplayStatics::GetIGameInstance(this);
	if (GameInstance == nullptr)
	{
		return GetResponseCmdNullObject(TEXT("GameInstance"));
	}
	AIGameSession* Session = GameInstance->GetGameSession();
	if (Session == nullptr)
	{
		return GetResponseCmdNullObject(TEXT("Session"));
	}

	// Globally and server muted players can't make announcements
	if (IsValid(CallingPlayer) && (CallingPlayer->ShowGlobalMuteError(EChatChannel::System) || CallingPlayer->ShowLocalMuteError(EChatChannel::System)))
	{
		return FChatCommandResponse();
	}

	FString Announcement = TEXT("");
	for (int32 i = 1; i < Params.Num(); i++)
	{
		Announcement.Append(Params[i]);
		Announcement.AppendChar(' ');
	}

	AIPlayerState* CallingPlayerState = IsValid(CallingPlayer) ? CallingPlayer->GetPlayerState<AIPlayerState>() : nullptr;

	AIPlayerController* IPlayerController = nullptr;
	// Send HUD notice to all clients
	for (FConstPlayerControllerIterator Iterator = GetWorld()->GetPlayerControllerIterator(); Iterator; ++Iterator)
	{
		IPlayerController = Cast<AIPlayerController>(*Iterator);
		if (!IsValid(IPlayerController))
		{
			continue;
		}

		IPlayerController->ClientRecieveAnnouncement(Announcement, CallingPlayerState);
	}

	return FChatCommandResponse();
}

FChatCommandResponse AIChatCommandManager::AnnounceRCONCommand(TArray<FString> Params)
{
	return AnnounceCommand(nullptr, Params);
}

FChatCommandResponse AIChatCommandManager::UnbanCommand(TArray<FString> Params)
{
	if (!GetWorld())
	{
		return GetResponseCmdNullObject(TEXT("GetWorld()"));
	}
	if (Params.Num() <= 1)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	UIGameInstance* GameInstance = UIGameplayStatics::GetIGameInstance(this);
	check(GameInstance);
	if (!GameInstance)
	{
		return FChatCommandResponse();
	}
	AIGameSession* Session = GameInstance->GetGameSession();
	check(Session);
	if (!Session)
	{
		return FChatCommandResponse();
	}

	FString Username = Params[1];

	const FFormatNamedArguments Arguments{
		{ TEXT("BanString"), FText::FromString(Username) } 
	};

	if (!IsValidAlderonId(Username))
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdUnbanFail"), Arguments);
	}

	if (Session->UnbanId(Username))
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdUnban"), Arguments);
	}
	else
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdUnbanFail"), Arguments);
	}
}

FChatCommandResponse AIChatCommandManager::BanCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	if (Params.Num() <= 1)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}
	FString Username = Params[1];

	int32 Hierarchy = GetRoleHierarchyForAGID(Username);

	if (GetRoleHierarchyForPlayer(CallingPlayer) <= Hierarchy)
	{
#if !WITH_EDITOR
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdInsufficientRoleHierarchy"));
#endif
	}

	if (CallingPlayer)
	{
		const AAlderonPlayerState* const TargetPlayerState = PlayerStateFromUsername(CallingPlayer, Username);
		const AAlderonPlayerState* const CallingPlayerState = CallingPlayer->GetPlayerState<AAlderonPlayerState>();
		if (CallingPlayerState && TargetPlayerState)
		{
			const FString CallingPlayerName = CallingPlayerState->GetPlayerName();
			const FString CallingPlayerID = CallingPlayerState->GetAlderonID().ToDisplayString();
			const FString TargetID = TargetPlayerState->GetAlderonID().ToDisplayString();
			UE_LOG(TitansLog, Warning, TEXT("%s (%s) is Banning player %s (%s)"), *CallingPlayerName, *CallingPlayerID, *Username, *TargetID);
		}
	}

	return BanRCONCommand(Params);
}

FChatCommandResponse AIChatCommandManager::KickCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	if (Params.Num() <= 1)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}
	FString Username = Params[1];

	int32 Hierarchy = GetRoleHierarchyForAGID(Username);

	if (GetRoleHierarchyForPlayer(CallingPlayer) <= Hierarchy)
	{
#if !WITH_EDITOR
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdInsufficientRoleHierarchy"));
#endif
	}

	if (CallingPlayer)
	{
		const AAlderonPlayerState* const TargetPlayerState = PlayerStateFromUsername(CallingPlayer, Username);
		const AAlderonPlayerState* const CallingPlayerState = CallingPlayer->GetPlayerState<AAlderonPlayerState>();
		if (CallingPlayerState && TargetPlayerState)
		{
			const FString CallingPlayerName = CallingPlayerState->GetPlayerName();
			const FString CallingPlayerID = CallingPlayerState->GetAlderonID().ToDisplayString();
			const FString TargetID = TargetPlayerState->GetAlderonID().ToDisplayString();
			UE_LOG(TitansLog, Warning, TEXT("%s (%s) is Kicking player %s (%s)"), *CallingPlayerName, *CallingPlayerID, *Username, *TargetID);
		}
	}

	return KickRCONCommand(Params);
}

FChatCommandResponse AIChatCommandManager::BanRCONCommand(TArray<FString> Params)
{
	FPlayerBan PlayerBan = FPlayerBan();

	FString PlayerString = TEXT("");
	FString TimeString = TEXT("");

	if (!GetWorld())
	{
		return GetResponseCmdNullObject(TEXT("GetWorld()"));
	}
	if (Params.Num() <= 1)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	if (Params.IsValidIndex(1))
	{
		PlayerString = Params[1];
	}
	if (Params.IsValidIndex(2))
	{
		if (Params[2] != TEXT("0"))
		{
			TimeString = Params[2];
		}
	}
	if (Params.IsValidIndex(3))
	{
		PlayerBan.AdminReason = Params[3];
	}
	if (Params.IsValidIndex(4))
	{
		PlayerBan.UserReason = Params[4];
	}

	AIGameSession* IGameSession = UIGameplayStatics::GetIGameSessionChecked(this);
	if (!IGameSession)
	{
		return GetResponseCmdNullObject(TEXT("IGameSession"));
	}

	if (AIPlayerState* IPlayerState = Cast<AIPlayerState>(PlayerStateFromUsername(this, PlayerString)))
	{
		if (IPlayerState->IsServerAdmin())
		{
			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdBanCannotBanAdmin"));
		}
		if (IPlayerState->IsGameDev())
		{
			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdBanCannotBanDev"));
		}
		PlayerBan.PlayerId = IPlayerState->GetAlderonID();
	}
	else if (IsValidAlderonId(PlayerString))
	{
		if (IGameSession->IsAdminID(PlayerString))
		{
			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdBanCannotBanAdmin"));
		}
		if (IGameSession->IsDevID(PlayerString))
		{
			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdBanCannotBanDev"));
		}

		PlayerBan.PlayerId = FAlderonPlayerID(PlayerString);
	}
	else
	{
		return GetResponseCmdInvalidUsername(PlayerString);
	}

	int32 TimeSeconds = 0;
	if (!TimeString.IsEmpty())
	{
		if (TimeStringToSeconds(TimeString, TimeSeconds))
		{
			PlayerBan.BanExpiration = FDateTime::UtcNow().ToUnixTimestamp() + TimeSeconds;
		}
		else
		{
			const FFormatNamedArguments Arguments{
				{ TEXT("Time"), FText::FromString(TimeString) }
			};

			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdServerMuteInvalidTime"), Arguments);
		}
	}

	if (IGameSession->IsPlayerBanned(PlayerBan.PlayerId))
	{
		const FFormatNamedArguments Arguments{ 
			{ TEXT("BanString"), FText::FromString(PlayerString) }
		};

		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdBanOfflinePlayerFail"), Arguments);
	}

	IGameSession->AppendBan(PlayerBan);

	if (TimeString.IsEmpty())
	{
		const FFormatNamedArguments Arguments{
			{ TEXT("Username"), FText::FromString(PlayerString) },
			{ TEXT("AdminReason"), FText::FromString(PlayerBan.AdminReason) },
			{ TEXT("UserReason"), FText::FromString(PlayerBan.UserReason) }
		};

		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdBanSuccess"), Arguments);
	}
	else
	{
		const FFormatNamedArguments Arguments{
			{ TEXT("Time"), FText::FromString(TimeString) },
			{ TEXT("Username"), FText::FromString(PlayerString) },
			{ TEXT("AdminReason"), FText::FromString(PlayerBan.AdminReason) },
			{ TEXT("UserReason"), FText::FromString(PlayerBan.UserReason) }
		};

		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdBanSuccessTime"), Arguments);
	}
}
FChatCommandResponse AIChatCommandManager::KickRCONCommand(TArray<FString> Params)
{
	if (!GetWorld())
	{
		return GetResponseCmdNullObject(TEXT("GetWorld()"));
	}
	if (Params.Num() <= 1)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	FString Username = Params[1];
	FString Reason;
	for (int32 i = 2; i < Params.Num(); i++)
	{
		Reason.Append(Params[i]);
		Reason.AppendChar(' ');
	}
	Reason.LeftChopInline(1);

	AIPlayerState* IPlayerState = Cast<AIPlayerState>(PlayerStateFromUsername(this, Username));
	if (IPlayerState)
	{
		if (IPlayerState->GetPlayerName() == Username)
		{
			AdminRequestAction(IPlayerState->GetAlderonID().ToString(), false, false, Reason);
			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdBanRequestAction"));
		}
		else if (IPlayerState->GetAlderonID().ToDisplayString() == Username)
		{
			AdminRequestAction(IPlayerState->GetAlderonID().ToString(), false, false, Reason);
			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdBanRequestActionID"));
		}
	}

	const FFormatNamedArguments Arguments{ 
		{ TEXT("KickString"), FText::FromString(Username) }
	};

	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdKickFail"), Arguments);
}

FChatCommandResponse AIChatCommandManager::SaveCommand(TArray<FString> Params)
{
	if (!GetWorld())
	{
		return GetResponseCmdNullObject(TEXT("GetWorld()"));
	}
	AIGameMode* IGameMode = Cast<AIGameMode>(UGameplayStatics::GetGameMode(GetWorld()));
	if (IGameMode == nullptr)
	{
		return GetResponseCmdNullObject(TEXT("IGameMode"));
	}

	if (AIWorldSettings* IWorldSettings = AIWorldSettings::GetWorldSettings(this))
	{
		if (AIWaterManager* WaterManager = IWorldSettings->WaterManager)
		{
			WaterManager->SaveWaterData();
		}

		if (AIWaystoneManager* WaystoneManager = IWorldSettings->WaystoneManager)
		{
			WaystoneManager->SaveWaystoneData();
		}
	}

	IGameMode->SaveAllPlayers();
	IGameMode->SaveAllNests();

	MessageAllPlayers(GetWorld(), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdSaveSuccess")));

	return FChatCommandResponse();
}

FChatCommandResponse AIChatCommandManager::LoadCommand(TArray<FString> Params)
{
	if (!GetWorld())
	{
		return GetResponseCmdNullObject(TEXT("GetWorld()"));
	}
	AIGameMode* IGameMode = Cast<AIGameMode>(UGameplayStatics::GetGameMode(this));
	if (IGameMode == nullptr)
	{
		return GetResponseCmdNullObject(TEXT("IGameMode"));
	}

	if (AIWorldSettings* IWorldSettings = AIWorldSettings::GetWorldSettings(this))
	{
		if (AIWaterManager* WaterManager = IWorldSettings->WaterManager)
		{
			WaterManager->LoadWaterData();
			MessageAllPlayers(GetWorld(), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdLoadWaterSuccess")));
		}

		if (AIWaystoneManager* WaystoneManager = IWorldSettings->WaystoneManager)
		{
			WaystoneManager->LoadWaystoneData();
			MessageAllPlayers(GetWorld(), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdLoadWaystoneSuccess")));
		}
	}

	MessageAllPlayers(GetWorld(), FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdLoadSuccess")));

	return FChatCommandResponse();
}

FChatCommandResponse AIChatCommandManager::TeleportAllCommand(TArray<FString> Params)
{
	if (!GetWorld())
	{
		return GetResponseCmdNullObject(TEXT("GetWorld()"));
	}
	if (Params.Num() != 2)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	FString LocationString = Params[1];

	FVector TeleportCoordinate;
	if (LocationString.Contains(TEXT("(X="), ESearchCase::IgnoreCase, ESearchDir::FromStart))
	{
		if (TeleportCoordinate.InitFromString(LocationString))
		{
			TeleportAllLocation(this, TeleportCoordinate);
			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdTeleportAllSuccess"));
		}
		else
		{
			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdTeleportAllInvalidCoordinate"));
		}
	}

	AActor* Poi = GetPoi(this, LocationString);
	if (Poi != nullptr)
	{
		TeleportAllPoi(Poi);
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdTeleportAllSuccess"));
	}

	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdTeleportAllInvalidPoi"));
}

FChatCommandResponse AIChatCommandManager::HealAllCommand(TArray<FString> Params)
{
	const AIGameState* const IGameState = UIGameplayStatics::GetIGameState(GetWorld());
	if (!IGameState)
	{
		return GetResponseCmdNullObject(TEXT("IGameState"));
	}

	if (Params.Num() != 1)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	int32 HealedPlayers = 0;

	for (TObjectPtr<APlayerState> PlayerState : IGameState->PlayerArray)
	{
		AIBaseCharacter* const IPlayerPawn = PlayerState->GetPawn<AIBaseCharacter>();
		if (!IPlayerPawn || IPlayerPawn->GetIsDying())
		{
			continue;
		}

		HealedPlayers++;
		IPlayerPawn->ResetStats();
		IPlayerPawn->GetDamageWounds_Mutable().Reset();
	}

	const FFormatNamedArguments Arguments{
		{ TEXT("PlayerCount"), HealedPlayers }
	};

	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdHealAll"), Arguments);
}

FChatCommandResponse AIChatCommandManager::HealRCONCommand(TArray<FString> Params)
{
	if (Params.Num() != 2)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}
	const FString Username = Params[1];
	AIPlayerController* const IPlayerController = Cast<AIPlayerController>(PlayerControllerFromUsername(this, Username));

	if (!IPlayerController)
	{
		return GetResponseCmdInvalidUsername(Username);
	}

	const FFormatNamedArguments Arguments{ 
		{ TEXT("Username"), FText::FromString(Username) } 
	};

	AIBaseCharacter* const IBaseCharacter = IPlayerController->GetPawn<AIBaseCharacter>();
	if (!IBaseCharacter || IBaseCharacter->GetIsDying())
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdNoPawn"));
	}

	IBaseCharacter->ResetStats();
	IBaseCharacter->GetDamageWounds_Mutable().Reset();

	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdHealSuccessOther"), Arguments);
}

FChatCommandResponse AIChatCommandManager::HealPlayerCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	AIPlayerController* TargetPlayerController = CallingPlayer;
	FString Username = TEXT("");
	if (Params.Num() > 2)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	if (Params.Num() == 2)
	{
		Username = Params[1];
		AIPlayerController* const OtherPlayerController = Cast<AIPlayerController>(PlayerControllerFromUsername(this, Username));
		if (OtherPlayerController != nullptr)
		{
			TargetPlayerController = OtherPlayerController;
		}
	}

	AIBaseCharacter* const IBaseCharacter = TargetPlayerController->GetPawn<AIBaseCharacter>();
	if (!IBaseCharacter || IBaseCharacter->GetIsDying())
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdNoPawn"));
	}

	IBaseCharacter->ResetStats();
	IBaseCharacter->GetDamageWounds_Mutable().Reset();

	if (TargetPlayerController == CallingPlayer)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdHealSuccess"));
	}
	else
	{
		const FFormatNamedArguments Arguments{
			{ TEXT("Username"), FText::FromString(Username) }
		};
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdHealSuccessOther"), Arguments);
	}
}

FChatCommandResponse AIChatCommandManager::PromoteRCONCommand(TArray<FString> Params)
{
	if (Params.Num() < 3)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	FString RoleName = TEXT("");
	RoleName.Reserve(64);

	for (int32 i = 2; i < Params.Num(); i++)
	{
		RoleName.Append(FString::Printf(TEXT("%s "), *Params[i]));
	}
	RoleName.TrimEndInline();

	FPlayerRole PlayerRole{};
	if (!GetPlayerRoleByName(RoleName, PlayerRole))
	{
		const FFormatNamedArguments Arguments{
			{ TEXT("RoleName"), FText::FromString(RoleName) }
		};
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdPromoteInvalidRole"), Arguments);
	}

	const FString Username = Params[1];

	const bool bIsAlderonId = IsValidAlderonId(Username);

	const FString Section = TEXT("PlayerRoles");
	FString Key = Username;

	if (!bIsAlderonId)
	{
		AIPlayerController* const IPlayerController = Cast<AIPlayerController>(PlayerControllerFromUsername(this, Username));

		if (IPlayerController == nullptr)
		{
			const FFormatNamedArguments Arguments{
				{ TEXT("Username"), FText::FromString(Username) }
			};
			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdPromoteInvalidUsername"), Arguments);
		}

		AIPlayerState* const IPlayerState = IPlayerController->GetPlayerState<AIPlayerState>();

		if (ensureAlways(IPlayerState))
		{
			IPlayerState->SetPlayerRole(PlayerRole);
			Key = IPlayerState->GetAlderonID().ToDisplayString();
		}
	}

	GConfig->SetString(
		*Section,
		*Key,
		*RoleName,
		ConfigPath);

	GConfig->Flush(false, ConfigPath);

	const FFormatNamedArguments Arguments{
		{ TEXT("Username"), FText::FromString(Username) },
		{ TEXT("RoleName"), FText::FromString(PlayerRole.Name) }
	};
	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdPromoteSuccess"), Arguments);
}

FChatCommandResponse AIChatCommandManager::PromoteCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	if (Params.Num() < 3)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}
	check(CallingPlayer);
	if (!CallingPlayer)
	{
		return GetResponseCmdNullObject(TEXT("CallingPlayer"));
	}

	FString RoleName = TEXT("");
	for (int32 i = 2; i < Params.Num(); i++)
	{
		RoleName.Append(Params[i]);

		RoleName.AppendChar(' ');
	}
	RoleName.TrimEndInline();
	FString Username = Params[1];
	AIPlayerController* IPlayerController = Cast<AIPlayerController>(PlayerControllerFromUsername(this, Username));
	if (IPlayerController == CallingPlayer && !CheckAdmin(CallingPlayer))
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdPromoteCantChangeSelf"));
	}

	if (GetRoleHierarchyForPlayer(CallingPlayer) < GetRoleHierarchyForPlayer(IPlayerController))
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdInsufficientRoleHierarchy"));
	}

	FPlayerRole PlayerRole;
	if (GetPlayerRoleByName(RoleName, PlayerRole))
	{
		if (GetRoleHierarchyForPlayer(CallingPlayer) < PlayerRole.Hierarchy)
		{
			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdPromoteInsufficientRoleHierarchy"));
		}
	}

	return PromoteRCONCommand(Params);
}

FChatCommandResponse AIChatCommandManager::DemoteRCONCommand(TArray<FString> Params)
{
	if (Params.Num() != 2)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	const FString Username = Params[1];

	const bool bIsAlderonId = IsValidAlderonId(Username);

	const FString Section = TEXT("PlayerRoles");
	FString Key = Username;

	if (!bIsAlderonId)
	{
		AIPlayerController* const IPlayerController = Cast<AIPlayerController>(PlayerControllerFromUsername(this, Username));

		if (IPlayerController == nullptr)
		{
			const FFormatNamedArguments Arguments{
				{ TEXT("Username"), FText::FromString(Username) }
			};

			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdPromoteInvalidUsername"), Arguments);
		}

		AIPlayerState* const IPlayerState = IPlayerController->GetPlayerState<AIPlayerState>();

		if (ensureAlways(IPlayerState))
		{
			IPlayerState->SetPlayerRole(FPlayerRole());
			Key = IPlayerState->GetAlderonID().ToDisplayString();
		}
	}

	GConfig->RemoveKey(
		*Section,
		*Key,
		ConfigPath);

	GConfig->Flush(false, ConfigPath);

	const FFormatNamedArguments Arguments{
		{ TEXT("Username"), FText::FromString(Username) }
	};

	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdDemoteSuccess"), Arguments);
}

FChatCommandResponse AIChatCommandManager::DemoteCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	check(CallingPlayer);
	if (!CallingPlayer)
	{
		return GetResponseCmdNullObject(TEXT("CallingPlayer"));
	}

	if (!ensure(Params.IsValidIndex(1)))
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	FString Username = Params[1];
	AIPlayerController* IPlayerController = Cast<AIPlayerController>(PlayerControllerFromUsername(this, Username));
	if (IPlayerController == CallingPlayer && !CheckAdmin(CallingPlayer))
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdDemoteCantChangeSelf"));
	}

	if (GetRoleHierarchyForPlayer(CallingPlayer) < GetRoleHierarchyForPlayer(IPlayerController))
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdInsufficientRoleHierarchy"));
	}

	return DemoteRCONCommand(Params);
}

FChatCommandResponse AIChatCommandManager::GiveQuestRCONCommand(TArray<FString> Params, FAsyncChatCommandCallback& Callback)
{
	if (Params.Num() <= 2)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}
	const FString Username = Params[1];
	AIPlayerController* const IPlayerController = Cast<AIPlayerController>(PlayerControllerFromUsername(this, Username));
	if (IPlayerController == nullptr)
	{
		return GetResponseCmdInvalidUsername(Username);
	}

	int32 ParamsStart = 2;

	AIQuestManager* const QuestManager = AIWorldSettings::GetWorldSettings(this)->QuestManager;
	if (!QuestManager)
	{
		return GetResponseCmdNullObject(TEXT("QuestManager"));
	}

	FString QuestNameString;
	for (int32 i = ParamsStart; i < Params.Num(); i++)
	{
		QuestNameString.Append(Params[i]);
		QuestNameString.AppendChar(' ');
	}

	AIBaseCharacter* const IBaseCharacter = Cast<AIBaseCharacter>(IPlayerController->GetCharacter());
	if (IBaseCharacter == nullptr)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdGiveQuestInvalidBaseCharacter"));
	}

	FQuestIDLoaded LoadAssetDelegate;
	TWeakObjectPtr<AIChatCommandManager> WeakThis = MakeWeakObjectPtr(this);
	TWeakObjectPtr<AIBaseCharacter> WeakIBaseCharacter = MakeWeakObjectPtr(IBaseCharacter);
	LoadAssetDelegate.BindLambda([WeakThis, QuestNameString, WeakIBaseCharacter, Callback](FPrimaryAssetId QuestAssetId) {
		if (WeakThis.IsValid() && WeakIBaseCharacter.IsValid())
		{
			WeakThis->OnGiveQuestRCONCommand(QuestAssetId, QuestNameString, WeakIBaseCharacter.Get(), Callback);
		}
	});

	QuestManager->GetQuestByName(QuestNameString, LoadAssetDelegate, IBaseCharacter);

	const FFormatNamedArguments Arguments{
		{ TEXT("QuestName"), FText::FromString(QuestNameString) }
	};

	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdQuestAssetLoading"), Arguments);
}

void AIChatCommandManager::OnGiveQuestRCONCommand(FPrimaryAssetId QuestAssetId, FString QuestNameString, AIBaseCharacter* const IBaseCharacter, FAsyncChatCommandCallback Callback)
{
	const FFormatNamedArguments Arguments{ 
		{ TEXT("QuestName"), FText::FromString(QuestNameString) }
	};

	if (!QuestAssetId.IsValid())
	{
		Callback.ExecuteIfBound(AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdGiveQuestAssetNotValid"), Arguments).LocalizedText);
		return;
	}

	for (const UIQuest* CharacterQuest : IBaseCharacter->GetActiveQuests())
	{
		if (!CharacterQuest || QuestAssetId != CharacterQuest->GetQuestId())
		{
			continue;
		}

		Callback.ExecuteIfBound(AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdGiveQuestDuplicate"), Arguments).LocalizedText);
		return;
	}

	AIQuestManager* const QuestManager = AIWorldSettings::GetWorldSettings(this)->QuestManager;
	if (!QuestManager)
	{
		const FFormatNamedArguments Args{
			{ TEXT("Object"), FText::FromString(TEXT("QuestManager")) }
		};
		Callback.ExecuteIfBound(AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdNullObject"), Args).LocalizedText);
		return;
	}

	QuestManager->AssignQuest(QuestAssetId, IBaseCharacter, true);
	Callback.ExecuteIfBound(AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdGiveQuestSuccess")).LocalizedText);
}

FChatCommandResponse AIChatCommandManager::GiveQuestCommand(AIPlayerController* CallingPlayer, TArray<FString> Params, FAsyncChatCommandCallback& Callback)
{
	if (!Params.IsValidIndex(1))
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	if (!CallingPlayer)
	{
		return GetResponseCmdNullObject(TEXT("CallingPlayer"));
	}

	if (PlayerControllerFromUsername(CallingPlayer, Params[1]))
	{
		return GiveQuestRCONCommand(Params, Callback);
	}

	const AIPlayerState* const IPlayerState = CallingPlayer->GetPlayerState<AIPlayerState>();
	if (!IPlayerState)
	{
		return GetResponseCmdNullObject(TEXT("IPlayerState"));
	}

	TArray<FString> RconParams{ Params[0], IPlayerState->GetAlderonID().ToDisplayString() };
	RconParams.Reserve(Params.Num() + 1);

	Params.RemoveAt(0);
	RconParams.Append(Params);

	return GiveQuestRCONCommand(MoveTemp(RconParams), Callback);
}

FChatCommandResponse AIChatCommandManager::ListQuestsCommand(TArray<FString> Params, FAsyncChatCommandCallback& Callback)
{
	AIQuestManager* QuestManager = AIWorldSettings::GetWorldSettings(GetWorld())->QuestManager;

	if (!QuestManager)
	{
		return GetResponseCmdNullObject(TEXT("QuestManager"));
	}

	FQuestsIDsLoaded LoadAssetIDsDelegate;
	TWeakObjectPtr<AIChatCommandManager> WeakThis = MakeWeakObjectPtr(this);
	LoadAssetIDsDelegate.BindLambda([WeakThis, Callback](TArray<FPrimaryAssetId> QuestAssetIds) {
		if (WeakThis.IsValid())
		{
			WeakThis->OnListQuestsCommand(QuestAssetIds, Callback);
		}
	});

	QuestManager->GetLoadedQuestAssetIDsFromType(EQuestFilter::ALL, nullptr, LoadAssetIDsDelegate);

	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdListQuestsAssetsLoading"));
}

void AIChatCommandManager::OnListQuestsCommand(TArray<FPrimaryAssetId> QuestAssetIds, FAsyncChatCommandCallback Callback)
{
	AIQuestManager* QuestManager = AIWorldSettings::GetWorldSettings(GetWorld())->QuestManager;

	if (!QuestManager)
	{
		const FFormatNamedArguments Args{
			{ TEXT("Object"), FText::FromString(TEXT("QuestManager")) }
		};

		Callback.ExecuteIfBound(AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdNullObject"), Args).LocalizedText);
		return;
	}

	FString String = TEXT("");

	for (int32 i = 0; i < QuestAssetIds.Num(); i++)
	{
		FText QuestName;
		if (QuestManager->GetQuestDisplayName(QuestAssetIds[i], QuestName))
		{
			String.Append(QuestName.ToString());
			String.Append(TEXT("\n"));
		}
	}

	Callback.ExecuteIfBound(AIChatCommand::MakePlainResponse(String).LocalizedText);
}

FChatCommandResponse AIChatCommandManager::CompleteQuestCommand(AIPlayerController* CallingPlayer, TArray<FString> Params, FAsyncChatCommandCallback& Callback)
{
	if (Params.Num() < 1)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	AIQuestManager* QuestManager = AIWorldSettings::GetWorldSettings(CallingPlayer->GetWorld())->QuestManager;

	if (!QuestManager)
	{
		return GetResponseCmdNullObject(TEXT("QuestManager"));
	}

	AIPlayerController* IPlayerController = CallingPlayer;
	FString QuestNameString;

	if (Params.Num() > 1)
	{
		int32 ParamsStart = 1;
		FString Username = Params[1];
		AIPlayerController* FoundIPlayerController = Cast<AIPlayerController>(PlayerControllerFromUsername(this, Username));
		if (FoundIPlayerController == nullptr)
		{
			MessagePlayer(CallingPlayer, AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdCompleteQuestSelf")));
		}
		else
		{
			IPlayerController = FoundIPlayerController;
			const FFormatNamedArguments Arguments{ 
				{ TEXT("Username"), FText::FromString(Username) }
			};

			MessagePlayer(CallingPlayer, AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdCompleteQuest"), Arguments));

			ParamsStart = 2;
		}

		for (int32 i = ParamsStart; i < Params.Num(); i++)
		{
			QuestNameString.Append(Params[i]);
			QuestNameString.AppendChar(' ');
		}
	}

	AIBaseCharacter* IBaseCharacter = Cast<AIBaseCharacter>(IPlayerController->GetCharacter());
	if (IBaseCharacter == nullptr)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdGiveQuestInvalidBaseCharacter"));
	}

	FQuestIDLoaded LoadAssetDelegate;
	TWeakObjectPtr<AIChatCommandManager> WeakThis = MakeWeakObjectPtr(this);
	TWeakObjectPtr<AIBaseCharacter> WeakIBaseCharacter = MakeWeakObjectPtr(IBaseCharacter);
	TWeakObjectPtr<AIPlayerController> WeakCallingPlayer = MakeWeakObjectPtr(CallingPlayer);
	LoadAssetDelegate.BindLambda([WeakThis, WeakCallingPlayer, WeakIBaseCharacter, Callback](FPrimaryAssetId QuestAssetId) {
		if (WeakThis.IsValid() && WeakIBaseCharacter.IsValid() && WeakCallingPlayer.IsValid())
		{
			WeakThis->OnCompleteQuestCommand(QuestAssetId, WeakCallingPlayer.Get(), WeakIBaseCharacter.Get(), Callback);
		}
	});

	QuestManager->GetQuestByName(QuestNameString, LoadAssetDelegate, IBaseCharacter);

	const FFormatNamedArguments Arguments{
		{ TEXT("QuestName"), FText::FromString(QuestNameString) }
	};

	if (QuestNameString.IsEmpty())
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdCompleteAllQuests"));
	}

	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdQuestAssetLoading"), Arguments);
}

void AIChatCommandManager::OnCompleteQuestCommand(FPrimaryAssetId QuestAssetId, AIPlayerController* CallingPlayer, AIBaseCharacter* const IBaseCharacter, FAsyncChatCommandCallback Callback)
{
	const TArray<UIQuest*>& ActiveQuests = IBaseCharacter->GetActiveQuests();
	AIQuestManager* QuestManager = AIWorldSettings::GetWorldSettings(CallingPlayer->GetWorld())->QuestManager;

	if (!QuestManager)
	{
		const FFormatNamedArguments Args{ { TEXT("Object"), FText::FromString(TEXT("QuestManager")) } };

		Callback.ExecuteIfBound(AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdNullObject"), Args).LocalizedText);
		return;
	}

	for (int32 Index = ActiveQuests.Num() - 1; Index >= 0; --Index)
	{
		UIQuest* const CharacterQuest = ActiveQuests[Index];
		if (!CharacterQuest)
		{
			continue;
		}

		if (!QuestAssetId.IsValid() || QuestAssetId == CharacterQuest->GetQuestId())
		{
			QuestManager->OnQuestCompleted(IBaseCharacter, CharacterQuest);
		}
	}

	Callback.ExecuteIfBound(AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdCompleteQuestSuccess")).LocalizedText);
}

FChatCommandResponse AIChatCommandManager::ClearChatLocalCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	AIGameHUD* IGameHUD = Cast<AIGameHUD>(UIGameplayStatics::GetIHUD(CallingPlayer->GetWorld()));
	if (!IGameHUD)
	{
		return GetResponseCmdNullObject(TEXT("IGameHUD"));
	}

	UIMainGameHUDWidget* MainHUDWidget = Cast<UIMainGameHUDWidget>(IGameHUD->ActiveHUDWidget);
	if (!MainHUDWidget)
	{
		return GetResponseCmdNullObject(TEXT("MainHUDWidget"));
	}

	UIChatWindow* ChatWindow = MainHUDWidget->ChatWindow;
	if (!ChatWindow)
	{
		return GetResponseCmdNullObject(TEXT("ChatWindow"));
	}

	ChatWindow->ClearCurrentChannel();
	return FChatCommandResponse();
}

FChatCommandResponse AIChatCommandManager::MapBugLocalCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	CallingPlayer->MapBug();
	const FFormatNamedArguments Arguments{
		{ TEXT("Location"), FText::FromString(CallingPlayer->GetMapBug()) }
	};

	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdMapBugSuccess"), Arguments);
}

FChatCommandResponse AIChatCommandManager::MutePlayerLocalCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	if (Params.Num() < 2)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}
	FString ActionStr = Params[0];
	bool bMute = ActionStr.Equals(TEXT("mute"), ESearchCase::IgnoreCase);

	if (!bMute)
	{
		if (!ActionStr.Equals(TEXT("unmute"), ESearchCase::IgnoreCase))
		{
			return FChatCommandResponse();
		}
	}

	FString Username = Params[1];

	// Prevent players from muting / unmuting themselves
	AIPlayerState* LocalPlayerState = CallingPlayer->GetPlayerState<AIPlayerState>();
	if (LocalPlayerState)
	{
		if (LocalPlayerState->GetPlayerName() == Username || LocalPlayerState->GetAlderonID().ToString() == Username || LocalPlayerState->GetAlderonID().ToDisplayString() == Username)
		{
			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdMuteCantSelfMute"));
		}
	}

	if (AIGameState* IGameState = UIGameplayStatics::GetIGameState(CallingPlayer))
	{
		if (IGameState->PlayerArray.Num() > 1)
		{
			for (int32 i = 0; i < IGameState->PlayerArray.Num(); i++)
			{
				if (AIPlayerState* IPlayerState = Cast<AIPlayerState>(IGameState->PlayerArray[i]))
				{
					// Allow commands to work via Username, AlderonId with and without dashes
					if (IPlayerState->GetPlayerName() == Username || IPlayerState->GetAlderonID().ToDisplayString() == Username || IPlayerState->GetAlderonID().ToString() == Username)
					{
						const FFormatNamedArguments Arguments{
							{ TEXT("Username"), FText::FromString(IPlayerState->GetPlayerName()) },
							{ TEXT("AGID"), FText::FromString(IPlayerState->GetAlderonID().ToDisplayString()) }
						};

						if (bMute)
						{
							CallingPlayer->MutePlayerState(IPlayerState);
							return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdMutePlayer"), Arguments);
						}
						else
						{
							CallingPlayer->UnmutePlayerState(IPlayerState);
							return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdUnmutePlayer"), Arguments);
						}
					}
				}
			}
		}
	}
	return GetResponseCmdInvalidUsername(Username);
}

FChatCommandResponse AIChatCommandManager::CancelRestartCommand(TArray<FString> Params)
{
	if (!GetWorld())
	{
		return GetResponseCmdNullObject(TEXT("GetWorld()"));
	}
	AIGameMode* IGameMode = UIGameplayStatics::GetIGameMode(this);
	check(IGameMode);
	if (!IGameMode)
	{
		return FChatCommandResponse();
	}

	IGameMode->StopAutoRestart();

	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdCancelRestartSuccess"));
}

FChatCommandResponse AIChatCommandManager::RestartCommand(TArray<FString> Params)
{
	if (!GetWorld())
	{
		return GetResponseCmdNullObject(TEXT("GetWorld()"));
	}
	AIGameMode* IGameMode = UIGameplayStatics::GetIGameMode(this);
	check(IGameMode);
	if (!IGameMode)
	{
		return FChatCommandResponse();
	}

	float RestartTime = 10.f;
	if (Params.Num() >= 2)
	{
		RestartTime = FCString::Atof(*Params[1]);
	}

	IGameMode->StartRestartTimer(RestartTime);

	const FFormatNamedArguments Arguments{
		{ TEXT("Seconds"), RestartTime }
	};

	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdRestartInSeconds"), Arguments);
}

FChatCommandResponse AIChatCommandManager::CopyStiffBodyLocalCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	CallingPlayer->StiffBody();

	if (CallingPlayer->GetStiffBody() != FString())
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdCopyStiffBodySuccess"));
	}
	else
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdCopyStiffBodyFail"));
	}
}

FChatCommandResponse AIChatCommandManager::FixStiffBodyLocalCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	if (CallingPlayer->GetFixStiffBody() != FString())
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdFixStiffBodySuccess"));
	}
	else
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdFixStiffBodyFail"));
	}

	CallingPlayer->FixStiffBody();
}

FChatCommandResponse AIChatCommandManager::BugSnapLocalCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	FString BugMessageStr = "";
	for (int32 i = 1; i < Params.Num(); i++)
	{
		BugMessageStr.Append(Params[i]);
		BugMessageStr.AppendChar(' ');
	}

	AIGameHUD* IGameHUD = Cast<AIGameHUD>(UIGameplayStatics::GetIHUD(CallingPlayer));
	check(IGameHUD);
	if (IGameHUD)
	{
		IGameHUD->StartBugReport(BugMessageStr);
	}

	return FChatCommandResponse();
}

FChatCommandResponse AIChatCommandManager::DamageParticleLocalCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	CallingPlayer->DamageParticles();

	if (CallingPlayer->GetDamageParticles() != FString())
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdDamageParticleSuccess"));
	}
	else
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdDamageParticleFail"));
	}
}

FChatCommandResponse AIChatCommandManager::FlushLevelStreamingLocalCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	if (UWorld* World = CallingPlayer->GetWorld())
	{
		if (World != nullptr)
		{
			World->FlushLevelStreaming(EFlushLevelStreamingType::Full);
		}
	}
	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdFlushLevelStreaming"));
}

FChatCommandResponse AIChatCommandManager::ToggleIKLocalCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	UIGameInstance* IGameInstance = Cast<UIGameInstance>(CallingPlayer->GetGameInstance());
	if (IGameInstance == nullptr)
	{
		return GetResponseCmdNullObject(TEXT("IGameInstance"));
	}
	IGameInstance->ToggleIK();
	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdToggleIKSuccess"));
}

FChatCommandResponse AIChatCommandManager::DebugAILocalCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	UIGameInstance* IGameInstance = Cast<UIGameInstance>(CallingPlayer->GetGameInstance());
	if (IGameInstance == nullptr)
	{
		return GetResponseCmdNullObject(TEXT("IGameInstance"));
	}
	IGameInstance->ToggleDebugAI();
	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdDebugAISuccess"));
}

FChatCommandResponse AIChatCommandManager::DemoDownloadLocalCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{

	IAlderonCommon& AlderonCommon = IAlderonCommon::Get();
	IAlderonAuth& AlderonAuth = AlderonCommon.GetAuthInterface();
	if (!AlderonAuth.GetAuthInfo().bAdmin) // must be dev
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdUnknownCommand"));
	}

	if (Params.Num() != 3)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	IAlderonDownload& AlderonDownload = IAlderonCommon::Get().GetDownloadInterface();

	TSharedPtr<FDownloadHandle, ESPMode::ThreadSafe> DownloadHandle = AlderonDownload.DownloadFile(Params[1], Params[2], false);
	if (!DownloadHandle.IsValid() || DownloadHandle->bFailed)
	{
		return AIChatCommand::MakePlainResponse(FString::Printf(TEXT("Failed to download from '%s' to '%s'"), *Params[1], *Params[2]));
	}
	TWeakObjectPtr<AIPlayerController> WeakPlayer = CallingPlayer;

	DownloadHandle->OnCompleted.AddLambda([WeakPlayer, this, Params](FDownloadHandle* Download) {
		if (!WeakPlayer.Get())
		{
			return;
		}

		if (!Download)
		{
			MessagePlayer(WeakPlayer.Get(), AIChatCommand::MakePlainResponse(TEXT("Downloaded failed, DownloadHandle nullptr")));
			return;
		}

		if (Download->bFailed)
		{
			MessagePlayer(WeakPlayer.Get(), AIChatCommand::MakePlainResponse(TEXT("Downloaded failed")));
			return;
		}

		if (Download->bCompleted)
		{
			MessagePlayer(WeakPlayer.Get(), AIChatCommand::MakePlainResponse(FString::Printf(TEXT("Downloaded successfully from '%s' to '%s'"), *Params[1], *Params[2])));
			return;
		}
		MessagePlayer(WeakPlayer.Get(), AIChatCommand::MakePlainResponse(TEXT("Downloaded failed, unknown error")));
	});

	return AIChatCommand::MakePlainResponse(FString::Printf(TEXT("Trying to download from '%s' to '%s'"), *Params[1], *Params[2]));
}

FChatCommandResponse AIChatCommandManager::DemoRecLocalCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	UIGameInstance* IGameInstance = Cast<UIGameInstance>(CallingPlayer->GetGameInstance());
	if (IGameInstance == nullptr)
	{
		return GetResponseCmdNullObject(TEXT("IGameInstance"));
	}

	AIGameState* IGameState = UIGameplayStatics::GetIGameState(CallingPlayer);
	if (IGameState == nullptr)
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("Object"), FText::FromString(TEXT("IGameState")));
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdNullObject"), Args);
	}

	if (!IGameState->GetGameStateFlags().bAllowReplayRecording && !CheckAdmin(CallingPlayer))
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdNoPermission"));
	}

	IGameInstance->ReplayStartRecording();
	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdDemoRecSuccess"));
}

FChatCommandResponse AIChatCommandManager::DemoStopLocalCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	UIGameInstance* IGameInstance = Cast<UIGameInstance>(CallingPlayer->GetGameInstance());
	if (IGameInstance == nullptr)
	{
		return GetResponseCmdNullObject(TEXT("IGameInstance"));
	}

	IGameInstance->ReplayStopRecording();
	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdDemoStopSuccess"));
}

FChatCommandResponse AIChatCommandManager::CrashCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	return CrashServerCommand(Params);
}

FChatCommandResponse AIChatCommandManager::CrashServerCommand(TArray<FString> Params)
{
	ETestCrashType TestCrashType = ETestCrashType::Fatal;

	if (Params.Num() == 2)
	{
		FString CrashType = Params[1];
		if (CrashType.ToLower() == TEXT("event"))
		{
			TestCrashType = ETestCrashType::Event;
		}
		else if (CrashType.ToLower() == TEXT("assert"))
		{
			TestCrashType = ETestCrashType::Assert;
		}
		else if (CrashType.ToLower() == TEXT("fatal"))
		{
			TestCrashType = ETestCrashType::Fatal;
		}
		else if (CrashType.ToLower() == TEXT("crash"))
		{
			TestCrashType = ETestCrashType::Crash;
		}
		else
		{
			const FFormatNamedArguments Arguments{
				{ TEXT("CrashString"), FText::FromString(CrashType) }
			};

			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdCrashFail"), Arguments);
		}
	}
	else
	{
		// Disabled as we want to default to fatal with /crash
		// return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	UIGameInstance* IGameInstance = UIGameplayStatics::GetIGameInstance(this);
	if (IGameInstance == nullptr)
	{
		return GetResponseCmdNullObject(TEXT("IGameInstance"));
	}
	IGameInstance->Crash(TestCrashType);
	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdCrashSuccess"));
}

FChatCommandResponse AIChatCommandManager::RespawnLocalCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	bool bSuccess = CallingPlayer->Suicide();

	if (bSuccess)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdRespawnSuccess"));
	}
	else
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdRespawnFailed"));
	}
}

FChatCommandResponse AIChatCommandManager::WaystoneCooldownCommand(TArray<FString> Params)
{
	if (!GetWorld())
	{
		return GetResponseCmdNullObject(TEXT("GetWorld()"));
	}
	if (Params.Num() != 3)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	if (!Params[2].IsNumeric())
	{
		const FFormatNamedArguments Arguments{
			{ TEXT("Number"), FText::FromString(Params[2]) }
		};

		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdWaystoneInvalidFloat"), Arguments);
	}

	// Get multiplier from percentage
	float WaystoneCooldown = FMath::Clamp(FCString::Atof(*Params[2]) / 100, 0.f, 1.f);

	// Check the waystone exists before getting the waystone manager
	AIWaystone* TargetIWaystone = GetIWaystone(GetWorld(), Params[1]);
	if (!TargetIWaystone)
	{
		const FFormatNamedArguments Arguments{ 
			{ TEXT("Waystone"), FText::FromString(Params[1]) } 
		};

		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdWaystoneInvalidWaystone"), Arguments);
	}
	AIWaystoneManager* WaystoneManager = nullptr;
	if (AIWorldSettings* IWorldSettings = AIWorldSettings::GetWorldSettings(this))
	{
		WaystoneManager = IWorldSettings->WaystoneManager;
	}
	check(WaystoneManager);
	if (!WaystoneManager)
	{
		return FChatCommandResponse();
	}

	WaystoneManager->SetWaystoneCooldownProgress(FName(Params[1]), WaystoneCooldown);

	const FFormatNamedArguments Arguments{
		{ TEXT("Seconds"), FText::FromString(FString::SanitizeFloat(WaystoneManager->GetWaystoneCooldownSeconds(FName(Params[1])))) }
	};

	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdWaystoneSetSuccess"), Arguments);
}

FChatCommandResponse AIChatCommandManager::WaterQualityCommand(TArray<FString> Params)
{
	if (!GetWorld())
	{
		return GetResponseCmdNullObject(TEXT("GetWorld()"));
	}
	if (Params.Num() != 3)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	if (!Params[2].IsNumeric())
	{
		const FFormatNamedArguments Arguments{ 
			{ TEXT("Number"), FText::FromString(Params[2]) }
		};

		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdWaterInvalidFloat"), Arguments);
	}
	// Convert percentage to multiplier
	float WaterQuality = FMath::Clamp(FCString::Atof(*Params[2]) / 100, 0.f, 1.f);

	AIWater* TargetWaterBody = GetIWater(GetWorld(), Params[1]);
	if (!TargetWaterBody)
	{
		const FFormatNamedArguments Arguments{ 
			{ TEXT("Water"), FText::FromString(Params[1]) }
		};

		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdWaterInvalidWater"), Arguments);
	}

	TargetWaterBody->ModifyWaterQuality(WaterQuality, false, true);

	const FFormatNamedArguments Arguments{
		{ TEXT("Number"), FText::FromString(FString::SanitizeFloat(TargetWaterBody->GetWaterQuality())) }
	};

	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdWaterSetSuccess"), Arguments);
}

FChatCommandResponse AIChatCommandManager::ListWatersCommand(TArray<FString> Params)
{
	FString Result = TEXT("");
	Result.Reserve(1024);

	TSet<FName> WaterNames{};
	WaterNames.Reserve(64);

	for (TObjectIterator<AIWater> WaterIt; WaterIt; ++WaterIt)
	{
		const FName& WaterTag = WaterIt->GetIndentifier();
		if (WaterTag == NAME_None || WaterNames.Contains(WaterTag))
		{
			continue;
		}

		WaterNames.Add(WaterTag);

		if (!Result.IsEmpty())
		{
			Result.Append(TEXT(", "));
		}

		Result.Append(WaterTag.ToString());
	}

	return AIChatCommand::MakePlainResponse(Result);
}

FChatCommandResponse AIChatCommandManager::ListWaystonesCommand(TArray<FString> Params)
{
	FString Result = TEXT("");
	Result.Reserve(1024);

	TSet<FName> WaystoneNames{};
	WaystoneNames.Reserve(64);

	for (TObjectIterator<AIWaystone> WaystoneIt; WaystoneIt; ++WaystoneIt)
	{
		const FName& WaystoneTag = WaystoneIt->WaystoneTag;
		if (WaystoneTag == NAME_None || WaystoneNames.Contains(WaystoneTag))
		{
			continue;
		}

		WaystoneNames.Add(WaystoneTag);

		if (!Result.IsEmpty())
		{
			Result.Append(TEXT(", "));
		}

		Result.Append(WaystoneTag.ToString());
	}

	return AIChatCommand::MakePlainResponse(Result);
}

FChatCommandResponse AIChatCommandManager::EditQuests(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	AIPlayerController* TargetIPlayerController;
	FString TargetUsername = TEXT("");

	if (Params.Num() == 2)
	{
		TargetUsername = Params[1];

		TargetIPlayerController = Cast<AIPlayerController>(PlayerControllerFromUsername(CallingPlayer, TargetUsername));
	}
	else if (Params.Num() == 1)
	{
		TargetIPlayerController = CallingPlayer;
	}
	else
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	if (!TargetIPlayerController)
	{
		return GetResponseCmdInvalidUsername(TargetUsername);
	}

	AIBaseCharacter* IBaseCharacter = Cast<AIBaseCharacter>(TargetIPlayerController->GetPawn());
	if (!IBaseCharacter)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdEditQuestsInvalidPawn"));
	}

	AIPlayerState* IPlayerState = Cast<AIPlayerState>(IBaseCharacter->GetPlayerState());

	TArray<FPrimaryAssetId> QuestAssetIds;
	for (UIQuest* Quest : IBaseCharacter->GetActiveQuests())
	{
		if (!Quest)
		{
			continue;
		}
		QuestAssetIds.Add(Quest->GetQuestId());
	}
	// Get the unique ID of the base character so we can get the base character again
	// when the client wants to edit the quests. Can't sent the base character ptr because
	// it may not be net relevant
	uint32 CharacterId = IBaseCharacter->GetUniqueID();
	CallingPlayer->ClientOpenQuestEditorWidget(IPlayerState, CharacterId, QuestAssetIds);

	return FChatCommandResponse();
}

void AIChatCommandManager::AdminRequestAction(const FString& Identifier, bool bUseName, bool bBan, const FString& Reason)
{
	// Moved this function here because it wouldnt work with the refactored "standalone" command system
	UIGameInstance* GameInstance = UIGameplayStatics::GetIGameInstance(this);
	check(GameInstance);
	if (!GameInstance)
	{
		return;
	}

	AIGameSession* Session = GameInstance->GetGameSession();
	check(Session);
	if (!Session)
	{
		return;
	}

	// Kick Reason
	FText FinalReason = FText::FromString(Reason);

	// Format Reason for kick/ban
	if (Reason.IsEmpty())
	{
		// Default empty messages
		if (bBan)
		{
			FinalReason = FText::FromString(TEXT("Admin Ban."));
		}
		else
		{
			FinalReason = FText::FromString(TEXT("Admin Kick."));
		}
	}
	else
	{
		// Add a prefix to declare if ban or kick and then append reason
		FString PrefixReason;
		if (bBan)
		{
			PrefixReason = TEXT("Banned for: ");
		}
		else
		{
			PrefixReason = TEXT("Kicked for: ");
		}

		FinalReason = FText::FromString(PrefixReason.Append(Reason));
	}

	// Loop through player controllers and find the victim...
	AGameStateBase* State = GetWorld()->GetGameState();
	if (!IsValid(State))
	{
		return;
	}

	for (auto& PS : State->PlayerArray)
	{
		// Cast to Game Specific Player State Object
		if (AIPlayerState* IPlayerState = Cast<AIPlayerState>(PS))
		{
			// Don't check Player States being destroyed
			if (!IsValid(IPlayerState))
			{
				continue;
			}

			if (IPlayerState->GetAlderonID().ToString() == Identifier || IPlayerState->GetAlderonID().ToDisplayString() == Identifier || (bUseName && IPlayerState->GetPlayerName() == Identifier))
			{
				AIPlayerController* Controller = Cast<AIPlayerController>(IPlayerState->GetOwner());
				if (bBan)
				{
					Session->BanPlayer(Controller, FinalReason);
				}
				else
				{
					Session->KickPlayer(Controller, FinalReason);
				}

				return;
			}
		}
	}
}

FChatCommandResponse AIChatCommandManager::SinglePlayerGoto(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	if (Params.Num() != 2)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	UIGameInstance* IGameInstance = UIGameplayStatics::GetIGameInstance(CallingPlayer);
	check(IGameInstance);
	if (!IGameInstance)
	{
		return GetResponseCmdNullObject(TEXT("IGameInstance"));
	}

	AIBaseCharacter* IBaseCharacter = Cast<AIBaseCharacter>(CallingPlayer->GetPawn());
	check(IBaseCharacter);
	if (!IBaseCharacter)
	{
		return GetResponseCmdNullObject(TEXT("IBaseCharacter"));
	}

	TWeakObjectPtr<AIBaseCharacter> WeakCharacterPtr = IBaseCharacter;

	AILevelStreamingLoader* StreamingLoader = CallingPlayer->GetWorld()->SpawnActor<AILevelStreamingLoader>(IBaseCharacter->GetActorLocation(), IBaseCharacter->GetActorRotation());
	check(StreamingLoader);
	if (!StreamingLoader)
	{
		const FFormatNamedArguments Arguments{
			{ TEXT("Location"), FText::FromString(Params[1]) }
		};

		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdGotoAttempt"), Arguments);
	}

	FVector TargetLocation;
	FString LocationString = Params[1];
	if (AActor* POI = GetPoi(CallingPlayer, LocationString))
	{
		// Disable streaming for our playercontroller, only have streaming for our streamingloader
		IGameInstance->SetLevelStreamingEnabled(false);
		// lock the player so we dont fall through the world
		IBaseCharacter->SetPreloadPlayerLocks(true);

		TWeakObjectPtr<AActor> WeakPOI = POI;
		TWeakObjectPtr<AIChatCommandManager> WeakThis = this;

		StreamingLoader->Setup(POI->GetActorLocation(), AILevelStreamingLoader::FLevelStreamingLoaderComplete::CreateLambda([WeakCharacterPtr, WeakPOI, WeakThis, IGameInstance]() {
			IGameInstance->SetLevelStreamingEnabled(true);
			if (!WeakCharacterPtr.Get() || !WeakPOI.Get() || !WeakThis.Get())
			{
				return;
			}
			WeakCharacterPtr->SetPreloadPlayerLocks(false);
			FVector PoiLocation;
			if (WeakThis->GetLocationFromPoi(WeakPOI.Get(), PoiLocation, WeakCharacterPtr->IsAquatic()))
			{
				UE_LOG(TitansLog, Log, TEXT("AIChatCommandManager::SinglePlayerGoto: TP to POI at %s"), *PoiLocation.ToString());
				if (WeakCharacterPtr->GetCurrentInstance())
				{
					WeakCharacterPtr->ExitInstance(FTransform(WeakCharacterPtr->GetActorRotation(), PoiLocation));
				}
				else
				{
					WeakCharacterPtr->TeleportToFromCommand(PoiLocation, WeakCharacterPtr->GetActorRotation(), false, true);
				}
			}
			else
			{
				UE_LOG(TitansLog, Log, TEXT("AIChatCommandManager::SinglePlayerGoto: TP to POI ActorLocation at %s"), *WeakPOI->GetActorLocation().ToString());
				if (WeakCharacterPtr->GetCurrentInstance())
				{
					WeakCharacterPtr->ExitInstance(FTransform(WeakCharacterPtr->GetActorRotation(), WeakPOI->GetActorLocation()));
				}
				else
				{
					WeakCharacterPtr->TeleportToFromCommand(WeakPOI->GetActorLocation(), WeakCharacterPtr->GetActorRotation(), false, true);
				}
			}
		}));
	}
	else if (LocationString.Contains(TEXT("(X="), ESearchCase::IgnoreCase, ESearchDir::FromStart) && TargetLocation.InitFromString(LocationString))
	{
		// Disable streaming for our playercontroller, only have streaming for our streamingloader
		IGameInstance->SetLevelStreamingEnabled(false);
		// lock the player so we dont fall through the world
		IBaseCharacter->SetPreloadPlayerLocks(true);

		StreamingLoader->Setup(TargetLocation, AILevelStreamingLoader::FLevelStreamingLoaderComplete::CreateLambda([WeakCharacterPtr, TargetLocation, IGameInstance]() {
			IGameInstance->SetLevelStreamingEnabled(true);
			if (!WeakCharacterPtr.Get())
			{
				return;
			}
			WeakCharacterPtr->SetPreloadPlayerLocks(false);
			UE_LOG(TitansLog, Log, TEXT("AIChatCommandManager::SinglePlayerGoto: TP to TargetLocation at %s"), *TargetLocation.ToString());
			if (WeakCharacterPtr->GetCurrentInstance())
			{
				WeakCharacterPtr->ExitInstance(FTransform(WeakCharacterPtr->GetActorRotation(), TargetLocation));
			}
			else
			{
				WeakCharacterPtr->TeleportToFromCommand(TargetLocation, WeakCharacterPtr->GetActorRotation(), false, true);
			}
		}));
	}
	else // Not POI or location string
	{
		StreamingLoader->Destroy();
		const FFormatNamedArguments Arguments{ 
			{ TEXT("Location"), FText::FromString(Params[1]) }
		};

		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdGotoAttemptFailed"), Arguments);
	}

	const FFormatNamedArguments Arguments{
		{ TEXT("Location"), FText::FromString(Params[1]) }
	};

	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdGotoAttempt"), Arguments);
}

FChatCommandResponse AIChatCommandManager::SinglePlayerTeleport(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	// Use same functionality as goto in singleplayer because there is no one else to teleport
	return SinglePlayerGoto(CallingPlayer, Params);
}

FChatCommandResponse AIChatCommandManager::EnterHomecaveCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	IAlderonCommon& AlderonCommon = IAlderonCommon::Get();
	if (!UIGameplayStatics::AreHomeCavesEnabled(this))
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdUnknownCommand"));
	}
	if (!Cast<AIBaseCharacter>(CallingPlayer->GetPawn()))
	{
		return AIChatCommand::MakePlainResponse(TEXT("No Pawn"));
	}
	CallingPlayer->SpawnAndEnterInstance(Cast<AIBaseCharacter>(CallingPlayer->GetPawn())->DefaultHomecaveInstanceId, CallingPlayer->GetPawn()->GetActorTransform());
	return AIChatCommand::MakePlainResponse(TEXT("Entering homecave..."));
}

FChatCommandResponse AIChatCommandManager::SetNewMovementCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	if (Params.Num() < 2)
	{
		return AIChatCommand::MakePlainResponse(TEXT("Must specify on/off"));
	}

	if (Params[1] == TEXT("on"))
	{
		UIGameplayStatics::GetIGameStateChecked(this)->SetIsNewCharacterMovement(true);
		return AIChatCommand::MakePlainResponse(TEXT("Set new movement on"));
	}
	else if (Params[1] == TEXT("off"))
	{
		UIGameplayStatics::GetIGameStateChecked(this)->SetIsNewCharacterMovement(false);
		return AIChatCommand::MakePlainResponse(TEXT("Set new movement off"));
	}

	return AIChatCommand::MakePlainResponse(TEXT("Must specify on/off"));
}

FChatCommandResponse AIChatCommandManager::LeaveHomecaveCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	IAlderonCommon& AlderonCommon = IAlderonCommon::Get();
	if (!UIGameplayStatics::AreHomeCavesEnabled(this))
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdUnknownCommand"));
	}
	if (!Cast<AIBaseCharacter>(CallingPlayer->GetPawn()))
	{
		return AIChatCommand::MakePlainResponse(TEXT("No Pawn"));
	}
	if (AIBaseCharacter* IBaseCharacter = Cast<AIBaseCharacter>(CallingPlayer->GetPawn()))
	{
		IBaseCharacter->ExitInstance(FTransform());
		return AIChatCommand::MakePlainResponse(TEXT("Leaving homecave..."));
	}

	return AIChatCommand::MakePlainResponse(TEXT("Failed to leave homecave..."));
}

FChatCommandResponse AIChatCommandManager::ClearCreatorObjectsCommand(TArray<FString> Params)
{
	if (!GetWorld())
	{
		return GetResponseCmdNullObject(TEXT("GetWorld()"));
	}
	IAlderonCommon& AlderonCommon = IAlderonCommon::Get();
	if (!AlderonCommon.GetRemoteConfig().GetRemoteConfigFlag(TEXT("bCreatorMode")) && !AlderonCommon.GetRemoteConfig().GetRemoteConfigFlag(TEXT("bCreatorModeSinglePlayer")))
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdUnknownCommand"));
	}
	UICreatorModeObjectComponent::RemoveAllCreatorModeObjects(GetWorld());
	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdClearCreatorObjectsSuccess"));
}

FChatCommandResponse AIChatCommandManager::GiveWoundCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	static TMap<FString, EDamageWoundCategory> CategoriesMap;
	if (CategoriesMap.IsEmpty())
	{
		CategoriesMap.FindOrAdd("HeadLeft") = EDamageWoundCategory::HeadLeft;
		CategoriesMap.FindOrAdd("HeadRight") = EDamageWoundCategory::HeadRight;
		CategoriesMap.FindOrAdd("NeckLeft") = EDamageWoundCategory::NeckLeft;
		CategoriesMap.FindOrAdd("NeckRight") = EDamageWoundCategory::NeckRight;
		CategoriesMap.FindOrAdd("ShoulderLeft") = EDamageWoundCategory::ShoulderLeft;
		CategoriesMap.FindOrAdd("ShoulderRight") = EDamageWoundCategory::ShoulderRight;
		CategoriesMap.FindOrAdd("BodyLeft") = EDamageWoundCategory::BodyLeft;
		CategoriesMap.FindOrAdd("BodyRight") = EDamageWoundCategory::BodyRight;
		CategoriesMap.FindOrAdd("TailBaseLeft") = EDamageWoundCategory::TailBaseLeft;
		CategoriesMap.FindOrAdd("TailBaseRight") = EDamageWoundCategory::TailBaseRight;
		CategoriesMap.FindOrAdd("TailTip") = EDamageWoundCategory::TailTip;
		CategoriesMap.FindOrAdd("LeftArm") = EDamageWoundCategory::LeftArm;
		CategoriesMap.FindOrAdd("RightArm") = EDamageWoundCategory::RightArm;
		CategoriesMap.FindOrAdd("LeftHand") = EDamageWoundCategory::LeftHand;
		CategoriesMap.FindOrAdd("RightHand") = EDamageWoundCategory::RightHand;
		CategoriesMap.FindOrAdd("LeftLeg") = EDamageWoundCategory::LeftLeg;
		CategoriesMap.FindOrAdd("RightLeg") = EDamageWoundCategory::RightLeg;
		CategoriesMap.FindOrAdd("LeftFoot") = EDamageWoundCategory::LeftFoot;
		CategoriesMap.FindOrAdd("RightFoot") = EDamageWoundCategory::RightFoot;
	}

	if (Params.IsValidIndex(1) && Params[1] == TEXT("Help"))
	{
		FString CategoriesHelperString;
		for (auto& Pair : CategoriesMap)
		{
			CategoriesHelperString.Append(Pair.Key);
			CategoriesHelperString.Append(TEXT(", "));
		}
		CategoriesHelperString.RemoveFromEnd(TEXT(", "));

		const FFormatNamedArguments Arguments{ 
			{ TEXT("Categories"), FText::FromString(CategoriesHelperString) }
		};

		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdWoundHelp"), Arguments);
	}

	bool bPermanent = Params[0] == TEXT("SetPermaWound");
	FString CategoryString;
	AIBaseCharacter* TargetCharacter = nullptr;
	float Value = 0;
	if (Params.Num() == 3)
	{
		TargetCharacter = CallingPlayer->GetPawn<AIBaseCharacter>();
		CategoryString = Params[1];
		if (!FDefaultValueHelper::ParseFloat(Params[2], Value))
		{
			return GetResponseCmdSetAttribCantParseFloat(Params[2]);
		}
	}
	else if (Params.Num() == 4)
	{
		FString TargetName = Params[1];
		AIPlayerController* TargetPlayerController = Cast<AIPlayerController>(PlayerControllerFromUsername(GetWorld(), TargetName));
		if (!TargetPlayerController)
		{
			return GetResponseCmdInvalidUsername(TargetName);
		}
		TargetCharacter = TargetPlayerController->GetPawn<AIBaseCharacter>();
		CategoryString = Params[2];
		if (!FDefaultValueHelper::ParseFloat(Params[3], Value))
		{
			return GetResponseCmdSetAttribCantParseFloat(Params[3]);
		}
	}
	else
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	if (!TargetCharacter)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdNoPawn"));
	}

	Value = FMath::Clamp(Value, -CLAMP_MINMAX, CLAMP_MINMAX);

	EDamageWoundCategory* Category = CategoriesMap.Find(CategoryString);
	if (!Category)
	{
		const FFormatNamedArguments Arguments{
			{ TEXT("Command"), FText::FromString(Params[0]) }
		};

		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdWoundFailed"), Arguments);
	}

	if (bPermanent)
	{
		float NormalizedPermaWoundDamage = FMath::Clamp((FMath::Clamp(Value, 0, 1) - TargetCharacter->NormalizedPermaWoundsDamageRange.X) / (TargetCharacter->NormalizedPermaWoundsDamageRange.Y - TargetCharacter->NormalizedPermaWoundsDamageRange.X), 0.f, 1.f);

		TargetCharacter->GetDamageWounds_Mutable().SetPermaDamageForCategory(*Category, NormalizedPermaWoundDamage);
	}
	else
	{
		TargetCharacter->GetDamageWounds_Mutable().SetDamageForCategory(*Category, FMath::Clamp(Value, 0, 1));
	}

	const FFormatNamedArguments Arguments{
		{ TEXT("Damage"), FText::AsNumber(Value) },
		{ TEXT("Category"), FText::FromString(CategoryString) }
	};

	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdWoundGiven"), Arguments);
}

FChatCommandResponse AIChatCommandManager::EnableDetachGroundTraces(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	AIBaseCharacter* const Character = CallingPlayer->GetPawn<AIBaseCharacter>();

	if (!Character)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdDetachGroundTraceNoCharacter"));
	}

	if (!Params.IsValidIndex(1))
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdDetachGroundTraceNoArguments"));
	}

	int32 Enabled = 0;

	if (!FDefaultValueHelper::ParseInt(Params[1], Enabled))
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdDetachGroundTraceInvalidArgument"));
	}

	if (Enabled == 0)
	{
		Character->SetEnableDetachGroundTraces(false);
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdDetachGroundTracesDisabled"));
	}

	Character->SetEnableDetachGroundTraces(true);
	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdDetachGroundTracesEnabled"));
}

FChatCommandResponse AIChatCommandManager::EnableDetachFallDamageCheck(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	AIBaseCharacter* const Character = CallingPlayer->GetPawn<AIBaseCharacter>();

	if (!Character)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdDetachFallDamageCheckNoCharacter"));
	}

	if (!Params.IsValidIndex(1))
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdDetachFallDamageCheckNoArguments"));
	}

	int32 Enabled = 0;

	if (!FDefaultValueHelper::ParseInt(Params[1], Enabled))
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdDetachFallDamageCheckInvalidArgument"));
	}

	if (Enabled == 0)
	{
		Character->SetEnableDetachFallDamageCheck(false);
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdDetachFallDamageCheckDisabled"));
	}

	Character->SetEnableDetachFallDamageCheck(true);
	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdDetachFallDamageCheckEnabled"));
}

#if WITH_EDITOR
FChatCommandResponse AIChatCommandManager::DebugBloodTex(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	if (AIBaseCharacter* IBaseCharacter = CallingPlayer->GetPawn<AIBaseCharacter>())
	{
		IBaseCharacter->UpdateBloodMask();

		float HeadLeft = IBaseCharacter->GetDamageWounds().GetDamageForCategory(EDamageWoundCategory::HeadLeft);
		float HeadRight = IBaseCharacter->GetDamageWounds().GetDamageForCategory(EDamageWoundCategory::HeadRight);

		float NeckLeft = IBaseCharacter->GetDamageWounds().GetDamageForCategory(EDamageWoundCategory::NeckLeft);
		float NeckRight = IBaseCharacter->GetDamageWounds().GetDamageForCategory(EDamageWoundCategory::NeckRight);

		float ShoulderLeft = IBaseCharacter->GetDamageWounds().GetDamageForCategory(EDamageWoundCategory::ShoulderLeft);
		float ShoulderRight = IBaseCharacter->GetDamageWounds().GetDamageForCategory(EDamageWoundCategory::ShoulderRight);

		float BodyLeft = IBaseCharacter->GetDamageWounds().GetDamageForCategory(EDamageWoundCategory::BodyLeft);
		float BodyRight = IBaseCharacter->GetDamageWounds().GetDamageForCategory(EDamageWoundCategory::BodyRight);

		float TailBaseLeft = IBaseCharacter->GetDamageWounds().GetDamageForCategory(EDamageWoundCategory::TailBaseLeft);
		float TailBaseRight = IBaseCharacter->GetDamageWounds().GetDamageForCategory(EDamageWoundCategory::TailBaseRight);

		float TailTip = IBaseCharacter->GetDamageWounds().GetDamageForCategory(EDamageWoundCategory::TailTip);

		float LeftArm = IBaseCharacter->GetDamageWounds().GetDamageForCategory(EDamageWoundCategory::LeftArm);
		float RightArm = IBaseCharacter->GetDamageWounds().GetDamageForCategory(EDamageWoundCategory::RightArm);

		float LeftHand = IBaseCharacter->GetDamageWounds().GetDamageForCategory(EDamageWoundCategory::LeftHand);
		float RightHand = IBaseCharacter->GetDamageWounds().GetDamageForCategory(EDamageWoundCategory::RightHand);

		float LeftLeg = IBaseCharacter->GetDamageWounds().GetDamageForCategory(EDamageWoundCategory::LeftLeg);
		float RightLeg = IBaseCharacter->GetDamageWounds().GetDamageForCategory(EDamageWoundCategory::RightLeg);

		float LeftFoot = IBaseCharacter->GetDamageWounds().GetDamageForCategory(EDamageWoundCategory::LeftFoot);
		float RightFoot = IBaseCharacter->GetDamageWounds().GetDamageForCategory(EDamageWoundCategory::RightFoot);

		return AIChatCommand::MakePlainResponse(FString::Printf(TEXT("HeadLeft: %f HeadRight: %f NeckLeft: %f NeckRight: %f ShoulderLeft: %f ShoulderRight: %f BodyLeft: %f BodyRight: %f TailBaseLeft: %f TailBaseRight: %f TailTip: %f LeftArm: %f RightArm: %f LeftHand: %f RightHand: %f LeftLeg: %f RightLeg: %f LeftFoot: %f RightFoot: %f"),
			HeadLeft, HeadRight, NeckLeft, NeckRight, ShoulderLeft, ShoulderRight, BodyLeft, BodyRight, TailBaseLeft, TailBaseRight, TailTip, LeftArm, RightArm, LeftHand, RightHand, LeftLeg, RightLeg, LeftFoot, RightFoot));
	}
	return AIChatCommand::MakePlainResponse(TEXT("Error: No Character"));
}

#endif

FChatCommandResponse AIChatCommandManager::ServerPerfTest(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	if (CallingPlayer == nullptr || !CheckAdmin(CallingPlayer) || Params.Num() != 2)
	{
		return FChatCommandResponse();
	}

	AIBaseCharacter* BaseChar = CallingPlayer->GetPawn<AIBaseCharacter>();
	if (BaseChar == nullptr)
	{
		return FChatCommandResponse();
	}

	int32 DinosToSpawn = 0;
	if (!FDefaultValueHelper::ParseInt(Params[1], DinosToSpawn))
	{
		return FChatCommandResponse();
	}

	if (DinosToSpawn == 0)
	{
		return FChatCommandResponse();
	}

	for (int32 i = 0; i < DinosToSpawn; i++)
	{
		CallingPlayer->ServerSpawnCharacterAtRandomizedTransform(BaseChar->GetCharacterID(), BaseChar->GetActorLocation(), 10000.f, BaseChar->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	}

	CallingPlayer->AddServerPerfAICount(DinosToSpawn);

	return FChatCommandResponse();
}

FChatCommandResponse AIChatCommandManager::ServerAutoRecord(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	if (Params.Num() < 2)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdServerAutoRecordNoBool"));
	}
	AIGameMode* const IGameMode = UIGameplayStatics::GetIGameMode(this);
	if (!IGameMode)
	{
		return AIChatCommand::MakePlainResponse(TEXT("Error IGameMode nullptr"));
	}

	const bool CurrentConfig = Params[1].ToBool();
	IGameMode->SetServerAutoRecord(CurrentConfig);

	const FText Result = CurrentConfig ? FText::FromString(TEXT("true")) : FText::FromString(TEXT("false"));

	const FFormatNamedArguments Args{ { TEXT("Value"), Result } };

	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdServerAutoRecordResponse"), Args);
}

FChatCommandResponse AIChatCommandManager::ServerAutoRecordRCON(TArray<FString> Params)
{
	if (Params.Num() < 2)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdServerAutoRecordNoBool"));
	}

	AIGameMode* const IGameMode = UIGameplayStatics::GetIGameMode(this);
	if (!IGameMode)
	{
		return AIChatCommand::MakePlainResponse(TEXT("Error IGameMode nullptr"));
	}

	const bool CurrentConfig = Params[1].ToBool();
	IGameMode->SetServerAutoRecord(CurrentConfig);

	const FText Result = CurrentConfig ? FText::FromString(TEXT("true")) : FText::FromString(TEXT("false"));

	const FFormatNamedArguments Args{ { TEXT("Value"), Result } };

	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdServerAutoRecordResponse"), Args);
}

FChatCommandResponse AIChatCommandManager::ResetHomeCaveSaveInfoCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	if (AIBaseCharacter* IBaseCharacter = CallingPlayer->GetPawn<AIBaseCharacter>())
	{
		if (IBaseCharacter->GetCurrentInstance())
		{
			IBaseCharacter->ExitInstance(FTransform());
		}
		IBaseCharacter->GetHomeCaveSaveableInfo_Mutable() = FHomeCaveSaveableInfo();
		IBaseCharacter->InstanceLogoutInfo.Empty();

		UIGameplayStatics::GetIGameModeChecked(this)->SaveCharacter(IBaseCharacter, ESavePriority::Medium);
	}
	return AIChatCommand::MakePlainResponse(TEXT("Reset Home Caves for Debug."));
}

FChatCommandResponse AIChatCommandManager::WhisperRCONCommand(TArray<FString> Params)
{
	if (Params.Num() < 2)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdWhisperNoMessage"));
	}
	int32 MessageIndex = 1;
	AIPlayerController* Recipient = nullptr;
	if (AIPlayerController* PotentialRecipient = Cast<AIPlayerController>(PlayerControllerFromUsername(GetWorld(), Params[1])))
	{
		Recipient = PotentialRecipient;
		MessageIndex++;
	}
	if (MessageIndex == 2 && Params.Num() == 2)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdWhisperNoMessage"));
	}
	if (!Recipient)
	{
		return GetResponseCmdInvalidUsername(Params[1]);
	}

	FString WhisperMessage = TEXT("");
	for (int32 Index = MessageIndex; Index < Params.Num(); Index++)
	{
		WhisperMessage.Append(Params[Index]);
		WhisperMessage.AppendChar(' ');
	}
	if (WhisperMessage.IsEmpty())
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdWhisperNoMessage"));
	}
	FGameChatMessage Message;
	Message.WhisperRecipient = Recipient->GetPlayerState<AIPlayerState>();
	Message.PlayerState = nullptr;
	Message.Message = WhisperMessage;
	Message.Channel = EChatChannel::Global;
	Message.bIsFromRCON = true;

	Recipient->ClientRecieveChatMessage(Message);

	const FFormatNamedArguments Arguments{
		{ TEXT("Username"), FText::FromString(Params[1]) },
		{ TEXT("Message"), FText::FromString(WhisperMessage) }
	};

	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdRCONWhisper"), Arguments);
}

FChatCommandResponse AIChatCommandManager::WhisperAllCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	if (Params.Num() < 2)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdWhisperNoMessage"));
	}

	AIPlayerState* const PlayerState = CallingPlayer->GetPlayerState<AIPlayerState>();
	if (!PlayerState)
	{
		return FChatCommandResponse();
	}

	// @TODO: Refactor the message handling with a method and apply it everywhere (all commands)
	FString WhisperMessage = TEXT("");
	WhisperMessage.Reserve(1024);
	for (int32 Index = 1; Index < Params.Num(); Index++)
	{
		WhisperMessage.Append(FString::Printf(TEXT("%s "), *Params[Index]));
	}

	if (WhisperMessage.IsEmpty())
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdWhisperNoMessage"));
	}

	IAlderonCommon& AlderonCommon = IAlderonCommon::Get();
	UAlderonChat* AlderonChat = AlderonCommon.GetChatInterface();

	if (AlderonChat)
	{
		FAlderonServerChatMsg AlderonChatMsg;
		AlderonChatMsg.Author = PlayerState->GetAlderonID();
		AlderonChatMsg.Message = WhisperMessage;
		AlderonChatMsg.Channel = TEXT("WhisperAll");
		AlderonChatMsg.Recipient = FAlderonPlayerID();
		AlderonChatMsg.Role = PlayerState->GetPlayerRole().Name;

		AlderonChat->StreamChatMessage(AlderonChatMsg);
	}
	else
	{
		UE_LOG(TitansOnline, Warning, TEXT("AIChatCommandManager::WhisperAllCommand - No AlderonChat found"));
	}

	// Report to Webhook
	if (AIGameSession::UseWebHooks(WEBHOOK_PlayerChat))
	{
		TMap<FString, TSharedPtr<FJsonValue>> WebHookProperties{
			{ TEXT("ChannelId"), MakeShareable(new FJsonValueNumber(static_cast<int>(EChatChannel::Global))) },
			{ TEXT("ChannelName"), MakeShareable(new FJsonValueString(TEXT("WhisperAll"))) },
			{ TEXT("PlayerName"), MakeShareable(new FJsonValueString(PlayerState->GetPlayerName())) },
			{ TEXT("Message"), MakeShareable(new FJsonValueString(WhisperMessage)) },
			{ TEXT("AlderonId"), MakeShareable(new FJsonValueString(PlayerState->GetAlderonID().ToDisplayString())) },
			{ TEXT("bServerAdmin"), MakeShareable(new FJsonValueBoolean(PlayerState->IsServerAdmin())) },
			{ TEXT("FromWhisper"), MakeShareable(new FJsonValueBoolean(true)) },
		};

		AIGameSession::TriggerWebHookFromContext(this, WEBHOOK_PlayerChat, WebHookProperties);
	}

	return WhisperAllRCONExecute(Params, PlayerState);
}

FChatCommandResponse AIChatCommandManager::WhisperAllRCONCommand(TArray<FString> Params)
{
	return WhisperAllRCONExecute(Params);
}

FChatCommandResponse AIChatCommandManager::WhisperAllRCONExecute(TArray<FString>& Params, AIPlayerState* const CallingPlayer /* nullptr in case of rcon */)
{
	if (Params.Num() < 2)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdWhisperNoMessage"));
	}

	FString WhisperMessage = TEXT("");
	WhisperMessage.Reserve(1024);
	for (int32 Index = 1; Index < Params.Num(); Index++)
	{
		WhisperMessage.Append(FString::Printf(TEXT("%s "), *Params[Index]));
	}

	if (WhisperMessage.IsEmpty())
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdWhisperNoMessage"));
	}

	FGameChatMessage Message{};
	Message.WhisperRecipient = nullptr;
	Message.PlayerState = CallingPlayer;
	Message.Message = WhisperMessage;
	Message.Channel = EChatChannel::Global;
	Message.bIsFromRCON = true;

	for (FConstPlayerControllerIterator PlayerIt = GetWorld()->GetPlayerControllerIterator(); PlayerIt; ++PlayerIt)
	{
		AIPlayerController* const PlayerController = Cast<AIPlayerController>(*PlayerIt);
		if (!PlayerController)
		{
			continue;
		}

		Message.WhisperRecipient = PlayerController->GetPlayerState<AIPlayerState>();
		PlayerController->ClientRecieveChatMessage(Message);
	}

	const FFormatNamedArguments Arguments{
		{ TEXT("Message"), FText::FromString(WhisperMessage) }
	};

	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdRCONWhisperAll"), Arguments);
}

FChatCommandResponse AIChatCommandManager::WhisperCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	if (!CallingPlayer)
	{
		return FChatCommandResponse();
	}
	if (Params.Num() < 2)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdWhisperNoMessage"));
	}
	if (AIWorldSettings* IWorldSettings = AIWorldSettings::GetWorldSettings(CallingPlayer))
	{
		if (AIQuestManager* QuestMgr = IWorldSettings->QuestManager)
		{
			QuestMgr->OnPlayerSubmitGenericTask(CallingPlayer->GetPawn<AIBaseCharacter>(), TEXT("SendChatMessage"));
		}
	}

	int32 MessageIndex = 1;
	AIPlayerController* Recipient = nullptr;
	if (AIPlayerController* PotentialRecipient = Cast<AIPlayerController>(PlayerControllerFromUsername(CallingPlayer, Params[1])))
	{
		Recipient = PotentialRecipient;
		MessageIndex++;
	}
	if (MessageIndex == 2 && Params.Num() == 2)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdWhisperNoMessage"));
	}
	if (!Recipient)
	{
		Recipient = CallingPlayer->GetLastWhisperRecipient();
	}
	if (!Recipient)
	{
		return GetResponseCmdInvalidUsername(Params[1]);
	}
	if (Recipient == CallingPlayer)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdWhisperSelf"));
	}

	FString WhisperMessage = TEXT("");
	for (int32 Index = MessageIndex; Index < Params.Num(); Index++)
	{
		WhisperMessage.Append(Params[Index]);
		WhisperMessage.AppendChar(' ');
	}
	if (WhisperMessage.IsEmpty())
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdWhisperNoMessage"));
	}
	FGameChatMessage Message;
	Message.WhisperRecipient = Recipient->GetPlayerState<AIPlayerState>();
	Message.PlayerState = CallingPlayer->GetPlayerState<AIPlayerState>();
	Message.Message = WhisperMessage;
	Message.Channel = EChatChannel::Global;

	CallingPlayer->ClientSendWhisper(Message);

	return FChatCommandResponse();
}

FChatCommandResponse AIChatCommandManager::ListPlayersRCONCommand(TArray<FString> Params)
{
	FString PlayerList = TEXT("\n");

	AGameStateBase* GameState = GetWorld()->GetGameState();
	check(GameState);
	if (!IsValid(GameState))
	{
		return GetResponseCmdNullObject(TEXT("GameState"));
	}

	for (APlayerState* PS : GameState->PlayerArray)
	{
		// Cast to Game Specific Player State Object
		if (AIPlayerState* IPlayerState = Cast<AIPlayerState>(PS))
		{
			// Don't check Player States being destroyed
			if (!IsValid(IPlayerState))
			{
				continue;
			}

			PlayerList.Append(FString::Printf(TEXT("%s (%s)\n"), *IPlayerState->GetPlayerName(), *IPlayerState->GetAlderonID().ToDisplayString()));
		}
	}

	const FFormatNamedArguments Args{
		{ TEXT("PlayerCount"), FText::AsNumber(GameState->PlayerArray.Num()) },
		{ TEXT("PlayerList"), FText::FromString(PlayerList) }
	};

	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdListPlayersFormat"), Args);
}

FChatCommandResponse AIChatCommandManager::ListPlayerPositionsRCONCommand(TArray<FString> Params)
{
	FString PlayerList;
	PlayerList.Reserve(1024);
	PlayerList.Append(TEXT("\n"));

	const AGameStateBase* const GameState = GetWorld()->GetGameState();
	check(GameState);
	if (!IsValid(GameState))
	{
		return GetResponseCmdNullObject(TEXT("GameState"));
	}

	for (const APlayerState* const PS : GameState->PlayerArray)
	{
		// Cast to Game Specific Player State Object
		if (const AIPlayerState* const IPlayerState = Cast<AIPlayerState>(PS))
		{
			// Don't check Player States being destroyed
			if (!IsValid(IPlayerState))
			{
				continue;
			}

			const AIBaseCharacter* const IBaseCharacter = IPlayerState->GetPawn<AIBaseCharacter>();
			const FString PlayerLoc = IBaseCharacter ? IBaseCharacter->GetActorLocation().ToString() : TEXT("-");
			PlayerList.Append(FString::Printf(TEXT("%s (%s), Loc: %s\n"), *IPlayerState->GetPlayerName(), *IPlayerState->GetAlderonID().ToDisplayString(), *PlayerLoc));
		}
	}

	const FFormatNamedArguments Args{
		{ TEXT("PlayerCount"), FText::AsNumber(GameState->PlayerArray.Num()) },
		{ TEXT("PlayerList"), FText::FromString(PlayerList) }
	};

	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdListPlayersFormat"), Args);
}

FChatCommandResponse AIChatCommandManager::PlayerInfoRCONCommand(TArray<FString> Params)
{
	if (Params.Num() != 2)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	const FString& Username = Params[1];

	const AIPlayerState* const IPlayerState = Cast<AIPlayerState>(PlayerStateFromUsername(this, Username));
	if (!IPlayerState)
	{
		return GetResponseCmdInvalidUsername(Username);
	}

	const AIBaseCharacter* const IBaseCharacter = IPlayerState->GetPawn<AIBaseCharacter>();

	const FFormatNamedArguments Args{
		{ TEXT("Name"), FText::FromString(IPlayerState->GetPlayerName()) },
		{ TEXT("AGID"), FText::FromString(IPlayerState->GetAlderonID().ToDisplayString()) },
		{ TEXT("Dino"), IBaseCharacter ? IPlayerState->GetCharacterSpecies() : FText::FromString(TEXT("-")) },
		{ TEXT("Role"), FText::FromString(IPlayerState->GetPlayerRole().bAssigned ? IPlayerState->GetPlayerRole().Name : TEXT("None")) },
		{ TEXT("Marks"), IBaseCharacter ? IPlayerState->GetMarksTemp() : 0 },
		{ TEXT("Growth"), IBaseCharacter ? IBaseCharacter->GetGrowthPercent() : 0 },
		{ TEXT("Location"), FText::FromString(IBaseCharacter ? IBaseCharacter->GetActorLocation().ToString() : TEXT("-")) }
	};
	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdPlayerInfoFormat"), Args);
}

FChatCommandResponse AIChatCommandManager::ServerMuteRCONCommand(TArray<FString> Params)
{
	FPlayerMute PlayerMute = FPlayerMute();

	FString PlayerString = TEXT("");
	FString TimeString = TEXT("");

	if (Params.Num() > 5 || Params.Num() < 2)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	if (Params.IsValidIndex(1))
	{
		PlayerString = Params[1];
	}
	if (Params.IsValidIndex(2))
	{
		if (Params[2] != TEXT("0"))
		{
			TimeString = Params[2];
		}
	}
	if (Params.IsValidIndex(3))
	{
		PlayerMute.AdminReason = Params[3];
	}
	if (Params.IsValidIndex(4))
	{
		PlayerMute.UserReason = Params[4];
	}

	if (AIPlayerState* IPlayerState = Cast<AIPlayerState>(PlayerStateFromUsername(this, PlayerString)))
	{
		PlayerMute.PlayerId = IPlayerState->GetAlderonID();
	}
	else if (IsValidAlderonId(PlayerString))
	{
		PlayerMute.PlayerId = FAlderonPlayerID(PlayerString);
	}
	else
	{
		return GetResponseCmdInvalidUsername(PlayerString);
	}

	AIGameSession* IGameSession = UIGameplayStatics::GetIGameSessionChecked(this);
	if (!IGameSession)
	{
		return GetResponseCmdNullObject(TEXT("IGameSession"));
	}

	if (IGameSession->IsPlayerServerMuted(PlayerMute.PlayerId))
	{
		const FFormatNamedArguments Arguments{
			{ TEXT("Username"), FText::FromString(PlayerString) } 
		};

		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdServerMuteAlreadyMuted"), Arguments);
	}

	int32 TimeSeconds = 0;
	if (!TimeString.IsEmpty())
	{
		if (TimeStringToSeconds(TimeString, TimeSeconds))
		{
			PlayerMute.BanExpiration = FDateTime::UtcNow().ToUnixTimestamp() + TimeSeconds;
		}
		else
		{
			const FFormatNamedArguments Arguments{
				{ TEXT("Time"), FText::FromString(TimeString) }
			};

			return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdServerMuteInvalidTime"), Arguments);
		}
	}

	IGameSession->AppendMute(PlayerMute);

	if (TimeString.IsEmpty())
	{
		const FFormatNamedArguments Arguments{
			{ TEXT("Username"), FText::FromString(PlayerString) },
			{ TEXT("AdminReason"), FText::FromString(PlayerMute.AdminReason) },
			{ TEXT("UserReason"), FText::FromString(PlayerMute.UserReason) }
		};

		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdServerMuteSuccess"), Arguments);
	}
	else
	{
		const FFormatNamedArguments Arguments{
			{ TEXT("Time"), FText::FromString(TimeString) },
			{ TEXT("Username"), FText::FromString(PlayerString) },
			{ TEXT("AdminReason"), FText::FromString(PlayerMute.AdminReason) },
			{ TEXT("UserReason"), FText::FromString(PlayerMute.UserReason) }
		};

		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdServerMuteSuccessTime"), Arguments);
	}
}

FChatCommandResponse AIChatCommandManager::ServerUnmuteRCONCommand(TArray<FString> Params)
{
	FString PlayerString = TEXT("");

	if (Params.IsValidIndex(1))
	{
		PlayerString = Params[1];
	}
	else
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	FAlderonPlayerID PlayerId;
	if (AIPlayerState* IPlayerState = Cast<AIPlayerState>(PlayerStateFromUsername(this, PlayerString)))
	{
		PlayerId = IPlayerState->GetAlderonID();
	}
	else if (IsValidAlderonId(PlayerString))
	{
		PlayerId = FAlderonPlayerID(PlayerString);
	}
	else
	{
		return GetResponseCmdInvalidUsername(PlayerString);
	}

	AIGameSession* IGameSession = UIGameplayStatics::GetIGameSessionChecked(this);
	if (!IGameSession)
	{
		return GetResponseCmdNullObject(TEXT("IGameSession"));
	}

	FPlayerMute PlayerMute = FPlayerMute();
	PlayerMute.PlayerId = PlayerId;

	const FFormatNamedArguments Arguments{
		{ TEXT("Username"), FText::FromString(PlayerString) }
	};

	if (IGameSession->RemoveMute(PlayerMute))
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdServerUnmuteSuccess"), Arguments);
	}

	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdServerUnmuteNotMuted"), Arguments);
}

FChatCommandResponse AIChatCommandManager::WhitelistRCONCommand(TArray<FString> Params)
{
	FString PlayerString = TEXT("");

	if (Params.IsValidIndex(1))
	{
		PlayerString = Params[1];
	}
	else
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	FAlderonPlayerID PlayerId;
	if (AIPlayerState* IPlayerState = Cast<AIPlayerState>(PlayerStateFromUsername(this, PlayerString)))
	{
		PlayerId = IPlayerState->GetAlderonID();
	}
	else if (IsValidAlderonId(PlayerString))
	{
		PlayerId = FAlderonPlayerID(PlayerString);
	}
	else
	{
		return GetResponseCmdInvalidUsername(PlayerString);
	}

	AIGameSession* IGameSession = UIGameplayStatics::GetIGameSessionChecked(this);
	if (!IGameSession)
	{
		return GetResponseCmdNullObject(TEXT("IGameSession"));
	}

	const FFormatNamedArguments Arguments{
		{ TEXT("Username"), FText::FromString(PlayerString) }
	};

	if (IGameSession->IsPlayerWhitelisted(PlayerId))
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdWhitelistAlreadyWhitelisted"), Arguments);
	}

	IGameSession->AppendWhitelist(PlayerId);

	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdWhitelistSuccess"), Arguments);
}

FChatCommandResponse AIChatCommandManager::DelWhitelistRCONCommand(TArray<FString> Params)
{
	FString PlayerString = TEXT("");

	if (Params.IsValidIndex(1))
	{
		PlayerString = Params[1];
	}
	else
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	FAlderonPlayerID PlayerId;
	if (AIPlayerState* IPlayerState = Cast<AIPlayerState>(PlayerStateFromUsername(this, PlayerString)))
	{
		PlayerId = IPlayerState->GetAlderonID();
	}
	else if (IsValidAlderonId(PlayerString))
	{
		PlayerId = FAlderonPlayerID(PlayerString);
	}
	else
	{
		return GetResponseCmdInvalidUsername(PlayerString);
	}

	AIGameSession* IGameSession = UIGameplayStatics::GetIGameSessionChecked(this);
	if (!IGameSession)
	{
		return GetResponseCmdNullObject(TEXT("IGameSession"));
	}

	const FFormatNamedArguments Arguments{
		{ TEXT("Username"), FText::FromString(PlayerString) }
	};

	if (IGameSession->RemoveWhitelist(PlayerId))
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdDelWhitelistSuccess"), Arguments);
	}
	else
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdDelWhitelistNotWhitelisted"), Arguments);
	}
}

FChatCommandResponse AIChatCommandManager::ReloadBansRCONCommand(TArray<FString> Params)
{
	AIGameSession* IGameSession = UIGameplayStatics::GetIGameSessionChecked(this);
	if (!IGameSession)
	{
		return GetResponseCmdNullObject(TEXT("IGameSession"));
	}

	IGameSession->LoadBans();
	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdReloadBansSuccess"));
}

FChatCommandResponse AIChatCommandManager::ReloadMutesRCONCommand(TArray<FString> Params)
{
	AIGameSession* IGameSession = UIGameplayStatics::GetIGameSessionChecked(this);
	if (!IGameSession)
	{
		return GetResponseCmdNullObject(TEXT("IGameSession"));
	}

	IGameSession->LoadMutes();
	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdReloadMutesSuccess"));
}

FChatCommandResponse AIChatCommandManager::ReloadWhitelistRCONCommand(TArray<FString> Params)
{
	AIGameSession* IGameSession = UIGameplayStatics::GetIGameSessionChecked(this);
	if (!IGameSession)
	{
		return GetResponseCmdNullObject(TEXT("IGameSession"));
	}

	IGameSession->LoadWhitelist();
	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdReloadWhitelistSuccess"));
}

FChatCommandResponse AIChatCommandManager::ReloadMOTDRCONCommand(TArray<FString> Params)
{
	AIGameSession* IGameSession = UIGameplayStatics::GetIGameSessionChecked(this);
	if (!IGameSession)
	{
		return GetResponseCmdNullObject(TEXT("IGameSession"));
	}
	IGameSession->LoadServerMOTD();
	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdReloadMOTDSuccess"));
}

FChatCommandResponse AIChatCommandManager::ReloadRulesRCONCommand(TArray<FString> Params)
{
	AIGameSession* IGameSession = UIGameplayStatics::GetIGameSessionChecked(this);
	if (!IGameSession)
	{
		return GetResponseCmdNullObject(TEXT("IGameSession"));
	}
	IGameSession->LoadServerRules();
	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdReloadRulesSuccess"));
}

FChatCommandResponse AIChatCommandManager::ResetCreatorModeCommand(TArray<FString> Params)
{
	if (!IsRunningDedicatedServer() && !WITH_EDITOR)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdDedicatedOnly"));
	}

	AIGameMode* IGameMode = UIGameplayStatics::GetIGameModeChecked(this);
	if (!IGameMode)
	{
		return FChatCommandResponse();
	}

	if (Params.Num() != 1)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	IGameMode->ResetCreatorModeObjects();

	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdCreatorObjectsReset"));
}

FChatCommandResponse AIChatCommandManager::SaveCreatorModeCommand(TArray<FString> Params, FAsyncChatCommandCallback& Callback)
{
	if (!IsRunningDedicatedServer() && !WITH_EDITOR)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdDedicatedOnly"));
	}

	if (Params.Num() != 2)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	AIGameMode* IGameMode = UIGameplayStatics::GetIGameModeChecked(this);
	if (!IGameMode)
	{
		return FChatCommandResponse();
	}

	FString SaveSlot = Params[1];
	if (SaveSlot.IsEmpty() || SaveSlot.Contains(" ")) // Can't have spaces or be empty
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}
	if (SaveSlot.Contains(TEXT("/")) || SaveSlot.Contains(TEXT("\\"))) // Can't contain slashes
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdCreatorModeSaveInvalidName"));
	}
	IGameMode->SaveCreatorModeObjects(SaveSlot, Callback);

	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdCreatorObjectsSaving"));
}

FChatCommandResponse AIChatCommandManager::LoadCreatorModeCommand(TArray<FString> Params, FAsyncChatCommandCallback& Callback)
{
	if (!IsRunningDedicatedServer() && !WITH_EDITOR)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdDedicatedOnly"));
	}

	if (Params.Num() != 2)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	AIGameMode* IGameMode = UIGameplayStatics::GetIGameModeChecked(this);
	if (!IGameMode)
	{
		return FChatCommandResponse();
	}

	FString SaveSlot = Params[1];

	IGameMode->LoadCreatorModeObjects(SaveSlot, Callback);

	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdCreatorObjectsLoading"));
}

FChatCommandResponse AIChatCommandManager::RemoveCreatorModeSaveCommand(TArray<FString> Params, FAsyncChatCommandCallback& Callback)
{
	if (!IsRunningDedicatedServer() && !WITH_EDITOR)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdDedicatedOnly"));
	}

	if (Params.Num() != 2)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	AIGameMode* IGameMode = UIGameplayStatics::GetIGameModeChecked(this);
	if (!IGameMode)
	{
		return FChatCommandResponse();
	}

	FString SaveSlot = Params[1];

	IGameMode->RemoveCreatorModeObjects(SaveSlot, Callback);
	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdCreatorObjectsRemoving"));
}

FChatCommandResponse AIChatCommandManager::ListCreatorModeSavesCommand(TArray<FString> Params, FAsyncChatCommandCallback& Callback)
{
	if (!IsRunningDedicatedServer() && !WITH_EDITOR)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdDedicatedOnly"));
	}

	if (Params.Num() != 1)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	AIGameMode* IGameMode = UIGameplayStatics::GetIGameModeChecked(this);
	if (!IGameMode)
	{
		return FChatCommandResponse();
	}

	IGameMode->ListCreatorModeSaves(Callback);

	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdCreatorObjectsGettingList"));
}

#if WITH_VIVOX
FChatCommandResponse AIChatCommandManager::SwitchVoiceChannelCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	if (Params.Num() != 1)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	UIGameInstance* IGameInstance = UIGameplayStatics::GetIGameInstance(this);
	check(IGameInstance);
	UIVoiceSubsystem* VoiceSubsystem = UGameInstance::GetSubsystem<UIVoiceSubsystem>(IGameInstance);
	check(VoiceSubsystem);

	VoiceSubsystem->SwapChannel();

	const FFormatNamedArguments Arguments{
		{ TEXT("ChannelName"), FText::FromString(VoiceSubsystem->GetChannelName()) }
	};

	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdVoiceChatSwitchChannel"), Arguments);
}

FChatCommandResponse AIChatCommandManager::VoiceChatVolumeCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	if (Params.Num() != 3 || !Params[2].IsNumeric())
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	UIGameInstance* IGameInstance = UIGameplayStatics::GetIGameInstance(this);
	check(IGameInstance);

	UIVoiceSubsystem* VoiceSubsystem = UGameInstance::GetSubsystem<UIVoiceSubsystem>(IGameInstance);

	if (VoiceSubsystem && VoiceSubsystem->SetLocalVolume(Params[1], FCString::Atoi(*Params[2])))
	{
		const FFormatNamedArguments Arguments{
			{ TEXT("Username"), FText::FromString(Params[1]) },
			{ TEXT("Number"), FText::FromString(Params[2]) }
		};

		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdVoiceChatVolume"), Arguments);
	}
	else
	{
		// TODO: Determine if I should be returning a more specific error message.
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}
}

FChatCommandResponse AIChatCommandManager::VoiceChatMuteCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	if (Params.Num() != 2)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	UIGameInstance* IGameInstance = UIGameplayStatics::GetIGameInstance(this);
	check(IGameInstance);
	UIVoiceSubsystem* VoiceSubsystem = UGameInstance::GetSubsystem<UIVoiceSubsystem>(IGameInstance);
	check(VoiceSubsystem);

	if (VoiceSubsystem->SetLocalMute(Params[1], true))
	{
		const FFormatNamedArguments Arguments{
			{ TEXT("Username"), FText::FromString(Params[1]) }
		};

		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdVoiceChatMute"), Arguments);
	}
	else
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}
}

FChatCommandResponse AIChatCommandManager::VoiceChatUnmuteCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	if (Params.Num() != 2)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	UIGameInstance* IGameInstance = UIGameplayStatics::GetIGameInstance(this);
	check(IGameInstance);
	UIVoiceSubsystem* VoiceSubsystem = UGameInstance::GetSubsystem<UIVoiceSubsystem>(IGameInstance);
	check(VoiceSubsystem);

	if (VoiceSubsystem->SetLocalMute(Params[1], false))
	{
		const FFormatNamedArguments Arguments{
			{ TEXT("Username"), FText::FromString(Params[1]) }
		};

		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdVoiceChatUnmute"), Arguments);
	}
	else
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}
}

#endif

FChatCommandResponse AIChatCommandManager::ClearCooldownsCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	// one parameter needed
	if (Params.Num() != 1)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	// get CallingPlayer's pawn as AIBaseCharacter and return if not valid
	AIBaseCharacter* ICharacter = Cast<AIBaseCharacter>(CallingPlayer->GetPawn());
	if (!ICharacter)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdNoPawn"));
	}

	// get the ability system component and return if not valid
	UPOTAbilitySystemComponent* AbilitySystemComponent = Cast<UPOTAbilitySystemComponent>(ICharacter->GetAbilitySystemComponent());
	if (!AbilitySystemComponent)
	{
		return GetResponseCmdNullObject(TEXT("AbilitySystemComponent"));
	}

	AbilitySystemComponent->RemoveAllCooldowns();

	// return success message
	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdClearCooldowns"));
}

FChatCommandResponse AIChatCommandManager::ClearEffectsCommand(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	// one parameter needed
	if (Params.Num() != 1)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdIncorrectSyntax"));
	}

	// get CallingPlayer's pawn as AIBaseCharacter and return if not valid
	AIBaseCharacter* ICharacter = Cast<AIBaseCharacter>(CallingPlayer->GetPawn());
	if (!ICharacter)
	{
		return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdNoPawn"));
	}

	// get the ability system component and return if not valid
	UAbilitySystemComponent* AbilitySystemComponent = ICharacter->GetAbilitySystemComponent();
	if (!AbilitySystemComponent)
	{
		return GetResponseCmdNullObject(TEXT("AbilitySystemComponent"));
	}

	static FGameplayTagContainer ExclusionTags = FGameplayTagContainer();
	static bool bTagsInitialized = false;
	if (!bTagsInitialized)
	{
		ExclusionTags.AddTagFast(FGameplayTag::RequestGameplayTag(NAME_Active));
		bTagsInitialized = true;
	}

	// get all of the active effects which are not default/initial effects or state effects. i.e. buffs or debuffs
	FGameplayEffectQuery EffectQuery = FGameplayEffectQuery();
	EffectQuery.CustomMatchDelegate.BindLambda([&](const FActiveGameplayEffect& Effect) -> bool {
		const UGameplayEffect* EffectDef = Effect.Spec.Def;
		if (!EffectDef)
		{
			return false;
		}

		return EffectDef->InheritableGameplayEffectTags.Added.HasAny(ExclusionTags) || EffectDef->InheritableOwnedTagsContainer.Added.HasAny(ExclusionTags);
	});

	TArray<FActiveGameplayEffectHandle> ActiveEffectHandles = AbilitySystemComponent->GetActiveEffects(EffectQuery);

	// remove all of the active effects
	for (FActiveGameplayEffectHandle ActiveEffectHandle : ActiveEffectHandles)
	{
		UE_LOG(TitansLog, Log, TEXT("Removed %s"), *AbilitySystemComponent->GetActiveGameplayEffect(ActiveEffectHandle)->Spec.ToSimpleString());
		AbilitySystemComponent->RemoveActiveGameplayEffect(ActiveEffectHandle);
	}

	// return success message
	return AIChatCommand::MakeLocalizedResponse(TEXT("ST_ChatCommands"), TEXT("CmdClearEffects"));
}

FChatCommandResponse AIChatCommandManager::ClearMapFog(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	UMapRevealerComponent* const MapRevealerComponent = CallingPlayer->MapRevealerComponent;
	if (!MapRevealerComponent)
	{
		return GetResponseCmdNullObject(TEXT("MapRevealerComponent"));
	}

	const float RevealDropOffDistance = MapRevealerComponent->GetRevealDropOffDistance();
	MapRevealerComponent->SetRevealDropOffDistance(2000000);
	const TWeakObjectPtr<UMapRevealerComponent> MapRevealComponentWeakPtr(MapRevealerComponent);
	FTimerHandle TimerHandle{};
	GetWorldTimerManager().SetTimer(TimerHandle, FTimerDelegate::CreateUObject(this, &AIChatCommandManager::SetMapRevealerDropOffDistance, MapRevealComponentWeakPtr, RevealDropOffDistance), 0.5f, false);
	return AIChatCommand::MakePlainResponse(TEXT("Map Fog Cleared"));
}

void AIChatCommandManager::SetMapRevealerDropOffDistance(const TWeakObjectPtr<UMapRevealerComponent> MapRevealComponentWeakPtr, const float RevealDropOffDistance)
{
	if (!MapRevealComponentWeakPtr.IsValid())
	{
		return;
	}

	MapRevealComponentWeakPtr.Get()->SetRevealDropOffDistance(RevealDropOffDistance);
}

FChatCommandResponse AIChatCommandManager::RestoreMapFog(AIPlayerController* CallingPlayer, TArray<FString> Params)
{
	AMapFog* const MapFog = Cast<AMapFog>(UGameplayStatics::GetActorOfClass(this, AMapFog::StaticClass()));
	if (!MapFog)
	{
		return GetResponseCmdNullObject(TEXT("MapFog"));
	}

	MapFog->RestoreFog();
	return AIChatCommand::MakePlainResponse(TEXT("Map Fog Restored"));
}

FText AIChatCommandManager::GetUnknownCommandText()
{
	return FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdNoCommand"));
}

FText AIChatCommandManager::GetNullObjectText()
{
	return FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdNullObject"));
}

FText AIChatCommandManager::GetNoPermissionText()
{
	return FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdNoPermission"));
}

FText AIChatCommandManager::GetInvalidInputText()
{
	return FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdNoParams"));
}

FText AIChatCommandManager::GetHelpCommandDescriptionText()
{
	return FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdHelpDescription"));
}

void AttemptGenerateCommandExample(FString& InString)
{
	// This is slow af. But it's only for developer use. -Poncho

	static const TArray<TPair<FString, FString>> Examples = {
		{ TEXT("Username"), TEXT("Jiggy") },
		{ TEXT("Role"), TEXT("Moderator") },
		{ TEXT("Reason"), TEXT("\"Breaking rule number 12\"") },
		{ TEXT("Seconds"), TEXT("900") },
		{ TEXT("Message"), TEXT("\"Come to Corpse Cove!\"") },
		{ TEXT("SaveName"), TEXT("RockWorld") },
		{ TEXT("WoundCategory"), TEXT("HeadLeft") },
		{ TEXT("Value"), TEXT("0.5") },
		{ TEXT("XYZ"), TEXT("(X=600,Y=1000,Z=600)") },
		{ TEXT("Location"), TEXT("CorpseCove") },
		{ TEXT("QuestName"), TEXT("\"Collect Mushrooms\"") },
		{ TEXT("Time"), TEXT("1400") },
		{ TEXT("Weather"), TEXT("ClearSky") },
		{ TEXT("Attribute"), TEXT("Hunger") }
	};

	for (const TPair<FString, FString>& Example : Examples)
	{
		InString.ReplaceInline(*FString::Printf(TEXT("[%s]"), *Example.Key), *Example.Value);
		InString.ReplaceInline(*FString::Printf(TEXT("<%s>"), *Example.Key), *Example.Value);
	}
}

FChatCommandResponse AIChatCommandManager::DumpCommandsRCONCommand(TArray<FString> Params)
{
	FString Result = TEXT("Name | Example | Description | Permission | RCON Support | Hidden\n");
	Result.Reserve(8192);

	const bool bShowHidden = Params.IsValidIndex(1) && Params[1] == TEXT("ShowHidden");

	for (const TPair<FString, AIChatCommand*>& ChatCommandPair : ChatCommands)
	{
		const AIChatCommand* const ChatCommand = ChatCommandPair.Value;
		if (!ChatCommand || (!bShowHidden && ChatCommand->bIsHidden))
		{
			continue;
		}

		const FString PermissionString = ChatCommand->Permission.IsEmpty() ? ChatCommand->CommandName : ChatCommand->Permission;
		const bool bHasRCON = ChatCommand->HasRCONVariant() || ChatCommand->HasAsyncRCONVariant();
		FString NameString = ChatCommand->CommandName;
		FString ExampleString = TEXT("");
		FString DescriptionString = TEXT("");

		// Try to separate the description text into an example & a proper description.
		if (ChatCommand->Description.ToString().Split(TEXT(";"), &ExampleString, &DescriptionString))
		{
			while (ExampleString.RemoveFromStart(TEXT(" ")))
			{
			}
			while (DescriptionString.RemoveFromStart(TEXT(" ")))
			{
			}
			NameString = ExampleString;
			ExampleString.InsertAt(0, TEXT("/"));
			AttemptGenerateCommandExample(ExampleString);
		}
		else
		{
			// Could not get example & description.
			ExampleString = TEXT("No Example");
			DescriptionString = ChatCommand->Description.ToString();
		}

		Result.Append(FString::Printf(
			TEXT("`/%s` | `%s` | %s | %s | %s | %s\n"),
			*NameString,
			*ExampleString,
			*DescriptionString,
			(ChatCommand->bRequiresPermission ? *PermissionString : TEXT("")),
			(bHasRCON ? TEXT("RCON") : TEXT("No RCON")),
			(ChatCommand->bIsHidden ? TEXT("Hidden") : TEXT("Not Hidden"))));
	}

	return AIChatCommand::MakePlainResponse(MoveTemp(Result));
}

#undef CLAMP_MINMAX