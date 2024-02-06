// Copyright 2019-2022 Alderon Games Pty Ltd, All Rights Reserved.

#include "GameMode/IGameMode.h"
#include "AIController.h"
#include "Player/IPlayerController.h"
#include "Online/IPlayerState.h"
#include "Online/IGameState.h"
#include "UI/IHUD.h"
#include "ITypes.h"
#include "Player/ISpectatorPawn.h"
#include "Player/Dinosaurs/IDinosaurCharacter.h"
#include "Misc/ScopeExit.h"
#include "World/IPlayerStart.h"
#include "World/ICharSelectPoint.h"
#include "IWorldSettings.h"
#include "Online/IPlayerState.h"
#include "Online/IGameSession.h"
#include "IGameEngine.h"
#include "IGameInstance.h"
#include "Online/IGameSession.h"
#include "Quests/IQuestManager.h"
#include "World/IGameSingleton.h"
#include "World/INest.h"
#include "IWorldSettings.h"
#include "ILevelSummary.h"
#include "Abilities/POTAbilitySystemComponent.h"
#include "AgonesComponent.h"
#include "TitanAssetManager.h"
#include "AlderonDatabaseBase.h"
#include "Player/IAdminCharacter.h"
#include "World/IPlayerStart.h"
#include "Components/ICreatorModeObjectComponent.h"
#include "Net/IVoiceSubsystem.h"
#include "HttpManager.h"
#include "Online/IPlayerGroupActor.h"
#include "Components/ICharacterMovementComponent.h"
#include "CaveSystem/IPlayerCaveBase.h"
#include "CaveSystem/IHatchlingCave.h"
#include "AlderonRemoteConfig.h"
#include "Abilities/POTAbilitySystemGlobals.h"
#include "Abilities/CoreAttributeSet.h"
#include "Abilities/POTAbilityAsset.h"
#include "UI/IAbilitySlotsEditor.h"
#include "Connection.h"
#include "World/IAnimationUpdateManager.h"
#include "CaveSystem/HomeCaveExtensionDataAsset.h"
#include "World/IGameplayAbilityVolume.h"
#include "World/IUltraDynamicSky.h"
#include "Modding/IModData.h"
#include "AlderonReplay.h"
#include "AlderonAnalytics.h"

#if WITH_BATTLEYE_SERVER
	#include "IBattlEyeServer.h"
#endif

static const FName NAME_HandleSpawnCharacter(TEXT("HandleSpawnCharacter"));

AIGameMode::AIGameMode(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	// Assign the class types used by this game mode 
	PlayerControllerClass = AIPlayerController::StaticClass();
	PlayerStateClass = AIPlayerState::StaticClass();
	GameStateClass = AIGameState::StaticClass();
	SpectatorClass = AISpectatorPawn::StaticClass();

	// Fixes issue with bPlayerValidated not being true - Nixon
	MaxInactivePlayers = 0;

	// Database
	bDatabaseSetup = false;

	DisplayName = TEXT("Unknown");
	bRelaunchRequired = false;

	CharSelectPoint = nullptr;
	LastRestartNoticeTimestamp = -1;

	bUseSeamlessTravel = true;

	FString EdgegapContextUrl = FPlatformMisc::GetEnvironmentVariable(TEXT("ARBITRIUM_CONTEXT_URL"));
	EdgegapContextUrl.TrimQuotesInline();

	if (!EdgegapContextUrl.IsEmpty())
	{
		ManagedServerType = EManagedServer::Edgegap;
	}

#if UE_SERVER
	if (FParse::Param(FCommandLine::Get(), TEXT("agones")))
	{
		AgonesSDK = CreateDefaultSubobject<UAgonesComponent>(TEXT("AgonesSDK"));
		ManagedServerType = EManagedServer::Agones;
	}

	//if (FParse::Param(FCommandLine::Get(), TEXT("edgegap")))
	//{
	//	ManagedServerType = EManagedServer::Edgegap;
	//}
#endif
}

UAlderonMetricEvent* AIGameMode::CreateMetricEventForPlayer(const FString& EventName, AIPlayerState* IPlayerState)
{
	UAlderonAnalytics& AlderonAnalytics = IAlderonCommon::Get().GetAnalyticsInterface();
	UAlderonMetricEvent* MetricEvent = AlderonAnalytics.CreateMetricEvent(EventName, IPlayerState->GetAlderonID(), PlatformToServerApiString(IPlayerState->GetPlatform()), TEXT(""));
	check(MetricEvent);

	// Add Optional Fields we might always want
	AIPlayerController* IPlayerController = Cast<AIPlayerController>(IPlayerState->GetOwningController());
	if (IPlayerController)
	{
		MetricEvent->SetStringValue(TEXT("Location"), IPlayerController->GetMapBug());

		AIBaseCharacter* IBaseCharacter = Cast<AIBaseCharacter>(IPlayerController->GetPawn());
		if (IBaseCharacter)
		{
			if (IBaseCharacter->CharacterDataAssetId.IsValid())
			{
				MetricEvent->SetStringValue(TEXT("CharacterAssetId"), IBaseCharacter->CharacterDataAssetId.ToString());
			}
		}
	}

	return MetricEvent;
}

AIPlayerState* AIGameMode::FindPlayerStateViaAGID(const FAlderonPlayerID& AlderonId)
{
	AIGameState* IGameState = UIGameplayStatics::GetIGameState(this);
	if (IGameState)
	{
		for (APlayerState* PlayerState : IGameState->PlayerArray)
		{
			AIPlayerState* IPlayerState = Cast<AIPlayerState>(PlayerState);
			if (IPlayerState && IPlayerState->GetAlderonID() == AlderonId)
			{
				return IPlayerState;
			}
		}
	}

	return nullptr;
}

TSubclassOf<AGameSession> AIGameMode::GetGameSessionClass() const
{
	return AIGameSession::StaticClass();
}

void AIGameMode::BeginPlay()
{
	// Setup Database if it isn't already setup
	SetupDatabase();

	// Load Nests
	LoadAllNests();

	Super::BeginPlay();

	UWorld* World = GetWorld();
	check(World);

#if UE_SERVER
	UIGameInstance* GI = Cast<UIGameInstance>(GetGameInstance());
	check(GI);

	AIGameSession* Session = Cast<AIGameSession>(GI->GetGameSession());
	check(Session);

	if (Session->bServerAnimationManager)
	{
		AIWorldSettings* IWorldSettings = AIWorldSettings::GetWorldSettings(this);
		check(IWorldSettings);

		TArray<AActor*> AnimationManagers;
		UGameplayStatics::GetAllActorsOfClass(World, AIAnimationUpdateManager::StaticClass(), AnimationManagers);

		if (AnimationManagers.Num() == 0)
		{
			World->SpawnActor<AIAnimationUpdateManager>();
		}
		else if (AnimationManagers.Num() > 1)
		{
			int32 i = 0;
			for (AActor* AUM: AnimationManagers)
			{
				if (AIAnimationUpdateManager* IAUM = Cast<AIAnimationUpdateManager>(AUM))
				{
					i++;

					if (i > 0)
					{
						IAUM->Destroy();
					}
				}
			}
		}
	}
#endif

	// Spawn the chat command manager in the world
	AIChatCommandManager* IChatCommandManager = World->SpawnActor<AIChatCommandManager>();

#if UE_SERVER
	if (AgonesSDK)
	{
		UE_LOG(TitansNetwork, Log, TEXT("IGameMode:BeginPlay: Starting connection to agones.."));
		Agones_CallGameServer();
	}

	if (ManagedServerType == EManagedServer::Edgegap)
	{
		FString ArbitriumUrl = FPlatformMisc::GetEnvironmentVariable(TEXT("ARBITRIUM_CONTEXT_URL"));
		FString ArbitriumToken = FPlatformMisc::GetEnvironmentVariable(TEXT("ARBITRIUM_CONTEXT_TOKEN"));

		UE_LOG(TitansNetwork, Log, TEXT("IGameMode: Edgegap Server Detected: Context: %s Token: %s"), *ArbitriumUrl, *ArbitriumToken);

		FHttpRequestPtr Request = IAlderonCommon::CreateAuthRequest(TEXT("GET"));
		Request->SetURL(ArbitriumUrl);

		Request->SetHeader(TEXT("Authorization"), ArbitriumToken);

		Request->OnProcessRequestComplete().BindUObject(this, &AIGameMode::Edgegap_ContextCallback);
		Request->ProcessRequest();
	}

	IAlderonCommon& AlderonCommon = IAlderonCommon::Get();
	AlderonCommon.SetRestartServerDelegate(FAlderonRestartServer::CreateUObject(this, &AIGameMode::StartRestartTimer));
#endif
}

void AIGameMode::Edgegap_ContextCallback(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	UE_LOG(LogTemp, Log, TEXT("AIGameMode:Edgegap_ContextCallback()"));

	FString Result = "";
	int ReturnCode = 0;

	if (Response.IsValid())
	{
		Result = Response->GetContentAsString();
		ReturnCode = Response->GetResponseCode();
	}

	// Handle Failure
	if (!bWasSuccessful || !EHttpResponseCodes::IsOk(ReturnCode))
	{
		UE_LOG(LogTemp, Error, TEXT("AIGameMode:Edgegap_ContextCallback: Failed - %s"), *Result);
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("AIGameMode:Edgegap_ContextCallback[%i]: Result - %s"), ReturnCode, *Result);

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Result);
	if (!FJsonSerializer::Deserialize(Reader, JsonObject))
	{
		UE_LOG(LogTemp, Error, TEXT("AIGameMode:Edgegap_ContextCallback: Json Deserialize Failure - %s"), *Result);
		return;
	}

	FString RequestId = JsonObject->GetStringField(TEXT("request_id"));
	FString PublicIp = JsonObject->GetStringField(TEXT("public_ip"));
	FString FQdn = JsonObject->GetStringField(TEXT("fqdn"));

	TSharedPtr<FJsonObject> PortsObject = JsonObject->GetObjectField(TEXT("ports"));
	
	TMap<FString, TSharedPtr<FJsonValue>> Values;

	TArray<FString> AttributeKeys;
	PortsObject->Values.GetKeys(AttributeKeys);

	AIGameSession* IGameSession = Cast<AIGameSession>(GameSession);
	if (!IGameSession)
	{
		UE_LOG(LogTemp, Error, TEXT("AIGameMode:Edgegap_ContextCallback: Json Deserialize Failure - %s"), *Result);
		return;
	}

	for (const FString& AttributeName : AttributeKeys)
	{
		TArray<FString> AddedValues;
		TSharedPtr<FJsonObject> PortObject = PortsObject->GetObjectField(AttributeName);
		int32 External = PortObject->GetNumberField(TEXT("external"));
		int32 Internal = PortObject->GetNumberField(TEXT("internal"));

		if (Internal == IGameSession->GetNetworkPort(ENetworkPortType::Primary))
		{
			IGameSession->SetServerListIP(PublicIp);
			IGameSession->SetServerListPort(External);

			bManagedServerConnected = true;

			IGameSession->CheckServerRegisterReady();

			break;
		}
	}
}

void AIGameMode::Agones_CallGameServer()
{
	UE_LOG(TitansNetwork, Log, TEXT("IGameMode:Agones_CallGameServer"));

	check(AgonesSDK);

	FGameServerDelegate SuccessDel;
	SuccessDel.BindUFunction(this, FName("Agones_GameServerSuccess"));
	FAgonesErrorDelegate FailureDel;
	FailureDel.BindUFunction(this, FName("Agones_GameServerError"));
	AgonesSDK->GameServer(SuccessDel, FailureDel);
}

void AIGameMode::Agones_CallAllocate()
{
	UE_LOG(TitansNetwork, Log, TEXT("IGameMode:Agones_CallAllocate"));

	FGameServerDelegate SuccessDel;
	SuccessDel.BindUFunction(this, FName("Agones_GameServer"));
	AgonesSDK->GameServer(SuccessDel, {});
}

void AIGameMode::Agones_CallReady()
{
	UE_LOG(TitansNetwork, Log, TEXT("IGameMode:Agones_CallReady"));

	FReadyDelegate SuccessDel;
	SuccessDel.BindUFunction(this, FName("Agones_ReadySuccess"));

	FAgonesErrorDelegate FailureDel;
	FailureDel.BindUFunction(this, FName("Agones_ReadyError"));

	AgonesSDK->Ready(SuccessDel, FailureDel);
}

void AIGameMode::Agones_Allocated()
{
	UE_LOG(TitansNetwork, Log, TEXT("IGameMode:Agones_Allocated()"));

	//FGameServerDelegate SuccessDel;
	//SuccessDel.BindUFunction(this, FName("Agones_GameServer"));
	//AgonesSDK->GameServer(SuccessDel, {});
}

void AIGameMode::Agones_GameServerError(const FAgonesError& Error)
{
	UE_LOG(TitansNetwork, Error, TEXT("IGameMode:Agones_GameServerError: %s"), *Error.ErrorMessage);
	Agones_CallGameServer();
}

void AIGameMode::Agones_GameServerSuccess(const FGameServerResponse& Response)
{
	UE_LOG(TitansNetwork, Log, TEXT("IGameMode:Agones_GameServerSuccess() Status: %s Address: %s"), *Response.Status.State, *Response.Status.Address);

	if (!bManagedServerReady)
	{
		Agones_CallReady();
		return;
	}

	if (Response.Status.Address.IsEmpty() || Response.Status.Ports.Num() == 0)
	{
		UE_LOG(TitansNetwork, Log, TEXT("IGameMode:Agones_GameServer(): No Port or Address Allocated yet"));
		Agones_CallGameServer();
		return;
	}

	AIGameSession* IGameSession = Cast<AIGameSession>(GameSession);
	check(IGameSession);
	IGameSession->SetServerListIP(Response.Status.Address);

	int PrimaryNetworkPort = IGameSession->GetNetworkPort();

	for (const FPort& AgonesPort : Response.Status.Ports)
	{
		if (AgonesPort.Port == PrimaryNetworkPort)
		{
			IGameSession->SetServerListPort(AgonesPort.Port);
			break;
		}
	}

	bManagedServerConnected = true;

	IGameSession->CheckServerRegisterReady();
}

void AIGameMode::Agones_ReadySuccess(const FEmptyResponse& Response)
{
	UE_LOG(TitansNetwork, Error, TEXT("IGameMode:Agones_ReadySuccess()"));
	bManagedServerReady = true;
	Agones_CallGameServer();
}

void AIGameMode::Agones_ReadyError(const FAgonesError& Error)
{
	UE_LOG(TitansNetwork, Error, TEXT("IGameMode:Agones_ReadyError: %s"), *Error.ErrorMessage);
	Agones_CallReady();
}

void AIGameMode::LoadAllNests()
{
}

void AIGameMode::SetupDatabase()
{
	UE_LOG(TitansNetwork, Log, TEXT("IGameMode::SetupDatabase()"));

	// Mutex Lock to avoid database being setup multiple times
	if (bDatabaseSetup) return;
	
	bDatabaseSetup = true;

	IAlderonDatabase& AlderonDatabase = IAlderonCommon::Get().GetDatabaseInterface();

	FString DatabaseType;
	bool bCustomDatabaseType = FParse::Value(FCommandLine::Get(), TEXT("Database="), DatabaseType);

	if (bCustomDatabaseType)
	{
		if (DatabaseType.ToLower() == TEXT("remote"))
		{
			DatabaseEngine = AlderonDatabase.CreateHandle(EDatabaseType::Remote, this);
		}
		else if (DatabaseType.ToLower() == TEXT("save") || DatabaseType.ToLower() == TEXT("savegame"))
		{
			DatabaseEngine = AlderonDatabase.CreateHandle(EDatabaseType::SaveGame, this);
		}
		else if (DatabaseType.ToLower() == TEXT("fs") || DatabaseType.ToLower() == TEXT("filesystem") || DatabaseType.ToLower() == TEXT("file"))
		{
			DatabaseEngine = AlderonDatabase.CreateHandle(EDatabaseType::FileSystem, this);
		}
	}

	if (!DatabaseEngine)
	{
		DatabaseEngine = AlderonDatabase.CreateHandle((PLATFORM_CONSOLE) ? EDatabaseType::SaveGame : EDatabaseType::FileSystem, this);
	}

	DatabaseEngine->Init(ALDERON_DB_VERSION);

	// Set Allowed Characters
	AIWorldSettings* IWorldSettings = AIWorldSettings::GetWorldSettings(this);
	check(IWorldSettings);
	UILevelSummary* LevelSummary = IWorldSettings->GetLevelSummary();
	check(LevelSummary);

	const TArray<FPrimaryAssetId>& AllowedCharacters = LevelSummary->GetAllowedCharacters(IWorldSettings);

	bool bRestrictedCharacters = (AllowedCharacters.Num() > 0);

	if (bRestrictedCharacters)
	{
		AIGameState* IGameState = GetGameState<AIGameState>();
		check(IGameState);
		IGameState->GetAllowedCharacters_Mutable() = AllowedCharacters;
	}
}

void AIGameMode::ShutdownDatabase()
{
	// Shutdown New Database
	if (DatabaseEngine)
	{
		DatabaseEngine->Shutdown();
		DatabaseEngine->ConditionalBeginDestroy();
		DatabaseEngine = nullptr;
		bDatabaseSetup = false;
	}
}

void AIGameMode::PostLogin(APlayerController* NewPlayer)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("AIGameMode::PostLogin"))

	// Race Condition (Database Setup)
	SetupDatabase();

	Super::PostLogin(NewPlayer);

	// Determine IPlayerController and IPlayerState
	AIPlayerController* IPlayerController = Cast<AIPlayerController>(NewPlayer);
	if (!IsValid(IPlayerController)) return;
	AIPlayerState* IPlayerState = Cast<AIPlayerState>(NewPlayer->PlayerState);
	if (!IsValid(IPlayerState)) return;

	// Check World Settings
	AIWorldSettings* IWorldSettings = Cast<AIWorldSettings>(GetWorldSettings());
	check(IWorldSettings);

	// Update Time of Day
	AIGameState* IGameState = Cast<AIGameState>(GameState);
	if (IGameState)
	{
		IGameState->UpdateTimeOfDay();
	}

#if !UE_SERVER
	if (!IsRunningDedicatedServer())
	{
		// Demo Spectator Map, we don't want to load characters here
		if (IWorldSettings->GetLevelSummary()->bDemoSpectatorMap)
		{
			return;
		}
	}
#endif

	// Send Client info about our server key and if we are running any anti cheats
	{
		AIGameSession* IGameSession = UIGameplayStatics::GetIGameSession(this);
		if (IGameSession)
		{
			const FAlderonServerKey& ServerKey = IAlderonCommon::Get().GetServerKey();
			bool bUseBattlEye = false;

#if WITH_BATTLEYE_SERVER
			if (IsRunningDedicatedServer())
			{
				if (IBattlEyeServer::IsAvailable())
				{
					bUseBattlEye = IBattlEyeServer::Get().IsEnforcementEnabled();
				}
			}
#endif

			IPlayerController->ClientServerKeyInfo(ServerKey, bUseBattlEye);
		}
	}

	// Load Existing account if we have one
	check(DatabaseEngine);
	DatabaseEngine->LoadPlayerState(IPlayerState, IPlayerState->GetAlderonID(), FDatabaseOperationCompleted::CreateUObject(this, &AIGameMode::LoadPlayerStateCompleted, IPlayerController));
}

void AIGameMode::LoadPlayerStateCompleted(const FDatabaseOperationData& Data, AIPlayerController* IPlayerController)
{
	if (!IsInGameThread())
	{
		UE_LOG(LogTemp, Error, TEXT("AIGameMode::LoadPlayerStateCompleted called from non game thread."));
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("AIGameMode::LoadPlayerStateCompleted"))

	// We have to use IsValidLowLevel here due to IsValid Crashing
	if (!IPlayerController->IsValidLowLevel()) return;

	AIPlayerState* IPlayerState = IPlayerController->GetPlayerState<AIPlayerState>();
	if (!IPlayerState->IsValidLowLevel()) return;
	
	// If we fail to load the player state we need to kick the player from the server
	if (!Data.bSuccess)
	{
		AIGameSession* IGameSession = Cast<AIGameSession>(GameSession);
		if (IGameSession)
		{
			IGameSession->KickPlayer(IPlayerController, FText::FromString(TEXT("Failed to Load Account Data: ") + Data.ErrorMessage));
			return;
		}
	}

	// Post Player State Load Migrations and Data Fixes	
	{
		bool bFixApplied = false;
		TArray<FSkinUnlockData>& UnlockedSkins = IPlayerState->GetUnlockedSkins_Mutable();

		// Clear Null Skins that didn't save properly
		{
			for (int32 Index = UnlockedSkins.Num() - 1; Index >= 0; --Index)
			{
				const FSkinUnlockData& UnlockData = UnlockedSkins[Index];
				//UE_LOG(LogTemp, Log, TEXT("IGameMode:LoadPlayerStateCompleted: Index: %i / Player: %s / Skin: %s"), i, *IPlayerState->GetAlderonID().ToDisplayString(), *UnlockData->GetPrimaryAssetId().ToString());

				if (!UnlockData.IsValid())
				{
					UnlockedSkins.RemoveAt(Index, 1, false);
					bFixApplied = true;
					UE_LOG(LogTemp, Warning, TEXT("IGameMode:LoadPlayerStateCompleted: Removing Invalid Skin At Index: %i for player: %s"), Index, *IPlayerState->GetAlderonID().ToDisplayString());
				}
			}
			
			UnlockedSkins.Shrink();
		}

		// Clear Invalid Character Ids
		{
			for (int i = IPlayerState->Characters.Num(); i-- > 0; )
			{
				FAlderonUID CharacterId = IPlayerState->Characters[i];
				if (!CharacterId.IsValid())
				{
					IPlayerState->Characters.RemoveAt(i,1,false);
					bFixApplied = true;
				}
			}
			IPlayerState->Characters.Shrink();
		}

		// Migrate Skins that have a redirector (PrimaryAssetId Rename)
		{
			UAssetManager& AssetManager = UAssetManager::Get();

			for (int32 Index = UnlockedSkins.Num() - 1; Index >= 0; --Index)
			{
				FSkinUnlockData& UnlockData = UnlockedSkins[Index];

				FPrimaryAssetId CurrentAssetId = UnlockData.GetPrimaryAssetId();
				FPrimaryAssetId RedirectedAssetId = AssetManager.GetRedirectedPrimaryAssetId(UnlockData.GetPrimaryAssetId());

				if (RedirectedAssetId.IsValid() && CurrentAssetId != RedirectedAssetId)
				{
					UnlockData.SetPrimaryAssetId(RedirectedAssetId);
					bFixApplied = true;
					UE_LOG(LogTemp, Warning, TEXT("IGameMode:LoadPlayerStateCompleted: Applying Asset Redirect %s to %s for %s"), *CurrentAssetId.ToString(), *RedirectedAssetId.ToString(), *IPlayerState->GetAlderonID().ToDisplayString());
				}
			}
		}

		if (bFixApplied)
		{
			SavePlayerState(IPlayerState, ESavePriority::Medium);
		}
	}

	LoadCharacter(IPlayerController);
}

void AIGameMode::PreLogout(APlayerController* Controller, APawn* Pawn)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("AIGameMode::PreLogout"))

	//UE_LOG(LogTemp, Warning, TEXT("AIGameMode::PreLogout()"));

	AIPlayerController* IPlayerController = Cast<AIPlayerController>(Controller);
	if (!IsValid(IPlayerController))
	{
		UE_LOG(LogTemp, Warning, TEXT("AIGameMode::PreLogout() - !IPlayerController || IPlayerController->IsPendingKill()"));
		return;
	}

	AIBaseCharacter* IBaseCharacter = Cast<AIBaseCharacter>(Pawn);
	if (!IsValid(IBaseCharacter))
	{
		UE_LOG(LogTemp, Warning, TEXT("AIGameMode::PreLogout() - !IBaseCharacter || IBaseCharacter->IsPendingKill()"));
		return;
	}

	// Remove Group Quest and Trophy Deliveries as we're no longer in a group nor have a carriable - ANixon
	TArray<UIQuest*> ActiveQuests = IBaseCharacter->GetActiveQuests();
	for (int i = ActiveQuests.Num(); i-- > 0; )
	{
		UIQuest* ActiveQuest = ActiveQuests[i];

		if (!ActiveQuest) continue;
		if (!ActiveQuest->QuestData) continue;

		if (ActiveQuest->GetPlayerGroupActor() || ActiveQuest->QuestData->QuestType == EQuestType::TrophyDelivery)
		{
			ActiveQuests.Remove(ActiveQuest);
		}
	}

	if (ActiveQuests.Num() != IBaseCharacter->GetActiveQuests().Num())
	{
		IBaseCharacter->GetActiveQuests_Mutable() = ActiveQuests;
	}

#if !UE_SERVER
	if (!IsRunningDedicatedServer())
	{
		IBaseCharacter->OnRep_ActiveQuests();
	}
#endif

	AIPlayerState* IPlayerState = IPlayerController->GetPlayerState<AIPlayerState>();

	SaveAll(IPlayerController, ESavePriority::High);

	// Dont add combat log to admin character!! 
	// also don't use combat log inside instances, it's safe to log out instantly
	bool bShouldStartCombatLog = true;
	if (Cast<AIAdminCharacter>(IBaseCharacter) || IBaseCharacter->GetCurrentInstance())
	{
		bShouldStartCombatLog = false;
	}

	bool bSpawnCombatLogAI = (IPlayerController->bCombatLogAI || IBaseCharacter->IsInCombat());

	if (bSpawnCombatLogAI && IPlayerState && bShouldStartCombatLog)
	{
		if (!IBaseCharacter->IsCombatLogAI())
		{
			AddCombatLogAI(IBaseCharacter, IBaseCharacter->GetCharacterID());
			if (IPlayerState)
			{
				FlagRevengeKill(IBaseCharacter->GetCharacterID(), IPlayerState, IBaseCharacter->GetActorLocation());
			}
		}
	} else {
		IBaseCharacter->Destroy();
	}
}

void AIGameMode::Logout(AController* Exiting)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("AIGameMode::Logout"))

	// We shouldn't need this because the character should be saved in prelogout(). This is called too late

	//UE_LOG(LogTemp, Warning, TEXT("AIGameMode::Logout()"));
#if WITH_VIVOX
	UIGameInstance* IGameInstance = UIGameplayStatics::GetIGameInstance(this);
	UIVoiceSubsystem* VoiceSubsystem = UGameInstance::GetSubsystem<UIVoiceSubsystem>(IGameInstance);
	if (VoiceSubsystem) {
		VoiceSubsystem->OnServerDisconnected();
	}
#endif
	
	Super::Logout(Exiting);
}

void AIGameMode::AddInactivePlayerBlueprint(APlayerState* PlayerState, APlayerController* PC)
{
	Super::AddInactivePlayer(PlayerState, PC);
}

void AIGameMode::AddInactivePlayer(APlayerState* PlayerState, APlayerController* PC)
{
	Super::AddInactivePlayer(PlayerState, PC);

	//UE_LOG(LogTemp, Error, TEXT("AIGameMode::AddInactivePlayer()"));
}

bool AIGameMode::SaveAllPlayers()
{
//#if !WITH_EDITOR
//	// Search for all players
//	TArray<AActor*> FoundPlayers;
//	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AIPlayerController::StaticClass(), FoundPlayers);
//	
//	// Cast from Actor and save all players
//	for (AActor* Actor : FoundPlayers)
//	{
//		AIPlayerController* PC = Cast<AIPlayerController>(Actor);
//		if (PC && PC->IsValidLowLevel())
//		{
//			SaveCharacter(PC);
//		}
//	}
//#endif

	return true;
}

bool AIGameMode::SaveAllNests()
{
	UE_LOG(LogTemp, Warning, TEXT("SaveAllNests()"));
	//// Search for all nests
	//TArray<AActor*> FoundNests;
	//UGameplayStatics::GetAllActorsOfClass(GetWorld(), AINest::StaticClass(), FoundNests);
	//
	//// Cast from Actor and save all nests
	//for (AActor* Actor : FoundNests)
	//{
	//	AINest* Nest = Cast<AINest>(Actor);
	//	if (Nest)
	//	{
	//		SaveNest(Nest);
	//	}
	//}

	return true;
}

void AIGameMode::CreateCharacter(AIPlayerController* PlayerController, FString Name, const UCharacterDataAsset* CharacterDataAsset, const USkinDataAsset* SkinDataAsset, bool bGender, float Growth, FAsyncCharacterCreated OnCompleted)
{
	check(CharacterDataAsset);
	if (!CharacterDataAsset || !SkinDataAsset)
	{
		OnCompleted.ExecuteIfBound(PlayerController, FAlderonUID());
		if (PlayerController)
		{
			PlayerController->ServerCreateCharacterReplyFail(FText::FromStringTable(TEXT("ST_ErrorMessages"), TEXT("InvalidCharacterData")));
		}
		return;
	}

	FSelectedSkin SelectedSkin = FSelectedSkin();
	SelectedSkin.CharacterAssetId = CharacterDataAsset->GetPrimaryAssetId();
	SelectedSkin.SkinAssetId = SkinDataAsset->GetPrimaryAssetId();
	SelectedSkin.bGender = bGender;
	FCharacterData CharacterData = FCharacterData();
	CharacterData.SelectedSkin = SelectedSkin;
	CharacterData.Name = Name;
	CharacterData.Growth = Growth;
	CreateCharacterAsync(PlayerController, CharacterData, OnCompleted);
}

void AIGameMode::CreateCharacterAsync(AIPlayerController* PlayerController, FCharacterData CharacterData, FAsyncCharacterCreated OnCompleted)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("AIGameMode::CreateCharacterAsync"))

	if (!ensure(PlayerController))
	{
		UE_LOG(TitansNetwork, Error, TEXT("AIGameMode::CreateCharacterAsync: PlayerController is null"));
		return;
	}

	AIPlayerState* IPlayerState = PlayerController->GetPlayerState<AIPlayerState>();
	if (!IPlayerState)
	{
		UE_LOG(TitansNetwork, Log, TEXT("AIGameMode::CreateCharacterAsync: IPlayerState is null"));
		PlayerController->ServerCreateCharacterReplyFail();
		return;
	}

	TArray<FCharacterData> Characters;
	IPlayerState->CharactersData.GenerateValueArray(Characters);
	if (Characters.Num() >= UIGameplayStatics::GetMaxCharacterLimit(PlayerController))
	{
		UE_LOG(TitansNetwork, Log, TEXT("AIGameMode::CreateCharacterAsync: Max Character Limit Reached"));
		PlayerController->ServerCreateCharacterReplyFail(FText::FromStringTable(TEXT("ST_ErrorMessages"), TEXT("CharacterLimitReached")));
		return;
	}

	if (UIGameplayStatics::GetCharacterSpeciesCount(this, Characters, CharacterData.SelectedSkin.CharacterAssetId) >= UIGameplayStatics::GetMaxCharacterSpeciesLimit(PlayerController))
	{
		UE_LOG(TitansNetwork, Log, TEXT("AIGameMode::CreateCharacterAsync: Max Character Species Limit Reached"));
		PlayerController->ServerCreateCharacterReplyFail(FText::FromStringTable(TEXT("ST_ErrorMessages"), TEXT("SpeciesLimitReached")));
		return;
	}

	TWeakObjectPtr<AIPlayerController> PlayerControllerWeak = PlayerController;

	FCharacterDataSkinLoaded PostLoadDelegate = FCharacterDataSkinLoaded::CreateLambda([this, PlayerControllerWeak, CharacterData, OnCompleted](bool bSuccess, FPrimaryAssetId CharacterAssetId, UCharacterDataAsset* CharacterDataAsset, TArray<FPrimaryAssetId> SkinAssetIds, TArray<USkinDataAsset*> SkinDataAssets, TSharedPtr<FStreamableHandle> Handle)
	{
		if (!PlayerControllerWeak.Get())
		{
			UE_LOG(TitansNetwork, Log, TEXT("AIGameMode::CreateCharacterAsync: PlayerControllerWeak is invalid"));
			return;
		}
		check(SkinDataAssets.Num() > 0);
		if (SkinDataAssets.Num() == 0)
		{
			UE_LOG(TitansNetwork, Log, TEXT("AIGameMode::CreateCharacterAsync: SkinDataAsset is null"));
			PlayerControllerWeak->ServerCreateCharacterReplyFail(FText::FromStringTable(TEXT("ST_ErrorMessages"), TEXT("InvalidSkinData")));
			return;
		}

		check(CharacterDataAsset);
		if (!CharacterDataAsset)
		{
			UE_LOG(TitansNetwork, Log, TEXT("AIGameMode::CreateCharacterAsync: CharacterDataAsset is null"));
			PlayerControllerWeak->ServerCreateCharacterReplyFail(FText::FromStringTable(TEXT("ST_ErrorMessages"), TEXT("InvalidCharacterData")));
			return;
		}

		if (!CharacterDataAsset->InventoryProductId.IsEmpty()) // If the player does not own this character then we can kick them, they should not call this function if they don't own it.
		{
			AIPlayerState* OwnerPlayerState = PlayerControllerWeak->GetPlayerState<AIPlayerState>();

			check(OwnerPlayerState);
			if (!OwnerPlayerState)
			{
				UE_LOG(TitansNetwork, Log, TEXT("AIGameMode::CreateCharacterAsync: OwnerPlayerState is null"));
				PlayerControllerWeak->ServerCreateCharacterReplyFail(); 
				return;
			}

			bool bCharacterIsOwned = OwnerPlayerState->OwnsStoreProduct(CharacterDataAsset->InventoryProductId);

			if (!ensure(bCharacterIsOwned))
			{
				AIGameSession* IGameSession = Cast<AIGameSession>(GameSession);
				if (IGameSession)
				{
					IGameSession->KickPlayer(PlayerControllerWeak.Get(), FText::FromStringTable(TEXT("ST_ErrorMessages"), TEXT("KickPlayerCharacterNotOwned")));
				}
				UE_LOG(TitansNetwork, Log, TEXT("AIGameMode::CreateCharacterAsync: Character is not owned. AGID = %s"), *OwnerPlayerState->GetAlderonID().ToDisplayString());
				return;
			}
		}

		if (!CharacterDataAsset->IsReleased())
		{
			UE_LOG(TitansNetwork, Log, TEXT("AIGameMode::CreateCharacterAsync: CharacterDataAsset is not flagged as released."));
			PlayerControllerWeak->ServerCreateCharacterReplyFail(); 
			return;
		}

		TSoftClassPtr<AIBaseCharacter> CharacterClassSoft = CharacterDataAsset->PreviewClass;
		if (!CharacterClassSoft.Get())
		{
			// Start Async Loading
			FStreamableManager& Streamable = UIGameplayStatics::GetStreamableManager(this);
			Streamable.RequestAsyncLoad(
				CharacterClassSoft.ToSoftObjectPath(),
				FStreamableDelegate::CreateUObject(this, &AIGameMode::FinishCreatingCharacter, PlayerControllerWeak.Get(), CharacterDataAsset, SkinDataAssets[0], CharacterData, Handle, OnCompleted),
				FStreamableManager::AsyncLoadHighPriority, false
			);
		} 
		else 
		{
			FinishCreatingCharacter(PlayerControllerWeak.Get(), CharacterDataAsset, SkinDataAssets[0], CharacterData, Handle, OnCompleted);
		}
	});

	TSharedPtr<FStreamableHandle> Handle = UIGameInstance::AsyncLoadCharacterSkinData(CharacterData.SelectedSkin.CharacterAssetId, CharacterData.SelectedSkin.SkinAssetId, PostLoadDelegate, ESkinLoadType::All);
}

float AIGameMode::GetDistanceToClosestPlayer(FVector TargetLocation)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("AIGameMode::GetDistanceToClosestPlayer"))

	const AIGameState* const IGameState = GetGameState<AIGameState>();
	check(IGameState);

	float DistanceToPlayer = TNumericLimits<float>::Max();

	for (const APlayerState* PlayerState : IGameState->PlayerArray)
	{
		if (!PlayerState)
		{
			continue;
		}

		const APawn* const ControlledPawn = PlayerState->GetPawn();
		if (!ControlledPawn)
		{
			continue;
		}

		const float FoundDistance = (TargetLocation - ControlledPawn->GetActorLocation()).Size();

		if (FoundDistance < DistanceToPlayer)
		{
			DistanceToPlayer = FoundDistance;
		}
	}

	for (const TPair<FAlderonUID, AIBaseCharacter*>& CombatLogPair : CombatLogAI)
	{
		const AIBaseCharacter* const CombatLogCharacter = CombatLogPair.Value;
		if (!CombatLogCharacter)
		{
			continue;
		}

		const float FoundDistance = (TargetLocation - CombatLogCharacter->GetActorLocation()).Size();

		if (FoundDistance < DistanceToPlayer)
		{
			DistanceToPlayer = FoundDistance;
		}
	}

	return DistanceToPlayer;
}

FTransform AIGameMode::FindRandomSpawnPointWithParams_Implementation(const FSpawnPointParameters& Params)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("AIGameMode::FindRandomSpawnPointForTag"))

	const bool bUseRandomMethod = ShouldUseRandomSpawnPoint();

	FName SpawnTag = NAME_None;
	if (!Params.TargetTags.IsEmpty())
	{
		SpawnTag = Params.TargetTags[0];
	}

	if (SpawnTag == NAME_None)
	{
		// No spawn tag so just find a random spawn point.

		FTransform Result = FTransform::Identity;

		if (!bUseRandomMethod)
		{
			Result = FindGenericSpawnPointFurthestFromPlayers();
		}

		if (Result.GetLocation() == FVector::ZeroVector)
		{
			Result = FindRandomGenericSpawnPoint();
		}

		return Result;
	}

	const TArray<APlayerStart*> SpawnPointsMatchingTag = GetSpawnPointsWithTag(SpawnTag);

	if (SpawnPointsMatchingTag.IsEmpty())
	{
		// No spawn points with the specified tag, so try the next tag.
		if (Params.TargetTags.IsValidIndex(1))
		{
			FSpawnPointParameters FallbackParams = Params;
			FallbackParams.TargetTags.RemoveAt(0);
			return FindRandomSpawnPointWithParams(FallbackParams);
		}

		// No valid spawn points for any tag and no spawn points without players nearby.
		// Return the spawn point with less players nearby.
		return FindGenericSpawnPointWithLessPlayersNearby();
	}

	if (bUseRandomMethod)
	{
		const int32 RandomSpawnIndex = FMath::RandRange(0, SpawnPointsMatchingTag.Num() - 1);
		const APlayerStart* const RandomPlayerStart = SpawnPointsMatchingTag[RandomSpawnIndex];
		check(RandomPlayerStart);
		return FTransform(FRotator(0, RandomPlayerStart->GetActorRotation().Yaw, 0), RandomPlayerStart->GetActorLocation(), FVector::OneVector);
	}

	// Find the spawn point furthest from any player.
	float DistanceFromTagToPlayer = 0.0f;
	const APlayerStart* TagFurthestAway = SpawnPointsMatchingTag[0];

	for (const APlayerStart* PlayerStart : SpawnPointsMatchingTag)
	{
		float Distance = GetDistanceToClosestPlayer(PlayerStart->GetActorLocation());
		if (Distance > DistanceFromTagToPlayer)
		{
			DistanceFromTagToPlayer = Distance;
			TagFurthestAway = PlayerStart;
		}
	}

	// Find all spawn points around the furthest spawn point within a certain radius,
	// away from any player, and far away from the player's previous death.
	TArray<const APlayerStart*> FurthestAwaySpawnPoints{};
	FurthestAwaySpawnPoints.Reserve(SpawnPointsMatchingTag.Num());
	TArray<const APlayerStart*> FurthestAwaySpawnPointsExcludingNearDeath{};
	FurthestAwaySpawnPointsExcludingNearDeath.Reserve(SpawnPointsMatchingTag.Num());

	for (const APlayerStart* PlayerStart : SpawnPointsMatchingTag)
	{
		if (ValidatePotentialSpawnPoint(PlayerStart, TagFurthestAway))
		{
			// If the spawn point is valid, add it to both FurthestAwaySpawnPoints and FurthestAwaySpawnPointsExcludingNearDeath.
			// If it too close to the previous death location, don't add it to FurthestAwaySpawnPointsExcludingNearDeath.
			// We need to use 2 arrays in case there are no spawn points far enough away from the last death.
			FurthestAwaySpawnPoints.Add(PlayerStart);

			if (Params.PreviousDeathLocation != FVector::ZeroVector && FVector::Distance(PlayerStart->GetActorLocation(), Params.PreviousDeathLocation) > ExcludeSpawnNearDeathRadius)
			{
				FurthestAwaySpawnPointsExcludingNearDeath.Add(PlayerStart);
			}
		}
	}

	// If there are spawn points far from the previous death, use those. Otherwise, use the backup spawn points.
	const TArray<const APlayerStart*>& SelectedSpawnPointArray = FurthestAwaySpawnPointsExcludingNearDeath.IsEmpty() ? FurthestAwaySpawnPoints : FurthestAwaySpawnPointsExcludingNearDeath;

	if (!SelectedSpawnPointArray.IsEmpty())
	{
		const int32 RandomSpawnIndex = FMath::RandRange(0, SelectedSpawnPointArray.Num() - 1);
		const APlayerStart* const RandomPlayerStart = SelectedSpawnPointArray[RandomSpawnIndex];
		check(RandomPlayerStart);
		return FTransform(FRotator(0, RandomPlayerStart->GetActorRotation().Yaw, 0), RandomPlayerStart->GetActorLocation(), FVector::OneVector);
	}

	// If there are absolutely no valid spawns for distance, then use the furthest spawn point from any player.
	return FTransform(FRotator(0, TagFurthestAway->GetActorRotation().Yaw, 0), TagFurthestAway->GetActorLocation(), FVector::OneVector);
}

FTransform AIGameMode::FindRandomSpawnPoint()
{
	return FindRandomSpawnPointForTag();
}

FTransform AIGameMode::FindRandomSpawnPointForCharacter_Implementation(const AIBaseCharacter* Character, const AIPlayerState* PlayerState, const AIPlayerController* PendingController)
{
	if (!Character)
	{
		return FTransform::Identity;
	}

	FSpawnPointParameters SpawnPointParams{};
	SpawnPointParams.TargetTags = { Character->GetCombinedPlayerStartTag(), Character->PlayerStartTag };

	if (PendingController == nullptr)
	{
		PendingController = Character->GetController<AIPlayerController>();
	}
	if (PendingController)
	{
		SpawnPointParams.PreviousDeathLocation = PendingController->GetDeathLocation();
	}
	return FindRandomSpawnPointWithParams(SpawnPointParams);
}

FTransform AIGameMode::FindRandomSpawnPointForTag_Implementation(FName SpawnTag, FName FallbackTag)
{
	FSpawnPointParameters SpawnPointParams{};
	if (SpawnTag != NAME_None)
	{
		SpawnPointParams.TargetTags.Add(SpawnTag);
	}

	if (FallbackTag != NAME_None)
	{
		SpawnPointParams.TargetTags.Add(FallbackTag);
	}

	SpawnPointParams.PreviousDeathLocation = FVector::ZeroVector;

	return FindRandomSpawnPointWithParams(SpawnPointParams);
}

FTransform AIGameMode::FindGenericSpawnPointFurthestFromPlayers_Implementation()
{
	FVector SelectedSpawnVector = FVector::ZeroVector;
	float DistanceFromSpawnToPlayer = 0.0f;

	for (const FVector& PossibleSpawnVector : GenericSpawnPoints)
	{
		float Distance = GetDistanceToClosestPlayer(PossibleSpawnVector);
		if (Distance > DistanceFromSpawnToPlayer)
		{
			DistanceFromSpawnToPlayer = Distance;
			SelectedSpawnVector = PossibleSpawnVector;
		}
	}

	return FTransform(FRotator(0, 0, 0), SelectedSpawnVector, FVector::OneVector);
}

FTransform AIGameMode::FindRandomGenericSpawnPoint_Implementation()
{
	if (GenericSpawnPoints.IsEmpty())
	{
		UE_LOG(TitansNetwork, Error, TEXT("AIGameMode::FindRandomGenericSpawnPoint_Implementation: There are no generic spawn points to be found! Please place some before trying to spawn in!"));
		return FTransform::Identity;
	}

	const int32 RandomSpawnIndex = FMath::RandRange(0, GenericSpawnPoints.Num() - 1);
	return FTransform(FRotator(0, 0, 0), GenericSpawnPoints[RandomSpawnIndex], FVector::OneVector);
}

FTransform AIGameMode::FindGenericSpawnPointWithLessPlayersNearby_Implementation()
{
	if (GenericSpawnPoints.IsEmpty())
	{
		UE_LOG(TitansNetwork, Error, TEXT("AIGameMode::FindRandomGenericSpawnPoint_Implementation: There are no generic spawn points to be found! Please place some before trying to spawn in!"));
		return FTransform::Identity;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(TitansNetwork, Error, TEXT("AIGameMode::FindRandomGenericSpawnPoint_Implementation: UWorld is null"));
		return FTransform::Identity;
	}

	FVector SelectedSpawnPoint;
	int32 MinPlayersInExclusionRadius = MAX_int32;
	TArray<FHitResult> OutHits{};
	ECollisionChannel TraceChannel = ECC_Pawn;
	FCollisionQueryParams TraceParams;
	
	for (const FVector SpawnPoint : GenericSpawnPoints)
	{
		World->SweepMultiByChannel(OutHits, SpawnPoint, SpawnPoint, FQuat(), TraceChannel, FCollisionShape::MakeSphere(PotentialSpawnExclusionRadius), TraceParams);
		int32 PlayersInExclusionRadius = 0;

		for (FHitResult Hit : OutHits)
		{
			if (Hit.GetActor()->IsA(AIDinosaurCharacter::StaticClass()))
			{
				PlayersInExclusionRadius++;
			}
		}

		if (PlayersInExclusionRadius < MinPlayersInExclusionRadius)
		{
			SelectedSpawnPoint = SpawnPoint;
		}
	}

	return FTransform(FRotator(0, 0, 0), SelectedSpawnPoint, FVector::OneVector);
}

bool AIGameMode::ShouldUseRandomSpawnPoint_Implementation() const
{
	const ENetMode NetMode = GetNetMode();
	if (NetMode == NM_ListenServer || NetMode == NM_Standalone)
	{
		return true;
	}

	const AIGameState* const IGameState = GetGameState<AIGameState>();
	if (!ensureAlways(IGameState))
	{
		return true;
	}

	for (APlayerState* PlayerState : IGameState->PlayerArray)
	{
		if (PlayerState->GetPawn<APawn>())
		{
			return false;
		}
	}

	return true;
}

bool AIGameMode::ValidatePotentialSpawnPoint(const APlayerStart* PlayerStart, const APlayerStart* FurthestPlayerStart)
{
	if (!PlayerStart)
	{
		return false;
	}

	if (!FurthestPlayerStart)
	{
		return false;
	}

	const FVector PlayerStartLocation = PlayerStart->GetActorLocation();

	if (FVector::Distance(PlayerStartLocation, FurthestPlayerStart->GetActorLocation()) > FurthestSpawnInclusionRadius)
	{
		return false;
	}

	if (GetDistanceToClosestPlayer(PlayerStartLocation) <= PotentialSpawnExclusionRadius)
	{
		return false;
	}

	return true;
}

TArray<APlayerStart*> AIGameMode::GetSpawnPointsWithTag(FName SpawnTag) const
{
	TArray<APlayerStart*> Result{};
	Result.Reserve(CustomSpawnPoints.Num());

	for (APlayerStart* PlayerStart : CustomSpawnPoints)
	{
		if (!PlayerStart)
		{
			continue;
		}

		if (PlayerStart->PlayerStartTag == SpawnTag)
		{
			Result.Add(PlayerStart);
			continue;
		}

		AIPlayerStart* const IPlayerStart = Cast<AIPlayerStart>(PlayerStart);
		if (IPlayerStart && IPlayerStart->AnyPlayerStartTags.Contains(SpawnTag))
		{
			Result.Add(PlayerStart);
			continue;
		}
	}

	return Result;
}

void AIGameMode::FinishCreatingCharacter(AIPlayerController* PlayerController, UCharacterDataAsset* CharacterDataAsset, USkinDataAsset* SkinDataAsset, FCharacterData CharacterData, TSharedPtr<FStreamableHandle> Handle, FAsyncCharacterCreated OnCreateCompleted)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("AIGameMode::FinishCreatingCharacter"))

	if (!PlayerController)
	{
		UE_LOG(TitansNetwork, Log, TEXT("AIGameMode::FinishCreatingCharacter: PlayerController is null"));
		return;
	}

	AIPlayerState* IPlayerState = PlayerController->GetPlayerState<AIPlayerState>();
	if (!IPlayerState)
	{
		UE_LOG(TitansNetwork, Log, TEXT("AIGameMode::FinishCreatingCharacter: IPlayerState is null"));
		PlayerController->ServerCreateCharacterReplyFail(); 
		return;
	}

	TSubclassOf<AIBaseCharacter> CharacterClass = CharacterDataAsset->PreviewClass.Get();
	check(CharacterClass);

	if (!CharacterClass)
	{
		UE_LOG(TitansNetwork, Log, TEXT("AIGameMode::FinishCreatingCharacter: CharacterClass is null"));
		PlayerController->ServerCreateCharacterReplyFail(FText::FromStringTable(TEXT("ST_ErrorMessages"), TEXT("InvalidCharacterData")));
		return;
	}

	check(DatabaseEngine);

	FAlderonPlayerID AlderonPlayerId = IPlayerState->GetAlderonID();

	check(AlderonPlayerId.IsValid());

	if (!AlderonPlayerId.IsValid())
	{
		UE_LOG(TitansNetwork, Log, TEXT("AIGameMode::FinishCreatingCharacter: AlderonPlayerId is invalid"));
		PlayerController->ServerCreateCharacterReplyFail(FText::FromStringTable(TEXT("ST_ErrorMessages"), TEXT("InvalidAlderonId")));
		return;
	}

	TWeakObjectPtr<AIPlayerController> PlayerControllerWeak = PlayerController;

	// Default CharacterId is 0 which is invalid
	// Database will detect this and randomly generate one
	// Hopefully it won't be taken but we will fix that later
	FDatabaseCreateUIDCompleted OnDatabaseUIDCreated = FDatabaseCreateUIDCompleted::CreateLambda([this, PlayerControllerWeak, CharacterData, CharacterClass, CharacterDataAsset, OnCreateCompleted, AlderonPlayerId, Handle](const FDatabaseCreateUID& Data)
	{
		if (!PlayerControllerWeak.IsValid())
		{
			//UE_LOG(TitansNetwork, Log, TEXT("AIGameMode::FinishCreatingCharacter: PlayerControllerWeak is invalid"));
			return;
		}
		
		if (!Data.bSuccess)
		{
			PlayerControllerWeak->ServerCreateCharacterReplyFail(FText::FromString(TEXT("Database Error Creating Character.")));
			return;
		}

		FCharacterData NewCharacterData = CharacterData;
		const FAlderonUID NewCharacterID = Data.GeneratedUID;

		TSubclassOf<AIBaseCharacter> LoadedCharacter = CharacterClass;

		if (!LoadedCharacter)
		{
			UE_LOG(TitansNetwork, Error, TEXT("AIGameMode::FinishCreatingCharacter: CharacterClass is null. Forcing Load!"));
			START_PERF_TIME()
			LoadedCharacter = CharacterDataAsset->PreviewClass.LoadSynchronous();
			END_PERF_TIME()
			WARN_PERF_TIME_STATIC(1)
		}
		
		if (!IsValid(LoadedCharacter))
		{
			UE_LOG(TitansNetwork, Error, TEXT("AIGameMode::FinishCreatingCharacter: CharacterClass is still null after force load"));
			PlayerControllerWeak->ServerCreateCharacterReplyFail(FText::FromStringTable(TEXT("ST_ErrorMessages"), TEXT("InvalidCharacterData")));
			return;
		}

		// Get Random Spawn Point
		const FName PlayerStartTag = LoadedCharacter->GetDefaultObject<AIBaseCharacter>()->PlayerStartTag;
		FSpawnPointParameters SpawnPointParams{};
		SpawnPointParams.TargetTags = { LoadedCharacter->GetDefaultObject<AIBaseCharacter>()->GetCombinedPlayerStartTag(&CharacterData.Growth), PlayerStartTag };
		SpawnPointParams.PreviousDeathLocation = PlayerControllerWeak->GetDeathLocation();
		const FTransform SpawnTransform = FindRandomSpawnPointWithParams(SpawnPointParams);

		AIBaseCharacter* Character = Cast<AIBaseCharacter>(UGameplayStatics::BeginDeferredActorSpawnFromClass(
			this, LoadedCharacter, SpawnTransform, ESpawnActorCollisionHandlingMethod::AlwaysSpawn, PlayerControllerWeak.Get())
			);

		if (!Character)
		{
			UE_LOG(TitansNetwork, Log, TEXT("AIGameMode::FinishCreatingCharacter: Character is null"));
			PlayerControllerWeak->ServerCreateCharacterReplyFail(); 
			return;
		}

		// Add Character to root to prevent it from being garbage collected while waiting for a database call
		Character->AddToRoot();

		Character->_SetReplicatesPrivate(false);
		Character->SetActorHiddenInGame(true);
		Character->SetActorTickEnabled(false);
		Character->SetActorEnableCollision(false);
		Character->SetGrowthPercent(CharacterData.Growth);

		Character->SaveCharacterPosition = SpawnTransform.GetLocation();
		Character->SaveCharacterRotation = FRotator::ZeroRotator;

		Character->SetCharacterName(CharacterData.Name);

		AIDinosaurCharacter* DinoCharacter = Cast<AIDinosaurCharacter>(Character);
		if (DinoCharacter)
		{
			DinoCharacter->FillModSkus();

			DinoCharacter->SetRandomBabySkinTint();
			DinoCharacter->SetSkinData(CharacterData.SelectedSkin);

			// Set initial subspecies as unlocked
			DinoCharacter->GetUnlockedSubspeciesIndexes_Mutable().Add(CharacterData.SelectedSkin.MeshIndex);
			NewCharacterData.UnlockedSubspeciesIndexes = DinoCharacter->GetUnlockedSubspeciesIndexes();

			NewCharacterData.BabySkinTint = DinoCharacter->GetBabySkinTint();

			if (DinoCharacter->AbilitySystem)
			{
				DinoCharacter->AbilitySystem->InitAttributes(true);
			}
		}

		if (AIGameSession* IGameSession = Cast<AIGameSession>(GameSession))
		{
			if (UIGameplayStatics::IsGrowthEnabled(this) && !UIGameplayStatics::AreHatchlingCavesEnabled(this) && IGameSession->HatchlingCaveExitGrowth > MIN_GROWTH)
			{
				UPOTAbilitySystemGlobals::ApplyDynamicGameplayEffect(DinoCharacter, UCoreAttributeSet::GetGrowthAttribute(), IGameSession->HatchlingCaveExitGrowth, EGameplayModOp::Override);
				DinoCharacter->ResetStats();
			}
		}

		check(NewCharacterID.IsValid());
		Character->SetCharacterID(NewCharacterID);
		NewCharacterData.CharacterID = NewCharacterID;

		FDatabaseOperationCompleted OnCharacterSaved = FDatabaseOperationCompleted::CreateLambda([this, Character, PlayerControllerWeak, NewCharacterID, NewCharacterData, OnCreateCompleted, AlderonPlayerId, Handle](const FDatabaseOperationData& Data)
		{
			if (IsValid(Character))
			{
				Character->RemoveFromRoot();
				Character->Destroy();
			}
			else // should never happen as we add the character to root while we are using until the callback
			{
				UE_LOG(LogTemp, Warning, TEXT("AIGameMode::FinishCreatingCharacter: Character is invalid"));
			}

			// Player disconnected while waiting for a character create operation
			if (!PlayerControllerWeak.IsValid())
			{
				return;
			}

			// Database Call Fail if the player is connected we need to let them know it failed
			if (!Data.bSuccess || !NewCharacterID.IsValid())
			{
				PlayerControllerWeak->ServerCreateCharacterReplyFail(FText::FromString(TEXT("Database Error Creating Character.")));
				return;
			}

			AIPlayerState* IPlayerState = PlayerControllerWeak->GetPlayerState<AIPlayerState>();

			if (IsValid(IPlayerState))
			{
				check(!IPlayerState->Characters.Contains(NewCharacterID));

				IPlayerState->Characters.Add(NewCharacterID.ToString());
				IPlayerState->CharactersData.Add(NewCharacterID, NewCharacterData);

				AIPlayerController* IPlayerController = Cast<AIPlayerController>(IPlayerState->GetOwner());
				if (IsValid(IPlayerController))
				{
					TArray<FCharacterData> Values;
					IPlayerState->CharactersData.GenerateValueArray(Values);
					IPlayerController->ServerCreateCharacterReply(Values);
				}

				SavePlayerState(IPlayerState, ESavePriority::High);
				OnCreateCompleted.ExecuteIfBound(IPlayerController, NewCharacterID);
			}
		});

		if (IsValid(Character))
		{
			DatabaseEngine->SaveCharacter(Character, AlderonPlayerId, NewCharacterID, OnCharacterSaved, ESavePriority::Low);
		}

		if (Handle.IsValid())
		{
			Handle->ReleaseHandle();
		}

	});

	DatabaseEngine->GetNewUID(OnDatabaseUIDCreated);
}

void AIGameMode::EditCharacterAsync(AIPlayerController* PlayerController, FCharacterData CharacterData, FAsyncCharacterCreated OnCompleted)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("AIGameMode::EditCharacterAsync"))

	FCharacterDataSkinLoaded PostLoadDelegate = FCharacterDataSkinLoaded::CreateLambda([this, PlayerController, CharacterData, OnCompleted](bool bSuccess, FPrimaryAssetId CharacterAssetId, UCharacterDataAsset* CharacterDataAsset, TArray<FPrimaryAssetId> SkinAssetIds, TArray<USkinDataAsset*> SkinDataAssets, TSharedPtr<FStreamableHandle> Handle)
	{
		check(SkinDataAssets.Num() > 0);
		if (SkinDataAssets.Num() == 0)
		{
			UE_LOG(LogTemp, Log, TEXT("AIGameMode::CreateCharacterAsync: SkinDataAsset is null"));
			return;
		}

		check(CharacterDataAsset);
		if (!CharacterDataAsset)
		{
			UE_LOG(LogTemp, Log, TEXT("AIGameMode::CreateCharacterAsync: CharacterDataAsset is null"));
			return;
		}

		TSoftClassPtr<AIBaseCharacter> CharacterClassSoft = CharacterDataAsset->PreviewClass;
		if (!CharacterClassSoft.Get())
		{
			// Start Async Loading
			FStreamableManager& Streamable = UIGameplayStatics::GetStreamableManager(this);
			Streamable.RequestAsyncLoad(
				CharacterClassSoft.ToSoftObjectPath(),
				FStreamableDelegate::CreateUObject(this, &AIGameMode::FinishEditingCharacter, PlayerController, CharacterDataAsset, SkinDataAssets[0], Handle, CharacterData, OnCompleted),
				FStreamableManager::AsyncLoadHighPriority, false
			);
		} else {
			FinishEditingCharacter(PlayerController, CharacterDataAsset, SkinDataAssets[0], Handle, CharacterData);
		}
	});

	TSharedPtr<FStreamableHandle> Handle = UIGameInstance::AsyncLoadCharacterSkinData(CharacterData.SelectedSkin.CharacterAssetId, CharacterData.SelectedSkin.SkinAssetId, PostLoadDelegate, ESkinLoadType::All);
}

void AIGameMode::FinishEditingCharacter(AIPlayerController* PlayerController, UCharacterDataAsset* CharacterDataAsset, USkinDataAsset* SkinDataAsset, TSharedPtr<FStreamableHandle> Handle, FCharacterData CharacterData, FAsyncCharacterCreated OnCreateCompleted)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("AIGameMode::FinishEditingCharacter"))

	TSubclassOf<AIBaseCharacter> CharacterClass = CharacterDataAsset->PreviewClass.Get();
	check(CharacterClass);
	if (!CharacterClass)
	{
		UE_LOG(TitansNetwork, Error, TEXT("AIGameMode::FinishEditingCharacter: CharacterClass is null when we just loaded it"));
		return;
	}

	FTransform SpawnTransform = FTransform(FVector::OneVector);

	bool bExistingCharacter = false;
	AIDinosaurCharacter* Character = nullptr;
	if (AIDinosaurCharacter* ExistingCharacter = Cast<AIDinosaurCharacter>(PlayerController->GetPawn()))
	{
		if (ExistingCharacter->GetClass() == CharacterClass)
		{
			bExistingCharacter = true;
			Character = ExistingCharacter;
		}
	}
	
	if (Character == nullptr)
	{
		Character = Cast<AIDinosaurCharacter>(UGameplayStatics::BeginDeferredActorSpawnFromClass(
			this, CharacterClass, SpawnTransform, ESpawnActorCollisionHandlingMethod::AlwaysSpawn, PlayerController)
			);

		if (!Character)
		{
			UE_LOG(TitansNetwork, Error, TEXT("AIGameMode::FinishEditingCharacter: Character is null when we just attempted to spawn it"));
			return;
		}

		Character->_SetReplicatesPrivate(false);
		Character->SetActorHiddenInGame(true);
		Character->SetActorTickEnabled(false);

		// Have to do this so the stats aren't saved over with 0's
		if (Character->AbilitySystem)
		{
			Character->AbilitySystem->InitAttributes(true);
		}
	}
	

	// Load Character From Database
	check(DatabaseEngine);

	if (bExistingCharacter)
	{
		FSelectedSkin PrevSkinData = Character->GetSkinData();

		if (Character->GetSheddingProgress() == 0)
		{
			if (CharacterData.SelectedSkin.MeshIndex != PrevSkinData.MeshIndex && !Character->GetUnlockedSubspeciesIndexes().Contains(CharacterData.SelectedSkin.MeshIndex))
			{
				UE_LOG(LogTemp, Log, TEXT("AIGameMode::FinishEditingCharacter: Subspecies is not unlocked."));
				return;
			}

			if (CharacterData.SelectedSkin.bGender != PrevSkinData.bGender || CharacterData.SelectedSkin.MeshIndex != PrevSkinData.MeshIndex)
			{
				AIGameSession* IGameSession = Cast<AIGameSession>(GameSession);
				check(IGameSession);
				if (!IGameSession) return;

				if (!IGameSession->bServerAllowChangeSubspecies)
				{
					UE_LOG(LogTemp, Log, TEXT("AIGameMode::FinishEditingCharacter: Changing subspecies is not allowed."));
					return;
				}

				if (UIGameplayStatics::AreHomeCavesEnabled(this))
				{
					// Can only edit subspecies in home cave
					if (!IsValid(Character->GetCurrentInstance()))
					{
						UE_LOG(LogTemp, Log, TEXT("AIGameMode::FinishEditingCharacter: Changing subspecies only allowed in home cave."));
						return;
					}
				}
				else
				{
					// Can only edit subspecies while sleeping
					if (!Character->IsSleeping())
					{
						UE_LOG(LogTemp, Log, TEXT("AIGameMode::FinishEditingCharacter: Changing subspecies only allowed while sleeping."));
						return;
					}
				}

				// Growth penalty for changing gender or subspecies
				Character->ApplyGrowthPenalty(IGameSession->GetChangeSubspeciesGrowthPenaltyPercent(), true, true, true);

				CharacterData.Growth = Character->GetGrowthPercent();
			}

			Character->SetSkinData(CharacterData.SelectedSkin);	// Only update the with a new skin if the character is not currently shedding.
		}

		AIPlayerState* IPlayerState = PlayerController->GetPlayerState<AIPlayerState>();
		check(IPlayerState);
		
		TStrongObjectPtr<AIPlayerController> PlayerControllerStrong = TStrongObjectPtr<AIPlayerController>(PlayerController);

		FDatabaseOperationCompleted OnCompleted2 = FDatabaseOperationCompleted::CreateLambda([PlayerControllerStrong, CharacterData](const FDatabaseOperationData& Data)
		{
			bool bCharacterEdited = true;

			if (PlayerControllerStrong.IsValid())
			{
				PlayerControllerStrong->ServerEditCharacterReply(bCharacterEdited, CharacterData);
			}
			else
			{
				UE_LOG(TitansLog, Error, 
				TEXT("AIGameMode::FinishEditingCharacter: Was unable to get a PlayerController to Complete ServerEditCharacterReply. If this is the case the player most likely DC'd while saving the skin, but if the player is still in game somehow then they are stuck in a never ending save"));
			}
		});

		DatabaseEngine->SaveCharacter(Character, IPlayerState->GetAlderonID(), CharacterData.CharacterID, OnCompleted2, ESavePriority::High);
		return;
	}
	else
	{
		TStrongObjectPtr<AIPlayerController> PlayerControllerStrong = TStrongObjectPtr<AIPlayerController>(PlayerController);
		TStrongObjectPtr<AIDinosaurCharacter> CharacterStrong = TStrongObjectPtr<AIDinosaurCharacter>(Character);
		TStrongObjectPtr<AIGameMode> StrongThis = TStrongObjectPtr<AIGameMode>(this);

		FDatabaseLoadCharacterCompleted OnCompleted = FDatabaseLoadCharacterCompleted::CreateLambda([PlayerControllerStrong, CharacterStrong, CharacterData, StrongThis, Handle, bExistingCharacter](const FDatabaseLoadCharacter& Data) mutable
		{
			if (!CharacterStrong.IsValid())
			{
				return;
			}
			if (!PlayerControllerStrong.IsValid())
			{
				return;
			}
			if (!StrongThis.IsValid())
			{
				return;
			}

			IAlderonDatabase::DeserializeObject(CharacterStrong.Get(), Data.CharacterDataJson);

			if (CharacterStrong->AbilitySystem != nullptr)
			{
				if (!bExistingCharacter)
				{
					if (UIGameplayStatics::IsGrowthEnabled(CharacterStrong.Get()))
					{
						float Growth = 0.f;
						if (IAlderonDatabase::DeserializeGameplayAttribute(Data.CharacterDataJson, IAlderonDatabase::GetAttributeSerializedName("Growth"), Growth))
						{
							CharacterStrong->AbilitySystem->SetNumericAttributeBase(UCoreAttributeSet::GetGrowthAttribute(), Growth);
						}
					}
					else
					{
						CharacterStrong->AbilitySystem->SetNumericAttributeBase(UCoreAttributeSet::GetGrowthAttribute(), 1.0);
					}

					CharacterStrong->AbilitySystem->InitAttributes(false);
				}
				
				IAlderonDatabase::DeserializeGameplayAttributes(CharacterStrong->AbilitySystem, Data.CharacterDataJson);
			}

			AIPlayerState* IPlayerState = PlayerControllerStrong->GetPlayerState<AIPlayerState>();
			if (!IPlayerState)
			{
				return;
			}

			if (!IPlayerState->CharactersData.Contains(CharacterData.CharacterID))
			{
				return;
			}

			FSelectedSkin PrevSkinData = CharacterStrong->GetSkinData();
			if (CharacterData.SelectedSkin.MeshIndex != PrevSkinData.MeshIndex && !CharacterStrong->GetUnlockedSubspeciesIndexes().Contains(CharacterData.SelectedSkin.MeshIndex))
			{
				UE_LOG(LogTemp, Log, TEXT("AIGameMode::FinishEditingCharacter: Subspecies is not unlocked."));
				return;
			}

			if (CharacterData.SelectedSkin.bGender != PrevSkinData.bGender || CharacterData.SelectedSkin.MeshIndex != PrevSkinData.MeshIndex)
			{
				AIGameSession* IGameSession = Cast<AIGameSession>(StrongThis->GameSession);
				check(IGameSession);
				if (!IGameSession) return;

				if (!IGameSession->bServerAllowChangeSubspecies)
				{
					UE_LOG(LogTemp, Log, TEXT("AIGameMode::FinishEditingCharacter: Changing subspecies is not allowed."));
					return;
				}

				CharacterStrong->ApplyGrowthPenalty(IGameSession->GetChangeSubspeciesGrowthPenaltyPercent(), true, true, true);

				//Update character data with new growth percent
				CharacterData.Growth = CharacterStrong->GetGrowthPercent();
			}

			// TODO: Validate Params to see if user input is valid
			// we are skipping this for saving time
			// Update Skin to new selection
			CharacterStrong->SetSkinData(CharacterData.SelectedSkin);

			IPlayerState->CharactersData[CharacterData.CharacterID].SheddingProgress = CharacterStrong->GetSheddingProgressRaw();

			UE_LOG(LogTemp, Log, TEXT("SheddingDebug: FinishEditingCharacter OnCompleted - %d"), CharacterStrong->GetSheddingProgressRaw());
			CharacterData.SheddingProgress = CharacterStrong->GetSheddingProgressRaw();

			FDatabaseOperationCompleted OnCompleted2 = FDatabaseOperationCompleted::CreateLambda([CharacterStrong, PlayerControllerStrong, CharacterData](const FDatabaseOperationData& Data)
			{
				if (!PlayerControllerStrong.IsValid())
				{
					return;
				}
				if (CharacterStrong.IsValid())
				{
					CharacterStrong->Destroy();
				}
				bool bCharacterEdited = true;
				PlayerControllerStrong->ServerEditCharacterReply(bCharacterEdited, CharacterData);
			});

			StrongThis->DatabaseEngine->SaveCharacter(CharacterStrong.Get(), IPlayerState->GetAlderonID(), CharacterData.CharacterID, OnCompleted2, ESavePriority::High);

			if (Handle.IsValid())
			{
				Handle->ReleaseHandle();
			}
		});

		DatabaseEngine->LoadCharacter(CharacterData.CharacterID, OnCompleted);
	}
}

bool AIGameMode::ShouldSpawnCombatLogAI()
{
#if WITH_EDITOR
	return true;
#else
	// Only spawn combat log ai on dedicated servers for now
	return (GetNetMode() == ENetMode::NM_DedicatedServer);
#endif
}

void AIGameMode::SpawnCharacterForPlayer(AIPlayerController* PlayerController, FAlderonUID CharacterUID, const FTransform& Transform, FAsyncCharacterSpawned OnSpawn)
{
	SpawnCharacter(PlayerController, CharacterUID, Transform.GetLocation() != FVector::ZeroVector, Transform, OnSpawn);
}

void AIGameMode::SpawnCharacter(AIPlayerController* PlayerController, FAlderonUID CharacterUID, bool bOverrideSpawn, FTransform OverrideTransform, FAsyncCharacterSpawned OnSpawn)
{
	if (!OnPlayerTrySpawn(PlayerController, CharacterUID, bOverrideSpawn, OverrideTransform, OnSpawn))
	{
		return;
	}

	SpawnCharacter_Internal(PlayerController, CharacterUID, bOverrideSpawn, OverrideTransform, OnSpawn);
}

void AIGameMode::SpawnCharacter_Internal(AIPlayerController* PlayerController, FAlderonUID CharacterUID, bool bOverrideSpawn, FTransform OverrideTransform, FAsyncCharacterSpawned OnSpawn)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("AIGameMode::SpawnCharacter"))
	
	AIGameSession* IGameSession = Cast<AIGameSession>(GameSession);
	if (!ensure(IGameSession))
	{
		UE_LOG(TitansNetwork, Error, TEXT("AIGameMode::SpawnCharacter: Invalid IGameSession"));
		return;
	}
		
	check(CharacterUID.IsValid());
	if (!CharacterUID.IsValid())
	{
		UE_LOG(TitansNetwork, Error, TEXT("AIGameMode::SpawnCharacter: Invalid CharacterUID %s"), *CharacterUID.ToString());
		return;
	}

	if (!PlayerController)
	{
		UE_LOG(TitansNetwork, Error, TEXT("AIGameMode::SpawnCharacter:  Invalid Player Controller"));
		return;
	}

	AIPlayerState* IPlayerState = PlayerController->GetPlayerState<AIPlayerState>();
	if (!IPlayerState)
	{
		UE_LOG(TitansNetwork, Error, TEXT("AIGameMode::SpawnCharacter: Invalid Player State"));
		return;
	}

	// Check Valid Character Index
	if (!ensure(IPlayerState->Characters.Contains(CharacterUID)))
	{
		UE_LOG(TitansNetwork, Log, TEXT("AIGameMode::SpawnCharacter: CharacterUID does not correlate to any valid Characters value"));
		IGameSession->KickPlayer(PlayerController, FText::FromStringTable(FName(TEXT("ST_ErrorMessages")), TEXT("InvalidCharacterData")));
		return;
	}
	
	if (!ensure(IPlayerState->CharactersData.Contains(CharacterUID)))
	{
		UE_LOG(TitansNetwork, Log, TEXT("AIGameMode::SpawnCharacter: CharacterUID does not correlate to any valid CharactersData value"));
		IGameSession->KickPlayer(PlayerController, FText::FromStringTable(FName(TEXT("ST_ErrorMessages")), TEXT("InvalidCharacterData")));
		return;
	}

	bool bSinglePlayer = (GetNetMode() == ENetMode::NM_Standalone || GetNetMode() == ENetMode::NM_ListenServer);

	// Setup Combat Log AI for player possess on MP or destruction on SP
	if (AIBaseCharacter* CombatAI = GetCombatLogAI(CharacterUID))
	{
		if (CombatAI->IsAlive())
		{

			// Prevent Combat AI from being killed since we want to reuse it
			bool bDestroy = false;
			bool bRemoveTimestamp = true;

			// mark this combat ai respawning user (server only)
			CombatAI->SetCombatLogRespawningUser();

			// Destroy Combat Log AI in SinglePlayer as we don't need it
			if (bSinglePlayer)
			{
				bDestroy = true;
			}

			// Server Log that we are reusing a combat log AI
			if (!bDestroy)
			{
				UE_LOG(TitansNetwork, Log, TEXT("AIGameMode::SpawnCharacter: Reusing Combat Log AI"));

				if (bOverrideSpawn)
				{
					CombatAI->SetActorTransform(OverrideTransform, false, nullptr, ETeleportType::ResetPhysics);
				}

				UCharacterDataAsset* CharacterDataAsset = UIGameInstance::LoadCharacterData(CombatAI->CharacterDataAssetId);
				check(CharacterDataAsset);
				IPlayerState->SetCharacterType(CharacterDataAsset->CharacterType);

				// Possess Character
				PlayerController->Possess(CombatAI);

				// Handle Loading
				PlayerController->AddClientViewSlaveLocation(CombatAI->GetActorLocation());

				// Clear environmental effects and re-apply any relevant ones.
				if (CombatAI->AbilitySystem != nullptr)
				{
					CombatAI->AbilitySystem->RemoveActiveEffectsWithTags(FGameplayTagContainer(UPOTAbilitySystemGlobals::Get().EnvironmentalEffectTag));
				}
				TArray<AActor*> GAVolumes;
				CombatAI->GetOverlappingActors(GAVolumes, AIGameplayAbilityVolume::StaticClass());
				for (AActor* GAVolume : GAVolumes)
				{
					PlayerController->ServerNotifyGAVolumeOverlap(true, Cast<AIGameplayAbilityVolume>(GAVolume));
				}

				// Temp Refresh Marks on Spawn Character
				CombatAI->AddMarks(0);
				CombatAI->AddCooldownsAfterLoad();
				CombatAI->AddGameplayEffectsAfterLoad();
				CombatAI->DirtySerializedNetworkFields();
				
				return;
			}
		}
	}

	// Fetch Character UID and use that to load it from the database
	FCharacterData& CharacterData = IPlayerState->CharactersData[CharacterUID];
	IPlayerState->SetLastSelectedCharacter(CharacterUID);

	// Check if over species limit
	TArray<FCharacterData> Characters;
	IPlayerState->CharactersData.GenerateValueArray(Characters);
	if (UIGameplayStatics::GetCharacterSpeciesCount(this, Characters, CharacterData.SelectedSkin.CharacterAssetId) > UIGameplayStatics::GetMaxCharacterSpeciesLimit(PlayerController))
	{
		UE_LOG(TitansNetwork, Log, TEXT("AIGameMode::SpawnCharacter: Max Character Species Limit Reached, Rejecting log in for character %s"), *CharacterData.CharacterID.ToString());
		IGameSession->KickPlayer(PlayerController, FText::FromString(TEXT("Max Character Species Limit Reached, Rejecting log in for character.")));
		return;
	}
	
	if (CharacterData.DeathInfo.bPermaDead && IGameSession->bPermaDeath)
	{
		UE_LOG(LogTemp, Warning, TEXT("Rejecting log in from permanently dead character %s"), *CharacterData.CharacterID.ToString());
		return;
	}
	
	UTitanAssetManager& AssetManager = static_cast<UTitanAssetManager&>(UAssetManager::Get());

	UIGameInstance::FixSkinData(CharacterData.SelectedSkin);

	FPrimaryAssetId SelectedSkin = AssetManager.GetRedirectForAsset(CharacterData.SelectedSkin.SkinAssetId);

	// Load Character & Skin Data Together
	TArray<FPrimaryAssetId> SkinsToLoad = { SelectedSkin };
	if (CharacterData.PreviousSkin.SkinAssetId.IsValid() && CharacterData.PreviousSkin.SkinAssetId != CharacterData.SelectedSkin.SkinAssetId)
	{
		UIGameInstance::FixSkinData(CharacterData.PreviousSkin);
		FPrimaryAssetId PreviousSkin = AssetManager.GetRedirectForAsset(CharacterData.PreviousSkin.SkinAssetId);
		SkinsToLoad.Add(PreviousSkin);
	}

	FCharacterDataSkinLoaded PostLoadDelegate = FCharacterDataSkinLoaded::CreateUObject(this, &ThisClass::SpawnCharacter_Stage1, TWeakObjectPtr<AIPlayerController>(PlayerController), CharacterUID, bOverrideSpawn, OverrideTransform, OnSpawn);
	UIGameInstance::AsyncLoadCharacterSkinData(CharacterData.SelectedSkin.CharacterAssetId, SkinsToLoad, PostLoadDelegate, ESkinLoadType::All);
}

void AIGameMode::SpawnCharacter_Stage1(bool bSuccess, FPrimaryAssetId CharacterAssetId, UCharacterDataAsset* CharacterDataAsset, TArray<FPrimaryAssetId> SkinAssetIds, TArray<USkinDataAsset*> SkinDataAssets, TSharedPtr<FStreamableHandle> Handle, TWeakObjectPtr<AIPlayerController> PlayerController, FAlderonUID CharacterUID, bool bOverrideSpawn, FTransform OverrideTransform, FAsyncCharacterSpawned OnSpawn)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("AIGameMode::SpawnCharacter_Stage1"))

	if (!PlayerController.Get())
	{
		UE_LOG(TitansNetwork, Error, TEXT("AIGameMode::SpawnCharacter_Stage1: Invalid Player Controller"));
		return;
	}

	AIPlayerState* IPlayerState = PlayerController->GetPlayerState<AIPlayerState>();
	if (!IPlayerState)
	{
		UE_LOG(TitansNetwork, Error, TEXT("AIGameMode::SpawnCharacter_Stage1: Invalid Player State"));
		return;
	}

	// Needed for kicking for bad character data etc
	AIGameSession* IGameSession = Cast<AIGameSession>(GameSession);
	check(IGameSession);

	if (!IGameSession) return;

	check(SkinDataAssets.Num() > 0);
	if (SkinDataAssets.Num() == 0)
	{
		UE_LOG(TitansNetwork, Log, TEXT("AIGameMode::SpawnCharacter_Stage1: SkinDataAsset is null"));
		IGameSession->KickPlayer(PlayerController.Get(), FText::FromStringTable(FName(TEXT("ST_ErrorMessages")), TEXT("InvalidCharacterData")));
		return;
	}

	check(CharacterDataAsset);
	if (!CharacterDataAsset)
	{
		UE_LOG(TitansNetwork, Log, TEXT("AIGameMode::SpawnCharacter_Stage1: CharacterDataAsset is null"));
		IGameSession->KickPlayer(PlayerController.Get(), FText::FromStringTable(FName(TEXT("ST_ErrorMessages")), TEXT("InvalidCharacterData")));
		return;
	}

	if (!CharacterDataAsset->IsReleased())
	{
		UE_LOG(TitansNetwork, Log, TEXT("AIGameMode::SpawnCharacter_Stage1: CharacterDataAsset is not flagged as released."));
		IGameSession->KickPlayer(PlayerController.Get(), FText::FromStringTable(FName(TEXT("ST_ErrorMessages")), TEXT("InvalidCharacterData")));
		return;
	}

	if (!CharacterDataAsset->InventoryProductId.IsEmpty()) // If the player does not own this character then we can kick them, they should not call this function if they don't own it.
	{
		bool bCharacterIsOwned = IPlayerState->OwnsStoreProduct(CharacterDataAsset->InventoryProductId);

		if (!ensure(bCharacterIsOwned))
		{
			IGameSession->KickPlayer(PlayerController.Get(), FText::FromStringTable(TEXT("ST_ErrorMessages"), TEXT("KickPlayerCharacterNotOwned")));
			UE_LOG(TitansNetwork, Log, TEXT("AIGameMode::SpawnCharacter_Stage1: Character is not owned. AGID = %s"), *IPlayerState->GetAlderonID().ToDisplayString());
			return;
		}
	}

	TSubclassOf<AIBaseCharacter> LoadedCharacter = CharacterDataAsset->PreviewClass.Get();

	if (!LoadedCharacter.Get())
	{
		UE_LOG(TitansNetwork, Error, TEXT("AIGameMode::SpawnCharacter: CharacterClass is null. Forcing Load!"));
		START_PERF_TIME()
		LoadedCharacter = CharacterDataAsset->PreviewClass.LoadSynchronous();
		END_PERF_TIME()
		#if UE_SERVER
			ERROR_BLOCK_THREAD_STATIC()
		#else
			WARN_PERF_TIME_STATIC(1)
		#endif
	}

	FTransform TempSpawnTransform = FTransform(FRotator(0, 0, 0), FVector(0, 0, 0), FVector::OneVector);

	AIDinosaurCharacter* Character = Cast<AIDinosaurCharacter>(UGameplayStatics::BeginDeferredActorSpawnFromClass
	(
		this,
		LoadedCharacter,
		TempSpawnTransform,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn,
		PlayerController.Get()
	));

	if (!Character)
	{
		UE_LOG(TitansNetwork, Log, TEXT("AIGameMode::SpawnCharacter: Character is null"));
		IGameSession->KickPlayer(PlayerController.Get(), FText::FromStringTable(FName(TEXT("ST_ErrorMessages")), TEXT("InvalidCharacterData")));
		return;
	}

	Character->SetActorHiddenInGame(true);
	Character->SetActorEnableCollision(false);
	Character->SetReplicates(false);

	/*if (Character->AbilitySystem)
	{
		Character->AbilitySystem->InitAttributes(true);
	}*/

	// Load Data onto Character Object
	check(DatabaseEngine);

	FDatabaseLoadCharacterCompleted OnCompleted = FDatabaseLoadCharacterCompleted::CreateUObject(this, &ThisClass::SpawnCharacter_Stage2, TWeakObjectPtr<AIBaseCharacter>(Character), TWeakObjectPtr<AIPlayerController>(PlayerController), bOverrideSpawn, OverrideTransform, OnSpawn);

	FWaitForDatabaseWrite* DatabaseWriteDel = CharacterSavesInProgress.Find(CharacterUID);
	if (DatabaseWriteDel)
	{
		DatabaseWriteDel->AddUObject(this, &AIGameMode::SpawnCharacter_Stage1_DBWrite, TWeakObjectPtr<AIBaseCharacter>(Character), CharacterUID, OnCompleted);
	} else {
		DatabaseEngine->LoadCharacter(CharacterUID, OnCompleted);
	}
}

void AIGameMode::SpawnCharacter_Stage1_DBWrite(TWeakObjectPtr<AIBaseCharacter> Character, FAlderonUID CharacterUID, FDatabaseLoadCharacterCompleted OnCompleted)
{
	check(DatabaseEngine);
	if (Character.IsValid())
	{
		DatabaseEngine->LoadCharacter(CharacterUID, OnCompleted);
	}
}

void AIGameMode::SpawnCharacter_Stage2(const FDatabaseLoadCharacter& Data, TWeakObjectPtr<AIBaseCharacter> Character, TWeakObjectPtr<AIPlayerController> PlayerController, bool bOverrideSpawn, FTransform OverrideTransform, FAsyncCharacterSpawned OnSpawn)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("AIGameMode::SpawnCharacter_Stage2"))

	if (!Character.IsValid())
	{
		return;
	}

	if (!PlayerController.IsValid())
	{
		Character->Destroy();
		return;
	}

	AIPlayerState* const IPlayerState = PlayerController->GetPlayerState<AIPlayerState>();
	if (!IPlayerState)
	{
		return;
	}

	// Game Session is needed for kicking players with invalid state
	AIGameSession* IGameSession = Cast<AIGameSession>(GameSession);
	check(IGameSession);
	if (!IGameSession) return;

	if (!Data.CharacterDataJson.IsValid())
	{
		return;
	}

	// Note: All data from the character is untrusted at this point and needs validation
	IAlderonDatabase::DeserializeObject(Character.Get(), Data.CharacterDataJson);

	AIDinosaurCharacter* IDinosaurCharacter = Cast<AIDinosaurCharacter>(Character.Get());

	UCharacterDataAsset* CharacterDataAsset = UIGameInstance::LoadCharacterData(Character->CharacterDataAssetId);
	check(CharacterDataAsset);

	if (!CharacterDataAsset || !CharacterDataAsset->IsReleased())
	{
		IGameSession->KickPlayer(PlayerController.Get(), FText::FromStringTable(FName(TEXT("ST_ErrorMessages")), TEXT("InvalidCharacterData")));
		return;
	}

	FCharacterData CharacterData{};
	CharacterData.SelectedSkin = IDinosaurCharacter->GetSkinData();
	CharacterData.PreviousSkin = IDinosaurCharacter->GetPreviousSkinData();

	if (UIGameInstance::TryFixUnOwnedSkin(CharacterData, CharacterDataAsset, IPlayerState))
	{
		// Do this twice to skip skin shedding.
		IDinosaurCharacter->SetSkinData(CharacterData.SelectedSkin);
		IDinosaurCharacter->SetSkinData(CharacterData.SelectedSkin);

		if (AIPlayerController* const IPlayerController = PlayerController.Get())
		{
			IPlayerController->ClientReceiveMessageBox(FText::FromStringTable(TEXT("ST_CharacterEdit"), TEXT("SelectedSkinNoLongerAvailable")), FText::FromStringTable(TEXT("ST_CharacterEdit"), TEXT("Info")));
		}
	}

	IPlayerState->SetCharacterType(CharacterDataAsset->CharacterType);

	// Validate Skin Modification and Character Data Matches
	if (IDinosaurCharacter)
	{
		if ((IDinosaurCharacter->GetSkinData().CharacterAssetId != Character->CharacterDataAssetId) || (IDinosaurCharacter->GetPreviousSkinData().CharacterAssetId != Character->CharacterDataAssetId))
		{
			IGameSession->KickPlayer(PlayerController.Get(), FText::FromStringTable(FName(TEXT("ST_ErrorMessages")), TEXT("InvalidCharacterData")));
			return;
		}
	}
	
	if (Character->DeathInfo.bPermaDead && IGameSession->bPermaDeath)
	{
		//This should not happen as the spawn should be rejected earlier.
		UE_LOG(LogTemp, Warning, TEXT("Late rejecting log in from permanently dead character %s"), *Character->GetName());
		Character->Destroy();
		return;
	}
	else if (!IGameSession->bPermaDeath)
	{
		Character->DeathInfo.bPermaDead = false;
	}

	const FString LevelName = UGameplayStatics::GetCurrentLevelName(this, true);
	bool bServerMapChange = (Character->MapName != LevelName);

	if (!bServerMapChange)
	{
		if (AIWorldSettings* IWorldSettings = AIWorldSettings::Get(this))
		{
			bServerMapChange = !IWorldSettings->IsInWorldBounds(Character->SaveCharacterPosition);

			if (bServerMapChange)
			{
				UE_LOG(TitansNetwork, Warning, TEXT("AIGameMode::SpawnCharacter_Stage2: Character is out of bounds, setting bServerMapChange"));
			}
		}
	}

	Character->SetCharacterID(Data.CharacterId);
	Character->SetCombatLogAlderonId(IPlayerState->GetAlderonID());

	const bool bGrowthEnabled = UIGameplayStatics::IsGrowthEnabled(this);

	if (ensure(Character->AbilitySystem))
	{
		float Growth = 0.f;
		const bool bDeserializedSuccess = IAlderonDatabase::DeserializeGameplayAttribute(Data.CharacterDataJson, IAlderonDatabase::GetAttributeSerializedName("Growth"), Growth);

		if (bDeserializedSuccess)
		{
			Character->AbilitySystem->SetNumericAttributeBase(UCoreAttributeSet::GetGrowthAttribute(), Growth);
		}

		// If a character plays in a growth disabled game their growth value will be overriden and saved as maxiumum.
		// To allow future session on growth enabled servers save the Growth value before it's modified.
		if (!bGrowthEnabled && Character->WasGrowthUsedLast() && bDeserializedSuccess)
		{
			Character->SetActualGrowth(Growth);
		}
		
		Character->AbilitySystem->InitAttributes(true);
		IAlderonDatabase::DeserializeGameplayAttributes(Character->AbilitySystem, Data.CharacterDataJson);

		if (AIWorldSettings* WS = AIWorldSettings::GetWorldSettings(this))
		{
			if (AIUltraDynamicSky* UDS = WS->UltraDynamicSky)
			{
				Character->AbilitySystem->NotifyTimeOfDay(UDS->LocalTimeOfDay);
			}
		}
	}

	//Set this to the current value to ensure it's not 0 next save.
	Character->LastSaveTime = GetWorld()->GetTimeSeconds();

	// If respawning, we will be setting this data later.
	if (IDinosaurCharacter != nullptr && !IDinosaurCharacter->bRespawn)
	{
		IDinosaurCharacter->ValidateAndFixBabySkinTint();
		IDinosaurCharacter->UpdateSkinData();
		IDinosaurCharacter->ValidateAndFixSheddingValues();
	}

	// Detect Level Changes and store it and respawn the character so it doesn't get stuck under the map since the 
	// coords are different for different maps
	bool bSkipStatReset = false;
	if (bServerMapChange)
	{
		Character->MapName = LevelName;
		if (!Character->bRespawn)
		{
			Character->bRespawn = true;
			bSkipStatReset = true;
		}
		Character->FallbackCaveReturnPosition = FVector::ZeroVector;
		Character->FallbackCaveReturnRotation = FRotator::ZeroRotator;
	} 

	IAlderonCommon& AlderonCommon = IAlderonCommon::Get();

	if (Character->LoggedOutInInstance(UGameplayStatics::GetCurrentLevelName(this)) && !UIGameplayStatics::AreHomeCavesEnabled(this))
	{
		Character->bRespawn = true;
		Character->InstanceLogoutInfo.Empty();
		SaveCharacter(Character.Get(), ESavePriority::High);
	}
	// Check for the respawn flag, reset position and stats
	if (Character->bRespawn)
	{
		//Moved this call to the end of the function so that the instance loading code knows if it needs to respawn or place the player where they are.
		//Character->bRespawn = false;

		// Get New Random Spawn Point. This will be overriden if we're respawning in an instance.
		const FTransform SpawnTransform = FindRandomSpawnPointForCharacter(Character.Get(), IPlayerState, PlayerController.Get());
		Character->SaveCharacterRotation = FRotator(0, SpawnTransform.Rotator().Yaw, 0);
		Character->SaveCharacterPosition = SpawnTransform.GetLocation();

		if (!Character->GetActiveQuests().IsEmpty())
		{
			Character->GetActiveQuests_Mutable().Empty();
		}
		
		//Character->QuestSaves.Empty();

		Character->DeathInfo.Lifetime = 0.f;

		// Update growth on spawning to the value set if we died
		// Also clear it so this only triggers once
		if (Character->CalculatedGrowthForRespawn != 0.0f)
		{
			Character->SetGrowthPercent(Character->CalculatedGrowthForRespawn);
			Character->CalculatedGrowthForRespawn = 0.0f;
		}

		if (!bSkipStatReset)
		{
			// Reset Stats
			Character->ResetStats();
		}
	}
	// If the player logged out in a cave we may need to respawn it
	else if (Character->LoggedOutInInstance(UGameplayStatics::GetCurrentLevelName(this)))
	{
		// Handle special use case if someone logged out in a cave but spawning at a waystone. We don't need to spawn their cave
		if (bOverrideSpawn)
		{
			Character->RemoveInstanceLogoutInfo(UGameplayStatics::GetCurrentLevelName(this));
		}
	}

	FInstancedTileSpawned FinishSpawningDelegate = FInstancedTileSpawned::CreateLambda([this, bOverrideSpawn, OverrideTransform, Character, PlayerController, bGrowthEnabled, IPlayerState, OnSpawn, CharacterDataAsset](FInstancedTile Tile)
	{
		FTransform FinalTransform = (bOverrideSpawn == true) ? OverrideTransform : FTransform(Character->SaveCharacterRotation, Character->SaveCharacterPosition, FVector::OneVector);
		
		IAlderonCommon& AlderonCommon = IAlderonCommon::Get();

		if (FInstanceLogoutSaveableInfo* InstanceLogoutInfo = Character->GetInstanceLogoutInfo(UGameplayStatics::GetCurrentLevelName(this)))
		{
			FinalTransform.SetLocation(InstanceLogoutInfo->CaveReturnLocation);
			FinalTransform.SetRotation(FQuat::MakeFromEuler(FVector(0, 0, InstanceLogoutInfo->CaveReturnYaw)));
		}
		

#if WITH_EDITOR
		UWorld* World = GetWorld();
		for (TActorIterator<APlayerStart> It(World); It; ++It)
		{
			APlayerStart* PlayerStart = *It;

			if (PlayerStart->IsA<APlayerStartPIE>())
			{
				FinalTransform = PlayerStart->GetActorTransform();
				break;
			}
		}
#endif
		Character->SetActorEnableCollision(true);
		
		FinalTransform.SetRotation(FQuat::MakeFromEuler(FVector(0, 0, FinalTransform.GetRotation().Rotator().Yaw)));
		UGameplayStatics::FinishSpawningActor(Character.Get(), FinalTransform);

		Character->SetActorHiddenInGame(false);
		Character->SetReplicates(true);

		// Character's half height doesn't account for growth until the character actor
		// has finished spawning. We can account for that by snapping to ground as soon as
		// it's spawned
		Character->SnapToGround();

		// If the player previously was an admin character, we can skip adding a login debuff
		bool bWasPreviouslyAdminCharacter = PlayerController->bSpawningFromAdminCharacter;
		PlayerController->bSpawningFromAdminCharacter = false;

		// Possess Character
		PlayerController->Possess(Character.Get());

		// Handle Loading
		PlayerController->PostSpawnCharacter(FinalTransform.GetLocation());

		if (GetNetMode() == ENetMode::NM_ListenServer)
		{
			PlayerController->PostSpawnCharacter_Implementation(FinalTransform.GetLocation());
		}

		if (Tile.IsValid())
		{
			Tile.SpawnedTile->Tile = Tile;
			Tile.SpawnedTile->InitialSetup(Character.Get());
			Character->ServerEnterInstance(Tile);
		}
		
		if (AIGameSession* IGameSession = Cast<AIGameSession>(GameSession))
		{
			AddLoginDebuffToCharacter(*Character, IGameSession->bServerAntiRevengeKill, bWasPreviouslyAdminCharacter);

			AIGameState* IGameState = UIGameplayStatics::GetIGameStateChecked(this);
			check(IGameState);

			AIDinosaurCharacter* DinoCharacter = Cast<AIDinosaurCharacter>(Character.Get());

			if (DinoCharacter != nullptr && DinoCharacter->bRespawn)
			{
				DinoCharacter->ValidateAndFixBabySkinTint();
				DinoCharacter->UpdateSkinData();
				DinoCharacter->ValidateAndFixSheddingValues();
			}

			if (bGrowthEnabled && IGameState->GetGameStateFlags().bGrowthWellRestedBuff)
			{
				check(DinoCharacter);

				if (Character->bRespawn)
				{
					UPOTAbilitySystemGlobals::ApplyDynamicGameplayEffect(DinoCharacter, UCoreAttributeSet::GetWellRestedBonusStartedGrowthAttribute(), 0.0f, EGameplayModOp::Override);
					UPOTAbilitySystemGlobals::ApplyDynamicGameplayEffect(DinoCharacter, UCoreAttributeSet::GetWellRestedBonusEndGrowthAttribute(), 0.0f, EGameplayModOp::Override);
					UPOTAbilitySystemGlobals::ApplyDynamicGameplayEffect(DinoCharacter, UCoreAttributeSet::GetWellRestedBonusMultiplierAttribute(), 1.0f, EGameplayModOp::Override);
				}
				else
				{
					FCharacterData& CharacterData = IPlayerState->CharactersData[Character->GetCharacterID()];

					FInstanceLogoutSaveableInfo* InstanceLogoutInfo = Character->GetInstanceLogoutInfo(UGameplayStatics::GetCurrentLevelName(this));

					if (InstanceLogoutInfo && CharacterData.LastPlayedDate != FDateTime() && (CharacterData.bHasFinishedHatchlingTutorial || !UIGameplayStatics::AreHatchlingCavesEnabled(this)))
					{
						const float PreviousWellRestedBonusGrowth = FMath::Clamp((DinoCharacter->GetWellRestedBonusEndGrowth() - DinoCharacter->GetGrowthPercent()), 0.0f, 0.05f);
						const FTimespan TimeSinceLastPlayed = (FDateTime::UtcNow() - CharacterData.LastPlayedDate);
						const float RewardGrowth = FMath::Clamp((0.005f * (TimeSinceLastPlayed.GetTotalHours() > 1.0f ? TimeSinceLastPlayed.GetTotalHours() : 0.0f)), PreviousWellRestedBonusGrowth, 0.05f);

						UPOTAbilitySystemGlobals::ApplyDynamicGameplayEffect(DinoCharacter, UCoreAttributeSet::GetWellRestedBonusStartedGrowthAttribute(), DinoCharacter->GetGrowthPercent(), EGameplayModOp::Override);
						UPOTAbilitySystemGlobals::ApplyDynamicGameplayEffect(DinoCharacter, UCoreAttributeSet::GetWellRestedBonusEndGrowthAttribute(), FMath::Clamp(DinoCharacter->GetGrowthPercent() + RewardGrowth, 0.0f, 1.0f), EGameplayModOp::Override);
					}

					DinoCharacter->ApplyWellRestedBonusMultiplier();
				}
			}
		}

		// Clear environmental effects and re-apply any relevant ones.
		if (Character->AbilitySystem != nullptr)
		{
			Character->AbilitySystem->RemoveActiveEffectsWithTags(FGameplayTagContainer(UPOTAbilitySystemGlobals::Get().EnvironmentalEffectTag));
		}
		TArray<AActor*> GAVolumes;
		Character->GetOverlappingActors(GAVolumes, AIGameplayAbilityVolume::StaticClass());
		for (AActor* GAVolume : GAVolumes)
		{
			PlayerController->ServerNotifyGAVolumeOverlap(true, Cast<AIGameplayAbilityVolume>(GAVolume));
		}

		// Don't assign a random quest to users who are going to get tutorial quests in a hatchling cave
		AIQuestManager* QuestMgr = AIWorldSettings::GetWorldSettings(this)->QuestManager;
		if (QuestMgr && Character->HasLeftHatchlingCave())
		{
			QuestMgr->AssignRandomPersonalQuests(Character.Get());
		}
		
		// Temp Refresh Marks on Spawn Character
		Character->AddMarks(0);
		Character->bRespawn = false;
		Character->AddCooldownsAfterLoad();
		Character->AddGameplayEffectsAfterLoad();
		Character->DirtySerializedNetworkFields();

		TArray<FSkinUnlockData> UnlockedSkins = IPlayerState->GetUnlockedSkins();
		TArray<FPrimaryAssetId> SkinAssetIDs{};
		SkinAssetIDs.Reserve(UnlockedSkins.Num());

		for (FSkinUnlockData SkinUnlockData : UnlockedSkins)
		{
			if (CharacterDataAsset->Skins.Contains(SkinUnlockData.GetPrimaryAssetId()))
			{
				SkinAssetIDs.Add(SkinUnlockData.GetPrimaryAssetId());
			}
		}

		FSkinDataAssetsLoaded SkinsLoadedDelegate = FSkinDataAssetsLoaded::CreateLambda([this, IPlayerState, Character](bool bSuccess, TArray<FPrimaryAssetId> SkinAssetIDs, TArray<USkinDataAsset*> SkinDataAssets, TSharedPtr<FStreamableHandle> Handle) {
			bool bFixApplied = false;
			TArray<FSkinUnlockData>& UnlockedSkins = IPlayerState->GetUnlockedSkins_Mutable();

			for (USkinDataAsset* SkinDataAsset : SkinDataAssets)
			{
				if (SkinDataAsset->IsEnabled())
				{
					continue;
				}

				const FSkinUnlockData* const FoundSkin = UnlockedSkins.FindByPredicate([SkinDataAsset](FSkinUnlockData x) {
					return x.GetPrimaryAssetId() == SkinDataAsset->GetPrimaryAssetId();
				});

				if (FoundSkin)
				{
					// The inital purchase of a skin puts it at upgrade level 1 so subtrack a level for just the upgrades
					Character->AddMarks(SkinDataAsset->PurchaseCostMarks + SkinDataAsset->UpgradeCostMarks * (FoundSkin->UpgradeLevel - 1));
					UnlockedSkins.Remove(*FoundSkin);
					bFixApplied = true;
				}
			}

			if (bFixApplied)
			{
				SavePlayerState(IPlayerState, ESavePriority::Medium);
			}
		});

		UIGameInstance::AsyncLoadSkinsData(SkinAssetIDs, SkinsLoadedDelegate, ESkinLoadType::All);


		if (AIGameSession::UseWebHooks(WEBHOOK_PlayerRespawn))
		{
			TMap<FString, TSharedPtr<FJsonValue>> WebHookProperties
			{
				{ TEXT("PlayerName"), MakeShareable(new FJsonValueString(IPlayerState->GetPlayerName()))},
				{ TEXT("PlayerAlderonId"), MakeShareable(new FJsonValueString(IPlayerState->GetAlderonID().ToDisplayString()))},
				{ TEXT("Location"), MakeShareable(new FJsonValueString(FinalTransform.GetLocation().ToString()))},
				{ TEXT("DinosaurType"), MakeShareable(new FJsonValueString(IPlayerState->GetCharacterSpecies().ToString()))},
				{ TEXT("DinosaurGrowth"), MakeShareable(new FJsonValueNumber(Character->GetGrowthPercent()))},
			};
			AIGameSession::TriggerWebHookFromContext(this, WEBHOOK_PlayerRespawn, WebHookProperties);
		}

		OnSpawn.ExecuteIfBound(PlayerController.Get(), Character.Get());
		OnCharacterSpawn(PlayerController.Get(), Character.Get());

		// A mod can hook the spawn to do extra things
		if (Tile.IsValid())
		{
			OnPlayerSpawnInstance(PlayerController.Get(), Character.Get(), bOverrideSpawn, FinalTransform, Tile.SpawnedTile);
		}
		else
		{
			OnPlayerSpawn(PlayerController.Get(), Character.Get(), bOverrideSpawn, FinalTransform);
		}
	});

	bool bSlottedAbilitiesChanged = false;

	UTitanAssetManager& AssetManager = static_cast<UTitanAssetManager&>(UAssetManager::Get());

	for (FSlottedAbilities& Abilities : Character->GetSlottedAbilityAssetsArray_Mutable())
	{
		for (int Index = Abilities.SlottedAbilities.Num() - 1; Index >= 0; Index--)
		{
			FPrimaryAssetId AbilityId = Abilities.SlottedAbilities[Index];

			UPOTAbilityAsset* AbilityAsset = Cast<UPOTAbilityAsset>(AssetManager.GetPrimaryAssetObject(AbilityId));
			if (!AbilityAsset)
			{
				AbilityAsset = AssetManager.ForceLoadAbility(AbilityId);
			}

			if (!UIAbilitySlotsEditor::IsAbilityEnabled(AbilityAsset))
			{
				UE_LOG(TitansLog, Log, TEXT("AIGameMode::SpawnCharacter_Stage2: Removed disabled ability \"%s\""), *AbilityId.ToString());
				if (AbilityAsset)
				{
					Character->AddMarks(AbilityAsset->UnlockCost);
				}
				bSlottedAbilitiesChanged = true;
				Abilities.SlottedAbilities.RemoveAt(Index);
				continue;
			}

			if (!UIAbilitySlotsEditor::CheckAbilityCompatibilityWithCharacter(Character.Get(), AbilityAsset))
			{
				if (AbilityAsset)
				{
					Character->AddMarks(AbilityAsset->UnlockCost);
				}
				UE_LOG(TitansLog, Log, TEXT("AIGameMode::SpawnCharacter_Stage2: Removed ability as it doesn't belong to this character \"%s\""), *AbilityId.ToString());
				bSlottedAbilitiesChanged = true;
				Abilities.SlottedAbilities.RemoveAt(Index);
			}

			// Check if the ability is slotted in the wrong category
			if (AbilityAsset && AbilityAsset->AbilityCategory != Abilities.Category)
			{
				UE_LOG(TitansLog, Log, TEXT("AIGameMode::SpawnCharacter_Stage2: Removed ability slotted in the wrong category \"%s\""), *AbilityId.ToString());
				bSlottedAbilitiesChanged = true;
				Abilities.SlottedAbilities.RemoveAt(Index);
			}
		}
	}

	// If character slotted abilities were changed also update ActionBarCustomizationVersion so that
	// AIBaseCharacter::PossessedBy does NOT override them with their default values.
	if (bSlottedAbilitiesChanged)
	{
		Character->ActionBarCustomizationVersion = static_cast<EActionBarCustomizationVersion>(static_cast<int32>(EActionBarCustomizationVersion::MAX) - 1);
	}

	// Fetch Quest Manager
	AIQuestManager* QuestMgr = AIWorldSettings::GetWorldSettings(this)->QuestManager;
	if (QuestMgr)
	{
		// Load Quests
		QuestMgr->LoadQuests(Character.Get(), bServerMapChange);
	}
	
	const bool bHasHatchlingTutorialQuests = (QuestMgr) ? QuestMgr->HasTutorialQuest(Character.Get()) : false;

	AIGameState* IGameState = UIGameplayStatics::GetIGameStateChecked(this);
	check(IGameState);

	// Handle Hatchling Caves
	bool bRequiresHatchlingCave = false;
	const bool bUnderHatchlingCaveGrowthLevel = (Character->GetGrowthPercent() < IGameState->GetGameStateFlags().HatchlingCaveExitGrowth);
	
	FPrimaryAssetId HatchlingAssetId;
	HatchlingAssetId = GetHatchlingCaveForCharacter(Character.Get());

	// If there is no hatchling cave asset for the spawned character then dont spawn them in a cave.
	if (UIGameplayStatics::AreHatchlingCavesEnabled(this) && HatchlingAssetId.IsValid())
	{
		if (bUnderHatchlingCaveGrowthLevel)
		{
			if (!Character->HasLeftHatchlingCave())
			{
				bRequiresHatchlingCave = true;
			}
		}
	}

	// Remove Old tutorial quests if you don't need them anymore
	if (!bRequiresHatchlingCave)
	{
		if (QuestMgr && bHasHatchlingTutorialQuests)
		{
			QuestMgr->ClearTutorialQuests(Character.Get());
			QuestMgr->ClearCompletedQuests(Character.Get()); 
		}

		const bool bHasLeftHatchlingCave = Character->HasLeftHatchlingCave();

		if (!bHasLeftHatchlingCave)
		{
			Character->SetHasLeftHatchlingCave(true);
			Character->SetReadyToLeaveHatchlingCave(true);
			Character->InstanceId = FPrimaryAssetId();
			Character->RemoveInstanceLogoutInfo(UGameplayStatics::GetCurrentLevelName(this));

			if (Character->GetGrowthPercent() <= IGameState->GetGameStateFlags().HatchlingCaveExitGrowth)
			{
				Character->RestoreHealth(Character->GetMaxHealth());
				Character->RestoreThirst(Character->GetMaxThirst());
				Character->RestoreHunger(Character->GetMaxHunger());

				if (bGrowthEnabled && !UIGameplayStatics::AreHatchlingCavesEnabled(this) && IGameSession->HatchlingCaveExitGrowth > MIN_GROWTH)
				{
					UPOTAbilitySystemGlobals::ApplyDynamicGameplayEffect(Character.Get(), UCoreAttributeSet::GetGrowthAttribute(), IGameSession->HatchlingCaveExitGrowth, EGameplayModOp::Override);
				}
			}
		}

		const bool bHasNoAbilities = (Character->GetSlottedAbilityCategories().Num() == 0);
		if (bHasNoAbilities)
		{
			Character->GetSlottedAbilityCategories_Mutable() = Character->GetClass()->GetDefaultObject<AIBaseCharacter>()->GetSlottedAbilityCategories();
			if (Character->GetSlottedAbilityAssetsArray().Num() == 0)
			{
				Character->GetSlottedAbilityAssetsArray_Mutable() = Character->GetClass()->GetDefaultObject<AIBaseCharacter>()->GetSlottedAbilityAssetsArray();
			}
		}
	}

	if (Character->LoggedOutInOwnedInstance(UGameplayStatics::GetCurrentLevelName(this)) && !bRequiresHatchlingCave)
	{
		AsyncSpawnInstancedTile(Character.Get(), Character->DefaultHomecaveInstanceId, FinishSpawningDelegate);
	}
	else
	{
		if (bRequiresHatchlingCave)
		{
			if (AIHatchlingCave* ExistingHatchlingCave = AIHatchlingCave::FindCompatibleSpawnedCave(Character.Get()))
			{
				FinishSpawningDelegate.Execute(ExistingHatchlingCave->Tile);
			}
			else
			{
				// Baby caves have no owners
				if (HatchlingAssetId.IsValid())
				{
					AsyncSpawnInstancedTile(nullptr, HatchlingAssetId, FinishSpawningDelegate);
				}
				else
				{
					FinishSpawningDelegate.Execute(FInstancedTile());
				}
			}
		}
		else
		{
			FinishSpawningDelegate.Execute(FInstancedTile());
		}
	}

	if (!bGrowthEnabled)
	{
		UPOTAbilitySystemGlobals::ApplyDynamicGameplayEffect(Character.Get(), UCoreAttributeSet::GetGrowthAttribute(), 1, EGameplayModOp::Override);
	}
	else if (Character->ShouldUseActualGrowth())
	{
		UPOTAbilitySystemGlobals::ApplyDynamicGameplayEffect(Character.Get(), UCoreAttributeSet::GetGrowthAttribute(), Character->GetActualGrowth(), EGameplayModOp::Override);
	}

	// Growth used last is used to determine if "ActualGrowth" should be applied
	// It needs to be set for the next time the character is loaded after all other growth loading is complete
	Character->SetGrowthUsedLast(bGrowthEnabled);
}

void AIGameMode::LoadAdminAsync(AIPlayerController* PlayerController, TSoftClassPtr<AIAdminCharacter> AdminClassRef)
{
	TSoftClassPtr<AIAdminCharacter> TargetAdminClassRef;
	if (AdminClassRef != nullptr)
	{
		TargetAdminClassRef = AdminClassRef;
	}
	else
	{
		TargetAdminClassRef = DefaultAdminClassRef;
	}
	if (!TargetAdminClassRef.IsValid())
	{
		FStreamableDelegate SpawnDelegate = FStreamableDelegate::CreateUObject(this, &AIGameMode::SpawnAdmin, PlayerController, TargetAdminClassRef);
		check(SpawnDelegate.IsBound());
		FStreamableManager& AssetLoader = UIGameplayStatics::GetStreamableManager(this);
		AssetLoader.RequestAsyncLoad(TargetAdminClassRef.ToSoftObjectPath(), SpawnDelegate, FStreamableManager::AsyncLoadHighPriority);
	}
	else
	{
		SpawnAdmin(PlayerController, TargetAdminClassRef);
	}
}

void AIGameMode::SpawnAdmin(AIPlayerController* PlayerController, TSoftClassPtr<AIAdminCharacter> AdminClassRef)
{
	TSoftClassPtr<AIAdminCharacter> TargetAdminClassRef;
	if (AdminClassRef != nullptr)
	{
		TargetAdminClassRef = AdminClassRef;
	}
	else
	{
		TargetAdminClassRef = DefaultAdminClassRef;
	}
	if (TargetAdminClassRef.Get())
	{
		FTransform AdminSpawnTransform;
		bool bRequiresReload = true;
		bool bSendWebhook = false;
		if (APawn* CurrentPlayerPawn = PlayerController->GetPawn())
		{
			// Player already has a pawn so let's spawn the admin character at the same spot
			AdminSpawnTransform.SetRotation(CurrentPlayerPawn->GetActorRotation().Quaternion());
			AdminSpawnTransform.SetLocation(CurrentPlayerPawn->GetActorLocation());
			PlayerController->UnPossess();
			CurrentPlayerPawn->Destroy();
			bRequiresReload = false;
			if (!Cast<AIAdminCharacter>(CurrentPlayerPawn)) // Only send a webhook if the player is not already spectating
			{
				bSendWebhook = true;
			}
		}
		else
		{
			// Get New Random Spawn Point
			AdminSpawnTransform.SetRotation(FRotator(EForceInit::ForceInitToZero).Quaternion());
			if (GenericSpawnPoints.Num() != 0)	
			{
				int RandomSpawnIndex = FMath::RandRange(0, GenericSpawnPoints.Num() - 1);
				AdminSpawnTransform.SetLocation(GenericSpawnPoints[RandomSpawnIndex]);
			}
			else if(CustomSpawnPoints.Num() != 0)
			{
				int RandomSpawnIndex = FMath::RandRange(0, CustomSpawnPoints.Num() - 1);
				if (APlayerStart* Start = CustomSpawnPoints[RandomSpawnIndex])
				{
					AdminSpawnTransform.SetLocation(Start->GetActorLocation());
				}
			}

			bSendWebhook = true;
		}

		if (bSendWebhook)
		{
			// Webhook for entering spectator mode
			if (AIGameSession::UseWebHooks(WEBHOOK_AdminSpectate))
			{
				const AIPlayerState* const IPlayerState = PlayerController->GetPlayerState<AIPlayerState>();
				check(IPlayerState);
				TMap<FString, TSharedPtr<FJsonValue>> WebHookProperties
				{
					{ TEXT("AdminName"), MakeShareable(new FJsonValueString(IPlayerState->GetPlayerName()))},
					{ TEXT("AdminAlderonId"), MakeShareable(new FJsonValueString(IPlayerState->GetAlderonID().ToDisplayString()))},
					{ TEXT("Action"), MakeShareable(new FJsonValueString(TEXT("Entered Spectator Mode"))) },
				};
				AIGameSession::TriggerWebHookFromContext(PlayerController, WEBHOOK_AdminSpectate, WebHookProperties);
			}
		}

		FTransform TempSpawnTransform = FTransform(FRotator(0, 0, 0), FVector(0, 0, 0), FVector::OneVector);

		AIAdminCharacter* AdminPawn = Cast<AIAdminCharacter>(UGameplayStatics::BeginDeferredActorSpawnFromClass
		(
			this,
			TargetAdminClassRef.Get(),
			TempSpawnTransform,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn,
			PlayerController
		));

		UGameplayStatics::FinishSpawningActor(AdminPawn, AdminSpawnTransform);

		// Set damage to false 
		AdminPawn->SetCanBeDamaged(false);

		// Possess Character
		PlayerController->Possess(AdminPawn);

		// Handle Loading
		if (bRequiresReload)
		{
			PlayerController->PostSpawnCharacter(AdminSpawnTransform.GetLocation());
		}
		
		if(GetNetMode() == ENetMode::NM_ListenServer)
		{
			if (bRequiresReload) PlayerController->PostSpawnCharacter_Implementation(AdminSpawnTransform.GetLocation());
		}
	}
	else
	{
		UE_LOG(TitansLog, Error, TEXT("IGameMode:SpawnAdmin: TargetAdminClassRef not valid or didn't load properly!"));
	}
}

void AIGameMode::GetAllCharacters(const AIPlayerController* PlayerController, FAsyncCharactersLoaded OnComplete)
{
	check (OnComplete.IsBound());
	if (!OnComplete.IsBound()) return;

	check(PlayerController);
	if (!PlayerController)
	{
		OnComplete.ExecuteIfBound(PlayerController, TArray<FAlderonUID>());
		return;
	}

	check(DatabaseEngine);
	if (!DatabaseEngine)
	{
		OnComplete.ExecuteIfBound(PlayerController, TArray<FAlderonUID>());
		return;
	}

	AIPlayerState* IPlayerState = PlayerController->GetPlayerState<AIPlayerState>();
	check(IPlayerState);
	if (!IPlayerState)
	{
		OnComplete.ExecuteIfBound(PlayerController, TArray<FAlderonUID>());
		return;
	}

	DatabaseEngine->LoadCharacters(IPlayerState->GetAlderonID(), FDatabaseLoadCharactersCompleted::CreateUObject(this, &AIGameMode::GetAllCharacters_Callback, PlayerController, OnComplete));
}

void AIGameMode::GetAllCharacters_Callback(const FDatabaseLoadCharacters& DatabaseLoadCharacters, const AIPlayerController* PlayerController, FAsyncCharactersLoaded OnComplete)
{
	TArray<FAlderonUID> AlderonUIDs;
	for (const FDatabaseLoadCharacter& LoadCharacter : DatabaseLoadCharacters.LoadedCharacters)
	{
		AlderonUIDs.Add(LoadCharacter.CharacterId);
	}

	OnComplete.ExecuteIfBound(PlayerController, AlderonUIDs);
}

void AIGameMode::DeleteCharacterBP(AIPlayerController* PlayerController, FAlderonUID CharacterUID, FAsyncCharacterDeleted OnCompleted)
{
	TWeakObjectPtr<AIPlayerController> PlayerControllerPtr = PlayerController;
	DeleteCharacter(PlayerController, CharacterUID, FAsyncOperationCompleted::CreateLambda([=](bool bSuccess){OnCompleted.ExecuteIfBound(PlayerControllerPtr.Get(), bSuccess);}));
}

void AIGameMode::DeleteCharacter(AIPlayerController* PlayerController, FAlderonUID CharacterUID, FAsyncOperationCompleted OnCompleted)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("AIGameMode::DeleteCharacter"))

	check(DatabaseEngine);
	if (!DatabaseEngine)
	{
		OnCompleted.ExecuteIfBound(false);
		return;
	}

	check(PlayerController);
	if (!PlayerController)
	{
		OnCompleted.ExecuteIfBound(false);
		return;
	}

	if (!CharacterUID.IsValid())
	{
		OnCompleted.ExecuteIfBound(false);
		return;
	}

	AIPlayerState* IPlayerState = PlayerController->GetPlayerState<AIPlayerState>();
	check(IPlayerState);
	if (!IPlayerState)
	{
		OnCompleted.ExecuteIfBound(false);
		return;
	}

	FDatabaseOperationCompleted OnCharacterDeleted = FDatabaseOperationCompleted::CreateUObject(this, &ThisClass::DeleteCharacter_Callback, TWeakObjectPtr<AIPlayerState>(IPlayerState), CharacterUID, OnCompleted);
	DatabaseEngine->DeleteCharacter(CharacterUID, OnCharacterDeleted);
}

void AIGameMode::DeleteCharacter_Callback(const FDatabaseOperationData& Data, TWeakObjectPtr<AIPlayerState> WeakPlayerStatePtr, FAlderonUID CharacterUID, FAsyncOperationCompleted OnCompleted)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("AIGameMode::DeleteCharacter_Callback"))

	AIPlayerState* IPlayerState = WeakPlayerStatePtr.Get();

	if (!IPlayerState || !Data.bSuccess)
	{
		// Player State got destroyed while we were trying to delete a character
		OnCompleted.ExecuteIfBound(false);
		return;
	}

	if (IPlayerState->Characters.Contains(CharacterUID))
	{
		IPlayerState->Characters.Remove(CharacterUID.ToString());
	}

	if (IPlayerState->CharactersData.Contains(CharacterUID))
	{
		IPlayerState->CharactersData.Remove(CharacterUID);
	}

	// Save Player State to update the record that we deleted those characters
	SavePlayerState(IPlayerState, ESavePriority::High);

	OnCompleted.ExecuteIfBound(true);
}

void AIGameMode::MergeCharacters(AIPlayerController* PlayerController, FAlderonUID CharacterToDeleteUID, FAlderonUID CharacterToReceiveUID, FAsyncOperationCompleted OnCompleted)
{
	AIPlayerState* IPlayerState = PlayerController->GetPlayerState<AIPlayerState>();
	check(IPlayerState);
	if (!IPlayerState)
	{
		UE_LOG(TitansLog, Error, TEXT("AIGameMode:MergeCharacters: IPlayerState is null."));
		OnCompleted.ExecuteIfBound(false);
		return;
	}

	if (!IPlayerState->CharactersData.Contains(CharacterToDeleteUID) || !IPlayerState->CharactersData.Contains(CharacterToReceiveUID))
	{
		UE_LOG(TitansLog, Error, TEXT("AIGameMode:MergeCharacters: Invalid character UID."));
		OnCompleted.ExecuteIfBound(false);
		return;
	}

	FCharacterData CharacterDataToDelete = IPlayerState->CharactersData[CharacterToDeleteUID];
	FCharacterData CharacterDataToReceive = IPlayerState->CharactersData[CharacterToReceiveUID];
	if (IPlayerState->CharactersData[CharacterToDeleteUID].SelectedSkin.CharacterAssetId != IPlayerState->CharactersData[CharacterToReceiveUID].SelectedSkin.CharacterAssetId)
	{
		UE_LOG(TitansLog, Error, TEXT("AIGameMode:MergeCharacters: Character asset ids do not match."));
		OnCompleted.ExecuteIfBound(false);
		return;
	}

	AIGameSession* IGameSession = Cast<AIGameSession>(GameSession);
	check(IGameSession);
	if (IGameSession && IGameSession->bPermaDeath)
	{
		if (CharacterDataToDelete.DeathInfo.bPermaDead || CharacterDataToReceive.DeathInfo.bPermaDead)
		{
			UE_LOG(TitansLog, Error, TEXT("AIGameMode:MergeCharacters: Cannot merge characters that are perma dead."));
			OnCompleted.ExecuteIfBound(false);
			return;
		}
	}

	FDatabaseLoadCharacterCompleted OnCharacterToDeleteLoaded = FDatabaseLoadCharacterCompleted::CreateUObject(this, &AIGameMode::MergeCharacters_CharacterToDeleteLoaded, PlayerController, CharacterToDeleteUID, CharacterToReceiveUID, OnCompleted);
	DatabaseEngine->LoadCharacter(CharacterToDeleteUID, OnCharacterToDeleteLoaded);
}

void AIGameMode::MergeCharacters_CharacterToDeleteLoaded(const FDatabaseLoadCharacter& CharacterToDeleteLoadData, AIPlayerController* PlayerController, FAlderonUID CharacterToDeleteUID, FAlderonUID CharacterToReceiveUID, FAsyncOperationCompleted OnCompleted)
{
	if (!IsValid(PlayerController))
	{
		UE_LOG(TitansLog, Error, TEXT("AIGameMode:MergeCharacters_CharacterToDeleteLoaded: PlayerController is null."));
		OnCompleted.ExecuteIfBound(false);
		return;
	}

	AIPlayerState* IPlayerState = PlayerController->GetPlayerState<AIPlayerState>();
	if (!IsValid(IPlayerState))
	{
		UE_LOG(TitansLog, Error, TEXT("AIGameMode:MergeCharacters_CharacterToDeleteLoaded: IPlayerState is null."));
		OnCompleted.ExecuteIfBound(false);
		return;
	}

	if (!IPlayerState->CharactersData.Contains(CharacterToDeleteUID))
	{
		UE_LOG(TitansLog, Error, TEXT("AIGameMode:MergeCharacters_CharacterToDeleteLoaded: IPlayerState does not contain character to receive in merge."));
		OnCompleted.ExecuteIfBound(false);
		return;
	}

	FCharacterData CharacterDataToDelete = IPlayerState->CharactersData[CharacterToDeleteUID];

	FCharacterDataAssetLoaded PostLoadCharacterDelegate = FCharacterDataAssetLoaded::CreateLambda([this, OnCompleted, PlayerController, CharacterToDeleteLoadData, CharacterToDeleteUID, CharacterToReceiveUID](bool bSuccess, FPrimaryAssetId CharacterAssetId, UCharacterDataAsset* CharacterDataAsset)
	{
		check(CharacterDataAsset);
		if (!CharacterDataAsset)
		{
			UE_LOG(TitansLog, Error, TEXT("AIGameMode:MergeCharacters_CharacterToDeleteLoaded: CharacterDataAsset is null."));
			OnCompleted.ExecuteIfBound(false);
			return;
		}

		FStreamableDelegate PostLoadCharacterClass = FStreamableDelegate::CreateLambda([this, CharacterDataAsset, OnCompleted, PlayerController, CharacterToDeleteLoadData, CharacterToDeleteUID, CharacterToReceiveUID]()
		{
			TSubclassOf<AIBaseCharacter> CharacterClass = CharacterDataAsset->PreviewClass.Get();
			check(CharacterClass);
			if (!CharacterClass)
			{
				UE_LOG(TitansLog, Error, TEXT("AIGameMode:MergeCharacters_CharacterToDeleteLoaded: CharacterClass is null when we just loaded it."));
				OnCompleted.ExecuteIfBound(false);
				return;
			}

			// Create a character to load the data into
			FTransform SpawnTransform = FTransform(FVector::OneVector);
			AIDinosaurCharacter* Character = Cast<AIDinosaurCharacter>(UGameplayStatics::BeginDeferredActorSpawnFromClass(
				this, CharacterClass, SpawnTransform, ESpawnActorCollisionHandlingMethod::AlwaysSpawn, PlayerController)
				);

			if (!Character)
			{
				UE_LOG(TitansLog, Error, TEXT("AIGameMode:MergeCharacters_CharacterToDeleteLoaded: Character is null when we just attempted to spawn it."));
				OnCompleted.ExecuteIfBound(false);
				return;
			}

			Character->_SetReplicatesPrivate(false);
			Character->SetActorHiddenInGame(true);
			Character->SetActorTickEnabled(false);
			Character->SetActorEnableCollision(false);
			Character->DestroyCosmeticComponents();
			Character->SetLifeSpan(300.0f); // Self destroy after 5 minutes incase to prevent leaks

			check(DatabaseEngine);

			bool bValid = IAlderonDatabase::DeserializeObject(Character, CharacterToDeleteLoadData.CharacterDataJson);
			if (!bValid)
			{
				UE_LOG(TitansLog, Error, TEXT("AIGameMode:MergeCharacters_CharacterToDeleteLoaded: Deserialization failed."));
				OnCompleted.ExecuteIfBound(false);
				Character->Destroy();
				return;
			}

			// Transfer marks
			int MarksToTransfer = Character->GetMarks();

			// Transfer decor items
			TArray<FHomeCaveDecorationPurchaseInfo> DecorationsToTransfer = Character->GetHomeCaveSaveableInfo().BoughtDecorations;
			for (const FHomeCaveDecorationSaveInfo& DecorationSaveInfo : Character->GetHomeCaveSaveableInfo().SavedDecorations)
			{
				bool bPurchaseInfoFound = false;
				for (FHomeCaveDecorationPurchaseInfo& DecorationPurchaseInfo : DecorationsToTransfer)
				{
					if (DecorationPurchaseInfo.DataAsset == DecorationSaveInfo.DataAsset)
					{
						DecorationPurchaseInfo.AmountOwned++;
						bPurchaseInfoFound = true;
						break;
					}
				}

				if (!bPurchaseInfoFound)
				{
					FHomeCaveDecorationPurchaseInfo NewPurchaseInfo = FHomeCaveDecorationPurchaseInfo();
					NewPurchaseInfo.DataAsset = DecorationSaveInfo.DataAsset;
					NewPurchaseInfo.AmountOwned = 1;
					DecorationsToTransfer.Add(NewPurchaseInfo);
				}
			}

			// Refund marks for bought home cave room sockets
			MarksToTransfer += Character->GetRoomSocketRefundAmount();

			// Refund marks for bought home cave rooms except for the default room
			bool DefaultRoomFound = false;
			UHomeCaveExtensionDataAsset* DefaultHomeCaveBaseRoom = Character->DefaultHomecaveBaseRoom.LoadSynchronous();
			for (const FHomeCaveExtensionPurchaseInfo& ExtensionPurchaseInfo : Character->GetHomeCaveSaveableInfo().BoughtExtensions)
			{
				if (!DefaultRoomFound && ExtensionPurchaseInfo.DataAsset == DefaultHomeCaveBaseRoom) {
					DefaultRoomFound = true;
					continue;
				}

				MarksToTransfer += ExtensionPurchaseInfo.DataAsset->MarksCost;
			}

			Character->Destroy();

			FDatabaseLoadCharacterCompleted OnCharacterToReceiveLoaded = FDatabaseLoadCharacterCompleted::CreateUObject(this, &AIGameMode::MergeCharacters_CharacterToReceiveLoaded, MarksToTransfer, DecorationsToTransfer, PlayerController, CharacterToDeleteUID, CharacterToReceiveUID, OnCompleted);
			DatabaseEngine->LoadCharacter(CharacterToReceiveUID, OnCharacterToReceiveLoaded);
		});

		TSoftClassPtr<AIBaseCharacter> CharacterClassSoft = CharacterDataAsset->PreviewClass;
		if (!CharacterClassSoft.Get())
		{
			// Start Async Loading
			FStreamableManager& Streamable = UIGameplayStatics::GetStreamableManager(this);
			Streamable.RequestAsyncLoad(
				CharacterClassSoft.ToSoftObjectPath(),
				PostLoadCharacterClass,
				FStreamableManager::AsyncLoadHighPriority, false
			);
		}
		else {
			PostLoadCharacterClass.ExecuteIfBound();
		}
	});
	UIGameInstance::AsyncLoadCharacterData(CharacterDataToDelete.SelectedSkin.CharacterAssetId, PostLoadCharacterDelegate, ESkinLoadType::None);
}

void AIGameMode::MergeCharacters_CharacterToReceiveLoaded(const FDatabaseLoadCharacter& CharacterToReceiveLoadData, int MarksToTransfer, TArray<FHomeCaveDecorationPurchaseInfo> DecorationsToTransfer, AIPlayerController* PlayerController, FAlderonUID CharacterToDeleteUID, FAlderonUID CharacterToReceiveUID, FAsyncOperationCompleted OnCompleted)
{
	AIPlayerState* IPlayerState = PlayerController->GetPlayerState<AIPlayerState>();
	if (!IPlayerState)
	{
		UE_LOG(TitansLog, Error, TEXT("AIGameMode:MergeCharacters_CharacterToReceiveLoaded: IPlayerState is null."));
		OnCompleted.ExecuteIfBound(false);
		return;
	}

	if (!IPlayerState->CharactersData.Contains(CharacterToReceiveUID))
	{
		UE_LOG(TitansLog, Error, TEXT("AIGameMode:MergeCharacters_CharacterToReceiveLoaded: IPlayerState does not contain character to receive in merge."));
		OnCompleted.ExecuteIfBound(false);
		return;
	}

	FCharacterData CharacterData = IPlayerState->CharactersData[CharacterToReceiveUID];

	FCharacterDataSkinLoaded PostLoadCharacterSkinDelegate = FCharacterDataSkinLoaded::CreateLambda([this, PlayerController, OnCompleted, CharacterToReceiveLoadData, CharacterData, IPlayerState, MarksToTransfer, DecorationsToTransfer, CharacterToDeleteUID, CharacterToReceiveUID](bool bSuccess, FPrimaryAssetId CharacterAssetId, UCharacterDataAsset* CharacterDataAsset, TArray<FPrimaryAssetId> SkinAssetIds, TArray<USkinDataAsset*> SkinDataAssets, TSharedPtr<FStreamableHandle> Handle)
	{
		check(SkinDataAssets.Num() > 0);
		if (SkinDataAssets.Num() == 0)
		{
			UE_LOG(TitansLog, Error, TEXT("AIGameMode:MergeCharacters_CharacterToReceiveLoaded: SkinDataAsset is null."));
			OnCompleted.ExecuteIfBound(false);
			return;
		}

		check(CharacterDataAsset);
		if (!CharacterDataAsset)
		{
			UE_LOG(TitansLog, Error, TEXT("AIGameMode:MergeCharacters_CharacterToReceiveLoaded: CharacterDataAsset is null."));
			OnCompleted.ExecuteIfBound(false);
			return;
		}

		FStreamableDelegate PostLoadCharacterClass = FStreamableDelegate::CreateLambda([this, CharacterDataAsset, PlayerController, OnCompleted, CharacterToReceiveLoadData, CharacterData, IPlayerState, MarksToTransfer, DecorationsToTransfer, Handle, CharacterToDeleteUID, CharacterToReceiveUID]() 
		{
			TSubclassOf<AIBaseCharacter> CharacterClass = CharacterDataAsset->PreviewClass.Get();
			check(CharacterClass);
			if (!CharacterClass)
			{
				UE_LOG(TitansLog, Error, TEXT("AIGameMode:MergeCharacters_CharacterToReceiveLoaded: CharacterClass is null when we just loaded it."));
				OnCompleted.ExecuteIfBound(false);
				return;
			}

			FTransform SpawnTransform = FTransform(FVector::OneVector);
			AIDinosaurCharacter* Character = Cast<AIDinosaurCharacter>(UGameplayStatics::BeginDeferredActorSpawnFromClass(
				this, CharacterClass, SpawnTransform, ESpawnActorCollisionHandlingMethod::AlwaysSpawn, PlayerController)
			);

			if (!Character)
			{
				UE_LOG(TitansLog, Error, TEXT("AIGameMode:MergeCharacters_CharacterToReceiveLoaded: Character is null when we just attempted to spawn it."));
				OnCompleted.ExecuteIfBound(false);
				return;
			}

			Character->_SetReplicatesPrivate(false);
			Character->SetActorHiddenInGame(true);
			Character->SetActorTickEnabled(false);

			// Have to do this so the stats aren't saved over with 0's
			if (Character->AbilitySystem)
			{
				Character->AbilitySystem->InitAttributes(true);
			}

			IAlderonDatabase::DeserializeObject(Character, CharacterToReceiveLoadData.CharacterDataJson);

			if (Character->AbilitySystem != nullptr)
			{
				if (UIGameplayStatics::IsGrowthEnabled(Character))
				{
					float Growth = 0.f;
					if (IAlderonDatabase::DeserializeGameplayAttribute(CharacterToReceiveLoadData.CharacterDataJson, IAlderonDatabase::GetAttributeSerializedName("Growth"), Growth))
					{
						Character->AbilitySystem->SetNumericAttributeBase(UCoreAttributeSet::GetGrowthAttribute(), Growth);
					}
				}

				Character->AbilitySystem->InitAttributes(false);

				IAlderonDatabase::DeserializeGameplayAttributes(Character->AbilitySystem, CharacterToReceiveLoadData.CharacterDataJson);
			}

			Character->SetSkinData(CharacterData.SelectedSkin);

			IPlayerState->CharactersData[CharacterData.CharacterID].SheddingProgress = Character->GetSheddingProgressRaw();

			// Add marks and decor from character that will be deleted
			Character->AddMarks(MarksToTransfer);
			FHomeCaveSaveableInfo& MutableHomeCaveSaveableInfo = Character->GetHomeCaveSaveableInfo_Mutable();
			for (const FHomeCaveDecorationPurchaseInfo& DecorationToTransfer : DecorationsToTransfer)
			{
				bool bDecorationFound = false;
				for (FHomeCaveDecorationPurchaseInfo& BoughtDecoration : MutableHomeCaveSaveableInfo.BoughtDecorations)
				{
					if (DecorationToTransfer.DataAsset == BoughtDecoration.DataAsset)
					{
						BoughtDecoration.AmountOwned += DecorationToTransfer.AmountOwned;
						bDecorationFound = true;
						break;
					}
				}

				if (!bDecorationFound) 
				{
					MutableHomeCaveSaveableInfo.BoughtDecorations.Add(DecorationToTransfer);
				}
			}

			FDatabaseOperationCompleted OnCharacterToReceiveSaved = FDatabaseOperationCompleted::CreateUObject(this, &AIGameMode::MergeCharacters_CharacterToReceiveSaved, Character, PlayerController, CharacterToDeleteUID, CharacterToReceiveUID, OnCompleted);
			DatabaseEngine->SaveCharacter(Character, IPlayerState->GetAlderonID(), CharacterData.CharacterID, OnCharacterToReceiveSaved, ESavePriority::High);

			if (Handle.IsValid())
			{
				Handle->ReleaseHandle();
			}
		});

		TSoftClassPtr<AIBaseCharacter> CharacterClassSoft = CharacterDataAsset->PreviewClass;
		if (!CharacterClassSoft.Get())
		{
			// Start Async Loading
			FStreamableManager& Streamable = UIGameplayStatics::GetStreamableManager(this);
			Streamable.RequestAsyncLoad(
				CharacterClassSoft.ToSoftObjectPath(),
				PostLoadCharacterClass,
				FStreamableManager::AsyncLoadHighPriority, false
			);
		}
		else {
			PostLoadCharacterClass.ExecuteIfBound();
		}
	});

	TSharedPtr<FStreamableHandle> Handle = UIGameInstance::AsyncLoadCharacterSkinData(CharacterData.SelectedSkin.CharacterAssetId, CharacterData.SelectedSkin.SkinAssetId, PostLoadCharacterSkinDelegate, ESkinLoadType::All);
}

void AIGameMode::MergeCharacters_CharacterToReceiveSaved(const FDatabaseOperationData& SaveOperationData, AIDinosaurCharacter* Character, AIPlayerController* PlayerController, FAlderonUID CharacterToDeleteUID, FAlderonUID CharacterToReceiveUID, FAsyncOperationCompleted OnCompleted)
{
	if (!SaveOperationData.bSuccess)
	{
		if (IsValid(Character))
		{
			Character->Destroy();
		}

		UE_LOG(TitansLog, Error, TEXT("AIGameMode:MergeCharacters_CharacterToReceiveSaved: Save operation failed."));
		OnCompleted.ExecuteIfBound(false);
		return;
	}

	if (IsValid(Character))
	{
		AIPlayerState* IPlayerState = PlayerController->GetPlayerState<AIPlayerState>();
		if (IPlayerState)
		{
			IPlayerState->CharactersData[CharacterToReceiveUID].Marks = Character->GetMarks();
		}

		Character->Destroy();
	}

	FDatabaseOperationCompleted OnCharacterToDeleteDeleted = FDatabaseOperationCompleted::CreateUObject(this, &AIGameMode::MergeCharacters_CharacterToDeleteDeleted, PlayerController, CharacterToDeleteUID, CharacterToReceiveUID, OnCompleted);
	DatabaseEngine->DeleteCharacter(CharacterToDeleteUID, OnCharacterToDeleteDeleted);
}

void AIGameMode::MergeCharacters_CharacterToDeleteDeleted(const FDatabaseOperationData& DeleteOperationData, AIPlayerController* PlayerController, FAlderonUID CharacterToDeleteUID, FAlderonUID CharacterToReceiveUID, FAsyncOperationCompleted OnCompleted)
{
	if (!DeleteOperationData.bSuccess)
	{
		UE_LOG(TitansLog, Error, TEXT("AIGameMode:MergeCharacters_CharacterToDeleteDeleted: Delete operation failed."));
		OnCompleted.ExecuteIfBound(false);
		return;
	}

	AIPlayerState* IPlayerState = PlayerController->GetPlayerState<AIPlayerState>();
	if (IPlayerState)
	{
		if (IPlayerState->Characters.Contains(CharacterToDeleteUID))
		{
			IPlayerState->Characters.Remove(CharacterToDeleteUID.ToString());
		}

		if (IPlayerState->CharactersData.Contains(CharacterToDeleteUID))
		{
			IPlayerState->CharactersData.Remove(CharacterToDeleteUID);
		}

		// Save Player State to update the record that we deleted those characters
		SavePlayerState(IPlayerState, ESavePriority::High);
	}

	OnCompleted.ExecuteIfBound(true);
}

//void AIGameMode::LoadCharacterAsync(AIPlayerController* PlayerController)
//{
	// Lamda Behavior for Ref vs Value
	// []        //no variables defined. Attempting to use any external variables in the lambda is an error.
	// [x, &y]   //x is captured by value, y is captured by reference
	// [&]       //any external variable is implicitly captured by reference if used
	// [=]       //any external variable is implicitly captured by value if used
	// [&, x]    //x is explicitly captured by value. Other variables will be captured by reference
	// [=, &z]   //z is explicitly captured by reference. Other variables will be captured by value

	// No Task Ref
	//AsyncTask(ENamedThreads::GameThread, [=]()
	//{
	//	LoadCharacter(IPlayerController);
	//});

	// Includes Task Ref for Management
	//FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady([=]()
	//{
	//	LoadCharacter(IPlayerController);
	//}, TStatId(), nullptr, ENamedThreads::GameThread);

	//AsyncTask(ENamedThreads::GameThread, [=]()
	//{
	//	LoadCharacter(PlayerController);
	//});
//}

void AIGameMode::HandleSpawnCharacter(AIPlayerController* PlayerController, const FCharacterSelectRow& CharacterData)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("AIGameMode::HandleSpawnCharacter"))

	AIPlayerState* IPlayerState = Cast<AIPlayerState>(PlayerController->PlayerState);
	TSubclassOf<AIBaseCharacter> CharacterClass = CharacterData.PreviewClass.Get();

	// Character Transform
	FVector SpawnVector = FVector(0, 0, 0);
	FRotator SpawnDirection = FRotator(0, 0, 0);
	FTransform SpawnTransform = FTransform(SpawnDirection, SpawnVector, FVector::OneVector);

	AIBaseCharacter* Character = Cast<AIBaseCharacter>(UGameplayStatics::BeginDeferredActorSpawnFromClass(
		this, CharacterClass, SpawnTransform, ESpawnActorCollisionHandlingMethod::AlwaysSpawn, PlayerController)
	);

	// Includes Task Ref for Management
	//FGraphEventRef DatabaseCharacterLoad = FFunctionGraphTask::CreateAndDispatchWhenReady([=]()
	//{
	//	// Load Character from Database
	//	DataHandler->Source(CharacterClass);
	//	DataHandler->Where(STRING_AccountUID, EDataHandlerOperator::Equals, IPlayerState->UniqueID()).First(Character);
	//}, TStatId(), nullptr, ENamedThreads::GameThread);

	//FGraphEventRef CharacterPossess = FFunctionGraphTask::CreateAndDispatchWhenReady([=]()
	//{
	//	// Finish Spawning Character
	//	UGameplayStatics::FinishSpawningActor(Character, SpawnTransform);
	//	// Possess Character
	//	PlayerController->Possess(Character);
	//	// Inform Client that Login is Complete
	//	PlayerController->ClientPostLogin(true);
	//}, TStatId(), DatabaseCharacterLoad, ENamedThreads::GameThread);
}

void AIGameMode::LoadCharacter(AIPlayerController* PlayerController)
{
	//UE_LOG(TitansLog, Log, TEXT("AIGameMode::LoadCharacter"));
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("AIGameMode::LoadCharacter"))

	AIPlayerState* IPlayerState = Cast<AIPlayerState>(PlayerController->PlayerState);
	if (!IPlayerState)
	{
		return;
	}

	TArray<FAlderonUID> LoadTimeRevengeKillFlags;
	PendingRevengeKillFlags.GenerateKeyArray(LoadTimeRevengeKillFlags);

	FDatabaseLoadCharactersCompleted OnCompleted = FDatabaseLoadCharactersCompleted::CreateUObject(this, &ThisClass::LoadCharacter_Stage1, TWeakObjectPtr<AIPlayerState>(IPlayerState), LoadTimeRevengeKillFlags);
	DatabaseEngine->LoadCharacters(IPlayerState->GetAlderonID(), OnCompleted);
}

void AIGameMode::LoadCharacter_Stage1(const FDatabaseLoadCharacters& Data, TWeakObjectPtr<AIPlayerState> IPlayerStateWeakPtr, TArray<FAlderonUID> LoadTimeRevengeKillFlags)
{
	//UE_LOG(TitansLog, Log, TEXT("AIGameMode::LoadCharacter_Stage1"));
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("AIGameMode::LoadCharacter_Stage1"))

	if (!IPlayerStateWeakPtr.IsValid())
	{
		return;
	}

	AIGameState* IGameState = GetGameState<AIGameState>();
	if (!IGameState) return;

	AIGameSession* IGameSession = Cast<AIGameSession>(GameSession);
	if (!IGameSession) return;

	// If we fail to load the character data we need to kick the player from the server
	if (!Data.bSuccess)
	{
		AIPlayerController* IPlayerController = Cast<AIPlayerController>(IPlayerStateWeakPtr->GetOwner());
		if (IPlayerController)
		{
			IGameSession->KickPlayer(IPlayerController, FText::FromString(TEXT("Failed to Load Character Data. Please try again later.")));
		}
		return;
	}

	TArray<FDatabaseLoadCharacter> LoadedCharacters = Data.LoadedCharacters;
	TArray<FCharacterData> LoadedCharacterData;
	LoadedCharacterData.Reserve(LoadedCharacters.Num());

	AIPlayerState* IPlayerState = IPlayerStateWeakPtr.Get();
	check(IPlayerState);

	// Load Characters if we have any
	FTransform TempTransform = FTransform(FRotator(0, 0, 0), FVector(0, 0, 0), FVector::OneVector);

	AIDinosaurCharacter* CharacterObject = Cast<AIDinosaurCharacter>(UGameplayStatics::BeginDeferredActorSpawnFromClass(
		this, AIDinosaurCharacter::StaticClass(), TempTransform, ESpawnActorCollisionHandlingMethod::AlwaysSpawn, nullptr)
	);

	check(CharacterObject);

	// Character can fail to create such as in cases where we are tearing down the world
	if (!CharacterObject)
	{
		UE_LOG(TitansLog, Error, TEXT("Failed to spawn character IGameMode:LoadCharacter_Stage1"));
		return;
	}

	CharacterObject->_SetReplicatesPrivate(false);
	CharacterObject->SetActorHiddenInGame(true);
	CharacterObject->SetActorTickEnabled(false);
	CharacterObject->SetActorEnableCollision(false);
	CharacterObject->DestroyCosmeticComponents();
	CharacterObject->SetLifeSpan(300.0f); // Self destroy after 5 minutes incase to prevent leaks

	IPlayerState->PendingLoadCharacter = CharacterObject;

	check(DatabaseEngine);

	// Clear character ids if we are using a remote database that does lookups
	if (DatabaseEngine->GetDatabaseType() == EDatabaseType::Remote)
	{
		IPlayerState->Characters.Empty();
	}

	// Clear character data as we are now loading it
	IPlayerState->CharactersData.Empty();

	UIGameInstance* IGameInstance = UIGameplayStatics::GetIGameInstance(this);
	check(IGameInstance);
	AIWorldSettings* IWorldSettings = Cast<AIWorldSettings>(GetWorld()->GetWorldSettings());
	check(IWorldSettings);
	UILevelSummary* LevelSummary = IWorldSettings->GetLevelSummary();
	check(LevelSummary);

	bool bRestrictedCharacters = (LevelSummary->GetAllowedCharacters(this).Num() > 0);

	// Pre-allocate arrays to avoid many resizing
	int32 EstimatedCharacterCount = LoadedCharacters.Num();
	IPlayerState->Characters.Reserve(EstimatedCharacterCount);
	IPlayerState->CharactersData.Reserve(EstimatedCharacterCount);

	// Sort out a list of characters we are going to have to load data for
	for (int Index = LoadedCharacters.Num() - 1; Index >= 0; Index--)
	{
		const FDatabaseLoadCharacter& LoadChar = LoadedCharacters[Index];

		if (!LoadChar.bSuccess || !LoadChar.CharacterId.IsValid())
		{
			LoadedCharacters.RemoveAt(Index);
			continue;
		}

		FAlderonUID CharacterUID = LoadChar.CharacterId;
		if (!CharacterUID.IsValid())
		{
			UE_LOG(TitansLog, Error, TEXT("IGameMode:LoadCharacter: Error CharacterUID %s is invalid."), *CharacterUID.ToString());
			LoadedCharacters.RemoveAt(Index);
			continue;
		}

		TSharedPtr<FJsonObject> CharacterJson = LoadChar.CharacterDataJson;
		if (!CharacterJson.IsValid())
		{
			UE_LOG(TitansLog, Error, TEXT("IGameMode:LoadCharacter: Error Json for CharacterUID %s is invalid."), *CharacterUID.ToString());
			LoadedCharacters.RemoveAt(Index);
			continue;
		}

		bool bValid = IAlderonDatabase::DeserializeObject(CharacterObject, CharacterJson);
		if (!bValid)
		{
			UE_LOG(TitansLog, Error, TEXT("IGameMode:LoadCharacter: Failed to Deserialize Json for CharacterUID %s"), *CharacterUID.ToString());
			LoadedCharacters.RemoveAt(Index);
			continue;
		}

		
		if (CharacterObject->GetCharacterName().IsEmpty())
		{
			UE_LOG(TitansLog, Error, TEXT("IGameMode:LoadCharacter: CharacterUID: %s has no Character Name"), *CharacterUID.ToString());
			LoadedCharacters.RemoveAt(Index);
			continue;
		}

		// Give character current subspecies if they have none unlocked.
		// This will actually be saved to the character when unlocking a new subspecies.
		if (CharacterObject->GetUnlockedSubspeciesIndexes().Num() == 0)
		{
			CharacterObject->GetUnlockedSubspeciesIndexes_Mutable().Add(CharacterObject->GetSkinData().MeshIndex);
		}

		CharacterObject->MigrateLegacyCharacterData();

		FCharacterData CharacterData;
		CharacterData.Name = CharacterObject->GetCharacterName();
		CharacterData.CharacterID = CharacterUID;
		CharacterData.SelectedSkin = CharacterObject->GetSkinData();		
		CharacterData.PreviousSkin = CharacterObject->GetPreviousSkinData();
		
		// Check Loaded Mods is compatible
		if (!CharacterObject->ModSkus.IsEmpty())
		{
			bool bCharacterIncompatible = false;
			FString IncompatibleMod = TEXT("");

			IAlderonUGC& UGCInterface = IAlderonCommon::Get().GetUGCInterface();

			const TMap<FString, TStrongObjectPtr<UAlderonUGCDetails>> ServerMods = UGCInterface.GetLoadedMods();

			for (const FString& CharModSku : CharacterObject->ModSkus)
			{
				if (!ServerMods.Contains(CharModSku))
				{
					bCharacterIncompatible = true;
					IncompatibleMod = CharModSku;
					break;
				}
			}

			// If there is an incompatible mod, try to fix our skin data.
			// If the character asset ID is invalid, it will be skipped in "LoadCharacter_Stage2"
			if (bCharacterIncompatible)
			{
				if (UIGameInstance::FixSkinData(CharacterData.SelectedSkin))
				{
					UE_LOG(TitansLog, Warning, TEXT("IGameMode:LoadCharacter: CharacterData.SelectedSkin was invalid, resetting to default skin (maybe a mod was disabled). Incompatible Mod: %s"), *IncompatibleMod);
				}
				if (UIGameInstance::FixSkinData(CharacterData.PreviousSkin))
				{
					UE_LOG(TitansLog, Warning, TEXT("IGameMode:LoadCharacter: CharacterData.PreviousSkin was invalid, resetting to default skin (maybe a mod was disabled). Incompatible Mod: %s"), *IncompatibleMod);
				}
			}
		}

		CharacterData.Marks = CharacterObject->GetMarks();
		CharacterData.ModSkus = CharacterObject->ModSkus;
		CharacterData.BabySkinTint = CharacterObject->GetBabySkinTint();
		CharacterData.DeathInfo = CharacterObject->DeathInfo;
		CharacterData.LastPlayedDate = CharacterObject->LastPlayedDate;
		CharacterData.LastKnownPosition = CharacterObject->SaveCharacterPosition;
		CharacterData.bHasFinishedHatchlingTutorial = CharacterObject->HasLeftHatchlingCave();
		CharacterData.UnlockedSubspeciesIndexes = CharacterObject->GetUnlockedSubspeciesIndexes();

		if (UIGameplayStatics::IsGrowthEnabled(this))
		{
			if (CharacterObject->ShouldUseActualGrowth())
			{
				CharacterData.Growth = CharacterObject->GetActualGrowth();
			}
			else
			{
				IAlderonDatabase::DeserializeGameplayAttribute(CharacterJson, IAlderonDatabase::GetAttributeSerializedName(TEXT("Growth")), CharacterData.Growth);
			}

			// If we died previously and we have a growth penalty, it needs to be set now so the character preview is accurate
			// It will actually apply properly on the server on respawn.
			if (CharacterObject->CalculatedGrowthForRespawn != 0.0f)
			{
				CharacterObject->SetGrowthPercent(CharacterObject->CalculatedGrowthForRespawn);
				CharacterData.Growth = CharacterObject->CalculatedGrowthForRespawn;
			}
		}

		CharacterData.SheddingProgress = CharacterObject->GetSheddingProgressRaw();

		bool bPendingSave = CharacterSavesInProgress.Contains(CharacterData.CharacterID);

		AIBaseCharacter* CombatLogCharacter = GetCombatLogAI(CharacterData.CharacterID);
		if (CombatLogCharacter)
		{
			CharacterData.bHasActiveCombatLogAI = true;

			float TimeRemaining = CombatLogCharacter->GetLifeSpan();
			CharacterData.CombatLogCooldownEnd = IGameState->ElapsedTime + CombatLogCharacter->GetCombatLogDuration();
		}
		else
		{
			bool bCombatLog = (LoadChar.Stamps.Contains(TEXT("CombatLog")) || CharacterSavesInProgress.Contains(CharacterData.CharacterID));
			if (bCombatLog)
			{
				CharacterData.CombatLogCooldownEnd = IGameState->ElapsedTime + CombatLogCharacter->GetCombatLogDuration();
			}
			else 
			{
				CharacterData.CombatLogCooldownEnd = 0;
			}
		}

		bool bRevengeKill = (LoadChar.Stamps.Contains(TEXT("RevengeKill")) || LoadTimeRevengeKillFlags.Contains(CharacterData.CharacterID) || PendingRevengeKillFlags.Contains(CharacterData.CharacterID));
		if (bRevengeKill)
		{
			CharacterData.RevengeKillCooldownEnd = IGameState->ElapsedTime + CombatLogCharacter->GetRevengeKillDuration();
		}
		else 
		{
			CharacterData.RevengeKillCooldownEnd = 0;
		}

		if (!CharacterData.SelectedSkin.SkinAssetId.IsValid())
		{
			// UE-Log Invalid Character State
			LoadedCharacters.RemoveAt(Index);
			UE_LOG(TitansLog, Error, TEXT("IGameMode:LoadCharacter: CharacterUID: %s has a invalid SkinAssetId %s"), *CharacterUID.ToString(), *CharacterData.SelectedSkin.SkinAssetId.ToString());
			continue;
		}

		// Skip if this character is not allowed in this map
		if (bRestrictedCharacters)
		{
			if (!LevelSummary->GetAllowedCharacters(this).Contains(CharacterObject->CharacterDataAssetId))
			{
				LoadedCharacters.RemoveAt(Index);
				continue;
			}
		}

		FPrimaryAssetId CharacterDataAssetId = CharacterData.SelectedSkin.CharacterAssetId;
		if (!CharacterDataAssetId.IsValid())
		{
			CharacterDataAssetId = CharacterObject->CharacterDataAssetId;
		}

		if (!CharacterDataAssetId.IsValid())
		{
			UE_LOG(TitansLog, Error, TEXT("IGameMode:LoadCharacter: CharacterUID: %s has a invalid CharacterDataAssetId %s"), *CharacterUID.ToString(), *CharacterDataAssetId.ToString());
			LoadedCharacters.RemoveAt(Index);
			continue;
		}

		LoadedCharacterData.Add(CharacterData);
	}

	// Need to reverse this as the entries were put in backwards.
	Algo::Reverse(LoadedCharacterData);

	IPlayerState->PendingCharacterLoads.Reserve(LoadedCharacters.Num());
	for (const FDatabaseLoadCharacter& LoadChar : LoadedCharacters)
	{
		IPlayerState->PendingCharacterLoads.Add(LoadChar.CharacterId);
	}

	if (IPlayerState->PendingCharacterLoads.Num() == 0)
	{
		// No Characters to load so tell the client we are done, else this will be called in a async post load callback
		SubmitCharacterData(IPlayerState);
	}
	else
	{
		// Load Character Data left over after sorting
		for (int Index = 0; Index < LoadedCharacters.Num(); Index++)
		{
			const FDatabaseLoadCharacter& LoadChar = LoadedCharacters[Index];
			FCharacterData& CharacterData = LoadedCharacterData[Index];
			FPrimaryAssetId CharacterDataAssetId = CharacterData.SelectedSkin.CharacterAssetId;
			FCharacterDataAssetLoaded PostCharacterDataLoad = FCharacterDataAssetLoaded::CreateUObject(this, &ThisClass::LoadCharacter_Stage2, TWeakObjectPtr<AIPlayerState>(IPlayerState), LoadChar.CharacterId, CharacterData);
			UIGameInstance::AsyncLoadCharacterData(CharacterDataAssetId, PostCharacterDataLoad);
		}
	}
}

void AIGameMode::LoadCharacter_Stage2(bool bSuccess, FPrimaryAssetId LoadedAssetId, UCharacterDataAsset* LoadedAsset, TWeakObjectPtr<AIPlayerState> IPlayerStateWeakPtr, FAlderonUID CharacterUID, FCharacterData CharacterData)
{
	//UE_LOG(TitansLog, Log, TEXT("AIGameMode::LoadCharacter_Stage2"));
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("AIGameMode::LoadCharacter_Stage2"))

	// Player State got destroyed while we were loading?
	if (!IPlayerStateWeakPtr.IsValid())
	{
		UE_LOG(TitansLog, Error, TEXT("IGameMode:LoadCharacter:PostCharacterDataLoad: IPlayerState is nullptr for CharacterUID: %s"), *CharacterUID.ToString());
		return;
	}

	AIPlayerState* IPlayerState = IPlayerStateWeakPtr.Get();
	check(IPlayerState);

	// Ensure Submit Character Data gets called when we run out of characters to load
	// This avoids duplicating code and can handle invalid mod character loads getting skipped without getting stuck waiting forever
	ON_SCOPE_EXIT
	{
		if (IPlayerState->PendingCharacterLoads.Num() == 0)
		{
			SubmitCharacterData(IPlayerState);
		}
	};

	if (!IPlayerState->PendingCharacterLoads.Contains(CharacterUID))
	{
		UE_LOG(TitansLog, Error, TEXT("IGameMode:LoadCharacter:PostCharacterDataLoad: PendingCharacterLoads does not contain %s"), *CharacterUID.ToString());
		return;
	}

	IPlayerState->PendingCharacterLoads.RemoveSingle(CharacterUID);

	if (!bSuccess)
	{
		UE_LOG(TitansLog, Error, TEXT("IGameMode:LoadCharacter:PostCharacterDataLoad: Failed to Async Load Character Data Asset %s"), *LoadedAssetId.ToString());
		return;
	}

	if (!LoadedAsset)
	{
		UE_LOG(TitansLog, Error, TEXT("IGameMode:LoadCharacter:PostCharacterDataLoad: Character Data Asset %s is nullptr"), *LoadedAssetId.ToString());
		return;
	}

	OnCharacterDataLoaded(IPlayerState, LoadedAsset, CharacterUID, CharacterData);
}

void AIGameMode::SubmitCharacterData(AIPlayerState* IPlayerState)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("AIGameMode::SubmitCharacterData"))

	check(IPlayerState);
	if (!IPlayerState) return;

	if (IPlayerState->PendingLoadCharacter)
	{
		IPlayerState->PendingLoadCharacter->Destroy();
		IPlayerState->PendingLoadCharacter = nullptr;
	}

	AIPlayerController* IPlayerController = Cast<AIPlayerController>(IPlayerState->GetOwner());
	check(IPlayerController);
	if (!IPlayerController) return;

	IPlayerState->PendingCharacterLoads.Empty();


	// Tell the client we are loaded. They will need to pick a character from the character selection screen
	TArray<FCharacterData> Characters;
	IPlayerState->CharactersData.GenerateValueArray(Characters);
	IPlayerController->ClientPostLogin(Characters);

	// Shrink and save memory
	IPlayerState->Characters.Shrink();
	IPlayerState->CharactersData.Shrink();
}

void AIGameMode::OnCharacterDataLoaded(AIPlayerState* IPlayerState, UCharacterDataAsset* CharacterDataAsset, FAlderonUID CharacterUID, FCharacterData CharacterData)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("AIGameMode::OnCharacterDataLoaded"))

	check(CharacterDataAsset);
	
	// Skip Characters with invalid character data assets
	if (!CharacterDataAsset) return;

	// Skip Characters not released
	if (!CharacterDataAsset->IsReleased()) return;

	// Fetch Asset Id
	FPrimaryAssetId AssetId = CharacterData.SelectedSkin.CharacterAssetId;

	// Fetch World Settings / Level Summary
	AIWorldSettings* IWorldSettings = Cast<AIWorldSettings>(GetWorld()->GetWorldSettings());
	check(IWorldSettings);

	UILevelSummary* LevelSummary = IWorldSettings->GetLevelSummary();
	check(LevelSummary);

	// Check Map Restricted Characters
	bool bRestrictedCharacters = (LevelSummary->GetAllowedCharacters(GetWorld()).Num() > 0);
	const TArray<FPrimaryAssetId>& AllowedCharacters = LevelSummary->GetAllowedCharacters(this);
	const TArray<FPrimaryAssetId>& DisallowedCharacters = LevelSummary->GetDisallowedCharacters(this);

	bool bCharacterMapWhitelisted = AllowedCharacters.Contains(AssetId);
	bool bCharacterMapBlacklisted = DisallowedCharacters.Contains(AssetId);

	// If we restrict characters, skip characters not white listed
	if (bRestrictedCharacters && !bCharacterMapWhitelisted) return;

	// If we restrict characters, skip characters not white listed
	if (bCharacterMapBlacklisted) return;

	// Skip Characters unless they are map white listed if specified in the data asset
	if (CharacterDataAsset->bHideUnlessMapWhitelisted && !bCharacterMapWhitelisted) return;

	// Skip Aquatics if they are not allowed
	if (!IWorldSettings->bAllowAquaticCharacters && CharacterDataAsset->bAquatic) return;

	// Skip Flyers if they are not allowed
	if (!IWorldSettings->bAllowFlyerCharacters && CharacterDataAsset->bFlyer) return;

	if (!ensure(IPlayerState != nullptr))
	{
		UE_LOG(TitansLog, Warning, TEXT("AIGameMode::OnCharacterDataLoaded: The player state was not valid; cannot add CharacterData to the Characters"));
		return;
	}

	IPlayerState->CharactersData.Add(CharacterUID, CharacterData);

	if (!IPlayerState->Characters.Contains(CharacterUID.ToString()))
	{
		IPlayerState->Characters.Add(CharacterUID.ToString());
	}
}

void AIGameMode::SavePlayerState(AIPlayerState* TargetPlayerState, const ESavePriority Priority)
{
	SavePlayerStateAsync(TargetPlayerState, FAsyncOperationCompleted(), Priority);
}

void AIGameMode::SavePlayerStateAsync(AIPlayerState* TargetPlayerState, FAsyncOperationCompleted OnCompleted, const ESavePriority Priority)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("AIGameMode::SavePlayerState"))

	//START_PERF_TIME()

	if (IsValid(TargetPlayerState) && TargetPlayerState->GetAlderonID().IsValid())
	{
		check(DatabaseEngine);

		if (!DatabaseEngine)
		{
			return;
		}

		FDatabaseOperationCompleted OnCompleted2 = FDatabaseOperationCompleted::CreateLambda([=](const FDatabaseOperationData& Data)
		{
			OnCompleted.ExecuteIfBound(true);
		});

		DatabaseEngine->SavePlayerState(TargetPlayerState, TargetPlayerState->GetAlderonID(), OnCompleted2, Priority);
	}
	else
	{
		OnCompleted.ExecuteIfBound(false);
	}

	//END_PERF_TIME()
	//WARN_PERF_TIME(1);
}

void AIGameMode::SaveCombatLogAI(AIBaseCharacter* TargetCombatLogAI, bool bDestroyWhenDone)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("AIGameMode::SaveCombatLogAI"))

	// If you need to handle special edge cases when saving combat log ai, you can do so here
	FAsyncOperationCompleted OnCombatLogSaved = FAsyncOperationCompleted::CreateLambda([=](bool bSuccess)
	{
		if (bDestroyWhenDone)
		{
			if (IsValid(TargetCombatLogAI))
			{
				TargetCombatLogAI->Destroy();
			}
		}
	});

	SaveCharacterAsync(TargetCombatLogAI, OnCombatLogSaved, ESavePriority::Low);
}

void AIGameMode::PrepareCharacterForSave(AIBaseCharacter* TargetCharacter)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("AIGameMode::PrepareCharacterForSave"))

	if (!IsValid(TargetCharacter))
	{
		return;
	}

	if (AIDinosaurCharacter* IDinoChar = Cast<AIDinosaurCharacter>(TargetCharacter))
	{
		IDinoChar->FillModSkus();
	}

	// Copy Save Position Data Over Before Saving.
	if (TargetCharacter->IsAlive())
	{
		// If the player is in an instance then don't change this,
		// just keep values from before entering instance
		if (!TargetCharacter->GetCurrentInstance())
		{
			TargetCharacter->SaveCharacterPosition = TargetCharacter->GetActorLocation();
			TargetCharacter->SaveCharacterRotation = TargetCharacter->GetActorRotation();
		}
	}
	else
	{
		TargetCharacter->SaveCharacterPosition = FVector::ZeroVector;
		TargetCharacter->SaveCharacterRotation = FRotator::ZeroRotator;
	}

	if (!TargetCharacter->bIsCharacterEditorPreviewCharacter)
	{
		float WorldTimeSeconds = GetWorld()->GetTimeSeconds();
		TargetCharacter->DeathInfo.Lifetime += (WorldTimeSeconds - TargetCharacter->LastSaveTime);
		TargetCharacter->LastSaveTime = WorldTimeSeconds;
	}

	// Save the Group Meetup Quest Cooldown
	if (AIQuestManager* QuestMgr = AIWorldSettings::GetWorldSettings(this)->QuestManager)
	{
		for (const FQuestCooldown& QuestCD : QuestMgr->GroupMeetQuestCooldowns)
		{
			if (QuestCD.CharacterID == TargetCharacter->GetCharacterID())
			{
				float TimeDifference = (GetWorld()->TimeSeconds - QuestCD.Timestamp);
				TargetCharacter->GroupMeetupTimeSpent = TimeDifference;
				break;
			}
		}
	}
	
	// Save current cooldown effects to restore them after load
	TargetCharacter->PrepareCooldownsForSave();

	// Save currently active gameplay effects to restore them after load
	TargetCharacter->PrepareGameplayEffectsForSave();

	// Handle Quest Saving
	AIQuestManager::SaveQuest(TargetCharacter);
}

void AIGameMode::SaveCharacter(AIBaseCharacter* TargetCharacter, const ESavePriority Priority)
{
	SaveCharacterAsync(TargetCharacter, FAsyncOperationCompleted(), Priority);
}

void AIGameMode::SaveCharacterAsync(AIBaseCharacter* TargetCharacter, FAsyncOperationCompleted OnCompleted, const ESavePriority Priority)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("AIGameMode::SaveCharacterAsync"))

	//START_PERF_TIME()

	if (IsValid(TargetCharacter))
	{
		TargetCharacter->LastPlayedDate = FDateTime::UtcNow();

		PrepareCharacterForSave(TargetCharacter);

		// Determine AlderonId to save Character Under
		FAlderonPlayerID SaveAlderonId;

		AIPlayerState* IPlayerState = TargetCharacter->GetPlayerState<AIPlayerState>();
		if (IPlayerState && IPlayerState->GetAlderonID().IsValid())
		{
			SaveAlderonId = IPlayerState->GetAlderonID();
		} else {
			SaveAlderonId = TargetCharacter->GetCombatLogAlderonId();
		}

		// Flag used if the character has died 
		bool bCharacterDeath = TargetCharacter->bRespawn;
		FAlderonUID CharacterID = TargetCharacter->GetCharacterID();

		// Request to Save Character
		check(DatabaseEngine);
		if (!DatabaseEngine)
		{
			UE_LOG(TitansLog, Error, TEXT("AIGameMode::SaveCharacterAsync: DatabaseEngine nullptr"));
			return;
		}
		FDatabaseOperationCompleted OnCompletedDatabaseLayer = FDatabaseOperationCompleted();

		OnCompletedDatabaseLayer = FDatabaseOperationCompleted::CreateLambda([this, CharacterID, OnCompleted](const FDatabaseOperationData& Data)
		{
			FWaitForDatabaseWrite* DelPtr = CharacterSavesInProgress.Find(CharacterID);
			if (DelPtr)
			{
				DelPtr->Broadcast();
				CharacterSavesInProgress.Remove(CharacterID);
			}

			CharacterSavesInProgress.Remove(CharacterID);

			OnCompleted.ExecuteIfBound(true);
		});

		CharacterSavesInProgress.Add(TargetCharacter->GetCharacterID(), FWaitForDatabaseWrite());

		DatabaseEngine->SaveCharacter(TargetCharacter, SaveAlderonId, TargetCharacter->GetCharacterID(), OnCompletedDatabaseLayer, Priority);
	}

	//END_PERF_TIME()
	//WARN_PERF_TIME(1);
}

void AIGameMode::SaveAll(AIPlayerController* PlayerController, const ESavePriority Priority)
{
	SaveAllAsync(PlayerController, FAsyncOperationCompleted(), Priority);
}

void AIGameMode::SaveAllAsync(AIPlayerController* PlayerController, FAsyncOperationCompleted OnCompleted, const ESavePriority Priority)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("AIGameMode::SaveAllAsync"))

	//START_PERF_TIME()

	if (!IsValid(PlayerController))
	{
		OnCompleted.ExecuteIfBound(false);
		return;
	}

	AIPlayerState* IPlayerState = Cast<AIPlayerState>(PlayerController->PlayerState);
	if (!IsValid(IPlayerState))
	{
		OnCompleted.ExecuteIfBound(false);
		return;
	}

	AIBaseCharacter* IBaseCharacter = Cast<AIBaseCharacter>(PlayerController->GetPawn());
	if (!IsValid(IBaseCharacter))
	{
		OnCompleted.ExecuteIfBound(false);
		return;
	}

	TStrongObjectPtr<AIPlayerState> IPlayerStateStrong = TStrongObjectPtr<AIPlayerState>(IPlayerState);
	TStrongObjectPtr<AIGameMode> StrongThis = TStrongObjectPtr< AIGameMode>(this);

	if (OnCompleted.IsBound())
	{
		FAsyncOperationCompleted OnCompletedCharacter = FAsyncOperationCompleted::CreateLambda([OnCompleted, IPlayerStateStrong, Priority, StrongThis](bool bSuccess)
		{
			if (!StrongThis.IsValid())
			{
				OnCompleted.ExecuteIfBound(false);
				return;
			}

			FAsyncOperationCompleted OnCompletedPlayerState = FAsyncOperationCompleted::CreateLambda([OnCompleted](bool bSuccess2)
			{
				OnCompleted.ExecuteIfBound(true);
			});

			if (IPlayerStateStrong.IsValid())
			{
				StrongThis->SavePlayerStateAsync(IPlayerStateStrong.Get(), OnCompletedPlayerState, Priority);
			}
			else
			{
				OnCompleted.ExecuteIfBound(false);
			}
		});

		//No need to save the Priority passed in as after saving the character the On Complete gets called and will save with the proper priority
		//Mainly concerned about writing High priority specifically on Switch
		SaveCharacterAsync(IBaseCharacter, OnCompletedCharacter, ESavePriority::Low);
	}
	else
	{
		// Complete save together
		SaveCharacter(IBaseCharacter, ESavePriority::Low);
		SavePlayerState(IPlayerState, Priority);
	}

	//END_PERF_TIME()
	//WARN_PERF_TIME(1);
}

void AIGameMode::SaveNest(AINest* Nest)
{
}

void AIGameMode::DeleteNest(AINest* Nest)
{
}

void AIGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
	ShutdownDatabase();
}

void AIGameMode::BeginDestroy()
{
	Super::BeginDestroy();
}

void AIGameMode::StartPlay()
{
	Super::StartPlay();

#if WITH_BATTLEYE_SERVER
	if (IsRunningDedicatedServer())
	{
		if (IBattlEyeServer::IsAvailable())
		{
			IBattlEyeServer::Get().AddOnCommandExecutedUFunction(this, FName(TEXT("ProcessBECommand")));
		}
	}
#endif
}

void AIGameMode::ProcessBECommand(const FString& String)
{

}

void AIGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("AIGameMode::PreLogin"))

	// Check if we want to skip login approval (dev or admin logging in)
	//bool bSkipLoginApproval = false;
	AIGameSession* IGameSession = Cast<AIGameSession>(GameSession);
	//if (IGameSession)
	//{
	//	FString UniqueIDStr = UniqueId.ToString();
	//	if (IGameSession->IsDevID(UniqueIDStr) || IGameSession->IsAdminID(UniqueIDStr))
	//	{
	//		bSkipLoginApproval = true;
	//	}
	//}

	// Approve the Player Login
	//if (!bSkipLoginApproval)
	//{
	//	ErrorMessage = GameSession->ApproveLogin(Options);
	//}

	ErrorMessage = GameSession->ApproveLogin(Options);

	// Allow our game session to validate that a player can play
	if (ErrorMessage.IsEmpty() && IGameSession)
	{
		bool bJoinAsSpectator = FCString::Stricmp(*UGameplayStatics::ParseOption(Options, TEXT("SpectatorOnly")), TEXT("1")) == 0;
		IGameSession->ValidatePlayer(Address, UniqueId.GetUniqueNetId(), ErrorMessage, bJoinAsSpectator);
	}
}

void AIGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	FSlowHeartBeatScope SuspendHeartBeat;

	bLockingLevelLoadCompleted = false;

	if (UWorld* World = GetWorld())
	{
		// Servers using world composition have to wait for level streaming to complete here, otherwise they will start talking to steam and might be unable to respond while blocking for loading the map
		if (World->WorldComposition)
		{
            if (WITH_EDITOR || (GetNetMode() != NM_Client && GetNetMode() != NM_Standalone))
            {
                GEngine->BlockTillLevelStreamingCompleted(World);
            }
        }

		// Handle Char Select Point
		AIWorldSettings* IWorldSettings = Cast<AIWorldSettings>(GetWorldSettings());
		CharSelectPoint = IWorldSettings->CharSelectPoint;

		// Process all Spawn Points, Cache and Delete Them
		{
			TArray<AActor*> FoundStartPoints;
			UGameplayStatics::GetAllActorsOfClass(World, APlayerStart::StaticClass(), FoundStartPoints);

			GenericSpawnPoints.Reserve(FoundStartPoints.Num());

			for (AActor* Actor : FoundStartPoints)
			{
#if WITH_EDITOR
				if (APlayerStartPIE* PSPIE = Cast<APlayerStartPIE>(Actor))
				{
					continue;
				}
#endif
				if (!IWorldSettings->IsInWorldBounds(Actor->GetActorLocation()))
				{
					UE_LOG(LogTemp, Error, TEXT("AIGameMode::InitGame: Spawn point out of bounds."));
					continue;
				}

				if (APlayerStart* PS = Cast<APlayerStart>(Actor))
				{
					AIPlayerStart* IPlayerStart = Cast<AIPlayerStart>(PS);

					if (PS->PlayerStartTag != NAME_None || (IPlayerStart && IPlayerStart->AnyPlayerStartTags.Num() > 0))
					{
						CustomSpawnPoints.Add(PS);

						if (IsValid(IPlayerStart))
						{
							if (IPlayerStart->AnyPlayerStartTags.Contains(NAME_Land))
							{
								GenericSpawnPoints.Add(Actor->GetActorLocation());
							}
						}
	
						//continue;
					}

					if (IsValid(PS) && PS->PlayerStartTag == NAME_Land)
					{
						GenericSpawnPoints.Add(Actor->GetActorLocation());
						//Actor->Destroy();
					}
				}


			}

			GenericSpawnPoints.Shrink();
		}
	}

	bLockingLevelLoadCompleted = true;

	if (AIGameSession* IGameSession = Cast<AIGameSession>(GameSession))
	{
		IGameSession->CheckServerRegisterReady();
	}

	bWasHandlingDedicatedServerReplays = bHandleDedicatedServerReplays;

	Super::InitGame(MapName, Options, ErrorMessage);
}

void AIGameMode::InitGameState()
{
	Super::InitGameState();
}

void AIGameMode::PreInitializeComponents()
{
	Super::PreInitializeComponents();

	// Set timer to run every second
	GetWorldTimerManager().SetTimer(TimerHandle_DefaultTimer, this, &AIGameMode::DefaultTimer, GetWorldSettings()->GetEffectiveTimeDilation(), true);
	if (!bMapLoaded)
	{
		GetWorldTimerManager().SetTimer(TimerHandle_MapReloadTimer, this, &AIGameMode::MapReloadTimer, GetWorldSettings()->GetEffectiveTimeDilation(), true);
	}
}

void AIGameMode::StartMatch()
{
	GetWorldTimerManager().ClearTimer(TimerHandle_DefaultTimer);

#if UE_SERVER
	if (IsRunningDedicatedServer())
	{
		GetWorldTimerManager().SetTimer(TimerHandle_StatsUpdate, this, &AIGameMode::StatsUpdate, GetWorldSettings()->GetEffectiveTimeDilation(), true);
		GetWorldTimerManager().SetTimer(TimerHandle_StatsPrint, this, &AIGameMode::StatsPrint, 15.0f, true);
	}
#endif

	Super::StartMatch();

	if (IsRunningDedicatedServer() || WITH_EDITOR)
	{
		// save all default creator mode objects for restoring when needed
		TArray<FDatabaseBunchEntry> Entries;
		GetDirtyCreatorModeObjects(Entries, true);

		for (FDatabaseBunchEntry& Entry : Entries)
		{
			if (Entry.Tag.Len() == 0) continue;
			if (!Entry.JsonObject.Get()) continue;
			DefaultCreatorModeActors.Add(Entry.Tag, Entry.JsonObject);
		}

		if (!DefaultCreatorModeSave.IsEmpty())
		{
			UE_LOG(TitansLog, Log, TEXT("AIGameMode::StartMatch: Attempting to load default creator mode save %s"), *DefaultCreatorModeSave);
			LoadCreatorModeObjects(DefaultCreatorModeSave);
		}
	}
}

void AIGameMode::HandleMatchHasStarted()
{
	// Copy of game engine base but different handling of server replay
	// So it generates an automatic replay name (saves doing an engine mod, and keeps hotfix compat)
	
	GameSession->HandleMatchHasStarted();

	// start human players first
	for (FConstPlayerControllerIterator Iterator = GetWorld()->GetPlayerControllerIterator(); Iterator; ++Iterator)
	{
		APlayerController* PlayerController = Iterator->Get();
		if (PlayerController && (PlayerController->GetPawn() == nullptr) && PlayerCanRestart(PlayerController))
		{
			RestartPlayer(PlayerController);
		}
	}

	// Make sure level streaming is up to date before triggering NotifyMatchStarted
	GEngine->BlockTillLevelStreamingCompleted(GetWorld());

	// First fire BeginPlay, if we haven't already in waiting to start match
	GetWorldSettings()->NotifyBeginPlay();

	// Then fire off match started
	GetWorldSettings()->NotifyMatchStarted();

	// if passed in bug info, send player to right location
	const FString BugLocString = UGameplayStatics::ParseOption(OptionsString, TEXT("BugLoc"));
	const FString BugRotString = UGameplayStatics::ParseOption(OptionsString, TEXT("BugRot"));
	if (!BugLocString.IsEmpty() || !BugRotString.IsEmpty())
	{
		for (FConstPlayerControllerIterator Iterator = GetWorld()->GetPlayerControllerIterator(); Iterator; ++Iterator)
		{
			APlayerController* PlayerController = Iterator->Get();
			if (PlayerController && PlayerController->CheatManager != nullptr)
			{
				PlayerController->CheatManager->BugItGoString(BugLocString, BugRotString);
			}
		}
	}

	if (IsHandlingReplays() && GetGameInstance() != nullptr)
	{
		GetGameInstance()->StartRecordingReplay(FString(), GetWorld()->GetMapName());
	}
}

void AIGameMode::StatsUpdate()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("AIGameMode::StatsUpdate"))

	AIGameState* IGameState = UIGameplayStatics::GetIGameState(this);
	if (!IGameState) return;
	UEngine* Engine = Cast<UEngine>(GEngine);
	if (!Engine) return;

	float DeltaTime = FApp::GetDeltaTime();
	float NewServerTickRate = FMath::RoundToInt(1.0f / DeltaTime);
	int NumPerformanaceSamples = PerformanceSamples.Num();

	if (NumPerformanaceSamples >= 60)
	{
		PerformanceSamples.RemoveAt(0);
	}
	PerformanceSamples.Add(NewServerTickRate);

	FServerTickInformation NewTickInformation;
	NewTickInformation.CurrentTickRate = NewServerTickRate;
	NewTickInformation.MaxTickRate = Engine->GetMaxTickRate(DeltaTime);

	// Calculate Average Performance
	{
		int Sum = 0;
		int BestPerformance = 0;
		int WorstPerformance = 0;
		float Average = 0;

		for (int32 Index = 0; Index < PerformanceSamples.Num(); Index++)
		{
			int32 Value = PerformanceSamples[Index];

			if (Value > BestPerformance)
			{
				BestPerformance = Value;
			}

			if (Value < WorstPerformance)
			{
				WorstPerformance = Value;
			}

			Sum += Value;
			
		}

		Average = (float)Sum / ((float)PerformanceSamples.Num());

		NewTickInformation.MinAverageTickRate = WorstPerformance;
		NewTickInformation.AverageTickRate = Average;
		NewTickInformation.MaxAverageTickRate = BestPerformance;
	}

	IGameState->SetServerTickInfo(NewTickInformation);

	IAlderonCommon& AlderonModule = IAlderonCommon::Get();
	AlderonModule.UpdatePerformanceMetrics(NewTickInformation.CurrentTickRate, NewTickInformation.MaxTickRate, NewTickInformation.AverageTickRate);

	// Memory Usage
	IGameState->SetServerMemoryInfo(FServerMemoryInformation::Get());
}

void AIGameMode::StatsPrint()
{
#if UE_SERVER
	AIGameState* IGameState = UIGameplayStatics::GetIGameState(this);
	if (!IGameState) return;

	UE_LOG(TitansNetwork, Log, TEXT("ServerHealth: CurrentTickRate: %i AverageTickRate: %i MaxTickRate: %i Players: %i"),
		IGameState->GetServerTickInfo().CurrentTickRate,
		IGameState->GetServerTickInfo().AverageTickRate,
		IGameState->GetServerTickInfo().MaxTickRate,
		IGameState->PlayerArray.Num());
#endif
}

void AIGameMode::DefaultTimer()
{
	TickOTPCache();

	// Start the Match If The Session Isn't Busy Creating a Session
	if (GetMatchState() == MatchState::WaitingToStart)
	{
		if (AIGameSession* session = Cast<AIGameSession>(GameSession))
		{
#if WITH_EDITOR
			// Workaround for tests not starting due to the match not starting
			// Gets stuck on waiting for login in editor forever
			StartMatch();
			return;
#endif 
			if (!session->IsBusy())
			{
				session->CheckServerRegisterReady();

				UE_LOG(TitansNetwork, Log, TEXT("IGameMode: Starting Match."));
	
				StartMatch();
			}
		}
	}
}

void AIGameMode::MapReloadTimer()
{
	UWorld* World = GetWorld();
	if (!World) return;

	const UGameMapsSettings* GameMapsSettings = GetDefault<UGameMapsSettings>();
	FString DefaultMap = GameMapsSettings->GetGameDefaultMap();

	if (!DefaultMap.Contains(World->GetMapName()))
	{
		World->GetTimerManager().ClearTimer(TimerHandle_MapReloadTimer);
		return;
	}

	AIGameSession* Session = Cast<AIGameSession>(GameSession);
	if (!Session) return;

	if (Session->CheckServerLoadTargetMapReady())
	{
		World->GetTimerManager().ClearTimer(TimerHandle_MapReloadTimer);
		bMapLoaded = true;
	}
}

bool AIGameMode::CanDealDamage(AIPlayerState* DamageCauser, class AIPlayerState* DamagedPlayer) const
{
	return true;
}

void AIGameMode::CacheOTP(FString Token, FString Otp, FAlderonServerUserDetails UserInfo, int32 CacheExpire)
{
	UIGameInstance* IGameInstance = UIGameplayStatics::GetIGameInstance(this);
	if (!ensure(IGameInstance))
	{
		return;
	}

	UAlderonGameInstanceSubsystem* AlderonGIS = IGameInstance->GetSubsystem<UAlderonGameInstanceSubsystem>();
	if (!ensure(AlderonGIS))
	{
		return;
	}

	AlderonGIS->CacheOTP(Token, Otp, UserInfo, CacheExpire);
}

FOTPCache AIGameMode::FetchOtpCache(FString Token)
{
	UIGameInstance* IGameInstance = UIGameplayStatics::GetIGameInstance(this);
	if (!ensure(IGameInstance))
	{
		return FOTPCache();
	}

	UAlderonGameInstanceSubsystem* AlderonGIS = IGameInstance->GetSubsystem<UAlderonGameInstanceSubsystem>();
	if (!ensure(AlderonGIS))
	{
		return FOTPCache();
	}

	return AlderonGIS->FetchOtpCache(Token);
}

void AIGameMode::TickOTPCache()
{
	UIGameInstance* IGameInstance = UIGameplayStatics::GetIGameInstance(this);
	if (!IGameInstance)
	{
		return;
	}

	UAlderonGameInstanceSubsystem* AlderonGIS = IGameInstance->GetSubsystem<UAlderonGameInstanceSubsystem>();
	if (!ensure(AlderonGIS))
	{
		return;
	}

	AlderonGIS->TickOTPCache();
}

FString AIGameMode::SanitizeStringForLogging(const FString& Options, FString& OutSanitized)
{
	TArray<FString> ToRemove;

	for (auto& Field: LogForbiddenFields)
	{
		const int32 StartIndex = Options.Find(Field);
		if (StartIndex!=INDEX_NONE)
		{
			int32 EndIndex = Options.Find("?", ESearchCase::IgnoreCase, ESearchDir::FromStart, StartIndex);
			if (EndIndex == INDEX_NONE)
			{
				EndIndex = Options.Len()-1;
			}

			ToRemove.Add(Options.Mid(StartIndex, EndIndex-StartIndex));
		}
	}

	FString SanitizedString = Options;
	for (auto& Part: ToRemove)
	{
		SanitizedString.ReplaceInline(*Part,TEXT("[REDACTED]"));
	}

	return SanitizedString;
}

FString AIGameMode::InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("AIGameMode::InitNewPlayer"))

	FString OutSanitized;
	SanitizeStringForLogging(Options, OutSanitized);
	
	UE_LOG(TitansNetwork, Log, TEXT("AIGameMode::InitNewPlayer(%s)"), *OutSanitized);

	AIGameSession* IGameSession = Cast<AIGameSession>(GameSession);
	check(IGameSession);

	// Check OTP / Encryption Token
	FString Otp = UGameplayStatics::ParseOption(Options, TEXT("OTP"));
	FString EncryptionToken = UGameplayStatics::ParseOption(Options, TEXT("EncryptionToken"));

	if (Otp.IsEmpty() && EncryptionToken.IsEmpty())
	{
#if !WITH_EDITOR
		UE_LOG(TitansLog, Error, TEXT("AIGameMode::InitNewPlayer() No OTP or Encryption Token Found in: %s"), *Options);
	#if UE_BUILD_SHIPPING
		if (IsRunningDedicatedServer())
		{
			return TEXT("JOIN_NO_OTP");
		}
	#endif
#endif
	}

	// Check Platform First
	FString PlatformString = UGameplayStatics::ParseOption(Options, TEXT("PT"));
	if (PlatformString.IsEmpty())
	{
		UE_LOG(TitansLog, Error, TEXT("AIGameMode::InitNewPlayer() Platform not found in: %s"), *Options);
		return TEXT("JOIN_NO_PLATFORM");
	}

	// Ownership Second
	FString OwnershipString = UGameplayStatics::ParseOption(Options, TEXT("OS"));
	if (OwnershipString.IsEmpty())
	{
		UE_LOG(TitansLog, Error, TEXT("AIGameMode::InitNewPlayer() Ownership not found in: %s"), *Options);
		return TEXT("JOIN_NO_OWNERSHIP");
	}

	// Platform Third
	EPlatformType DetectedPlatform = EPlatformType::PT_DEFAULT;
	{
		// We need to validate this data because it comes from the client
		uint8 PlatformTypeRaw = FCString::Atoi(*PlatformString);
		const int MinPlatformNumber = static_cast<uint8>(EPlatformType::PT_WINDOWS);
		const int MaxPlatformNumber = static_cast<uint8>(EPlatformType::PT_MAX);
		if (PlatformTypeRaw >= MinPlatformNumber && PlatformTypeRaw < MaxPlatformNumber)
		{
			DetectedPlatform = static_cast<EPlatformType>(PlatformTypeRaw);
		}
		else {
			UE_LOG(TitansLog, Error, TEXT("AIGameMode::InitNewPlayer() Processing Platform from String to Enum Failure %s"), *PlatformString);
		}
	}

	EGameOwnership DetectedOwnership = EGameOwnership::Free;
	{
		// We need to validate this data because it comes from the client
		uint8 OwnershipTypeRaw = FCString::Atoi(*OwnershipString);
		const int MinOwnershipNumber = static_cast<uint8>(EGameOwnership::None);
		const int MaxOwnershipNumber = static_cast<uint8>(EGameOwnership::MAX);
		if (OwnershipTypeRaw >= MinOwnershipNumber && OwnershipTypeRaw < MaxOwnershipNumber)
		{
			DetectedOwnership = static_cast<EGameOwnership>(OwnershipTypeRaw);
		}
		else {
			UE_LOG(TitansLog, Error, TEXT("AIGameMode::InitNewPlayer() Processing Ownership from String to Enum Failure %s"), *OwnershipString);
		}
	}

	if (IGameSession->bServerPaidUsersOnly && DetectedOwnership == EGameOwnership::Free)
	{
		return TEXT("PAID_GAME_ONLY");
	}

	// Alderon Player ID
	FString PlayerID = UGameplayStatics::ParseOption(Options, TEXT("PID"));
	if (PlayerID.IsEmpty())
	{
		UE_LOG(TitansLog, Error, TEXT("AIGameMode::InitNewPlayer() No Player ID Found in: %s"), *Options);
#if UE_BUILD_SHIPPING
		return TEXT("JOIN_NO_PLAYERID");
#endif
	}

	/*
	// Platform Nickname
	FString PlatformNickname = UGameplayStatics::ParseOption(Options, TEXT("PN"));
	if (PlatformNickname.IsEmpty())
	{
		if (DetectedPlatform == EPlatformType::PT_XBOXONE || DetectedPlatform == EPlatformType::PT_XSX)
		{
			FString PlatformNicknamePrefix = UGameplayStatics::ParseOption(Options, TEXT("PNP"));
			FString PlatformNicknameSuffix = UGameplayStatics::ParseOption(Options, TEXT("PNS"));

			PlatformNickname = PlatformNicknamePrefix;
			if (!PlatformNicknameSuffix.IsEmpty())
			{
				PlatformNickname += TEXT("#") + PlatformNicknameSuffix;
			}

			if (PlatformNickname.IsEmpty())
			{
#if UE_BUILD_SHIPPING
				return TEXT("INVALID_PLATFORM_NAME");
#endif
			}
		}
	}

	if (PlatformTypeIsGameConsole(DetectedPlatform) && PlatformNickname.IsEmpty())
	{
		UE_LOG(TitansLog, Error, TEXT("AIGameMode::InitNewPlayer() No PlatformNickname ID Found in: %s"), *Options);
#if UE_BUILD_SHIPPING
		return TEXT("INVALID_PLATFORM_NAME");
#endif
	}
	*/

	// Validate Player State is a valid Object
	AIPlayerState* PlayerState = NewPlayerController->GetPlayerState<AIPlayerState>();
	if (!PlayerState)
	{
		UE_LOG(TitansLog, Error, TEXT("AIGameMode::InitNewPlayer() Invalid PlayerState: Options: %s"), *Options);
		#if UE_BUILD_SHIPPING
			return TEXT("INVALID_PLAYERSTATE");
		#endif
	}

	// Set PlayerState AlderonID
	if (!PlayerID.IsEmpty())
	{
		PlayerState->SetAlderonID(PlayerID);
	}

	// Store Otp on PlayerState
	PlayerState->EncryptionToken = EncryptionToken.IsEmpty() ? Otp : EncryptionToken;

#if !WITH_EDITOR
	// Validate PlayerState AlderonID
	if (!PlayerState->GetAlderonID().IsValid())
	{
		UE_LOG(TitansLog, Error, TEXT("AIGameMode::InitNewPlayer() Invalid AlderonID: Options: %s"), *Options);
		return TEXT("INVALID_ALDERONID");
	}
#endif

//#if WITH_EDITOR
//	if (!PlayerState->AlderonID.IsValid())
//	{
//		PlayerState->AlderonID.Random();
//		UE_LOG(TitansLog, Error, TEXT("AIGameMode::InitNewPlayer: Recieved Invalid AlderonID: Generating Random One: %s"), *PlayerState->AlderonID.ToDisplayString());
//	}
//#endif

	PlayerState->SetPlatform(DetectedPlatform);
	PlayerState->SetGameOwnership(DetectedOwnership);

	// Check for AlderonId Bans
	FPlayerBan BanInfo = IGameSession->GetBanInformation(PlayerState->GetAlderonID());
	if (BanInfo.IsValid())
	{
		FString BanResponse = TEXT("BANNED");

		BanResponse += TEXT(":");
		BanResponse += FString::FromInt(BanInfo.BanExpiration);

		if (!BanInfo.UserReason.IsEmpty())
		{
			BanResponse += TEXT(":");
			BanResponse += BanInfo.UserReason;
		}

		return BanResponse;
	}

	bool bAntiCheat = false;

#if WITH_BATTLEYE_SERVER
	if (IsRunningDedicatedServer())
	{
		if (IBattlEyeServer::IsAvailable())
		{
			if (IBattlEyeServer::Get().IsEnforcementEnabled())
			{
				bAntiCheat = (DetectedPlatform == EPlatformType::PT_WINDOWS || DetectedPlatform == EPlatformType::PT_MAC || DetectedPlatform == EPlatformType::PT_LINUX || DetectedPlatform == EPlatformType::PT_DEFAULT);
			}
		}
	}
#endif

	// Register the player with the session
	const TSharedPtr<const FUniqueNetId>& InUniqueId = MakeShareable(new FUniqueNetIdAlderon(FAlderonPlayerID(PlayerID).ToDisplayString(), bAntiCheat));

	PlayerState->SetPlatformUniqueId(UniqueId);

	AIChatCommandManager* ChatCommandManager = AIChatCommandManager::Get(this);
	check(ChatCommandManager);
	ChatCommandManager->LoadPlayerRole(PlayerState);

	//This needs to be done after loading player role so things like bStealthServerAdmin and bStealthPlayerRole are set.
	PlayerState->SetIsServerAdmin(IGameSession->IsAdmin(NewPlayerController), PlayerState->GetPlayerRole().bStealthServerAdmin);
	PlayerState->SetIsGameDev(IGameSession->IsDev(NewPlayerController), PlayerState->GetPlayerRole().bStealthServerAdmin);

	// Flag players with a role as already white listed
	if (IGameSession->IsWhitelistActive() && PlayerState->GetPlayerRole().Name == TEXT("None"))
	{
		// Check for Whitelisting
		bool bPlayerWhitelisted = IGameSession->IsPlayerWhitelisted(PlayerState->GetAlderonID());
		if (!bPlayerWhitelisted)
		{
			return TEXT("WHITELIST");
		}
	}
	
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	GameSession->RegisterPlayer(NewPlayerController, InUniqueId, UGameplayStatics::HasOption(Options, TEXT("bIsFromInvite")));
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	// Check for Server Mutes
	FPlayerMute MuteInfo = IGameSession->GetServerMuteInformation(PlayerState->GetAlderonID());
	if (MuteInfo.IsValid())
	{
		PlayerState->SetIsServerMuted(true);
		PlayerState->SetMuteExpirationUnix(MuteInfo.BanExpiration);
	}

	AIPlayerController* IPlayerController = Cast<AIPlayerController>(NewPlayerController);
	if (IPlayerController && !IPlayerController->PendingKickText.IsEmpty())
	{
		return IPlayerController->PendingKickText.ToString();
	}

	// Player's Name
	FString InName = UGameplayStatics::ParseOption(Options, TEXT("Name")).Left(20);
	if (InName.IsEmpty())
	{
		InName = FString::Printf(TEXT("%s%i"), *DefaultPlayerName.ToString(), NewPlayerController->PlayerState->GetPlayerId());
		UE_LOG(LogTemp, Error, TEXT("AIGameMode::InitNewPlayer() No Name found in Options List %s"), *Options);
	}

	ChangeName(NewPlayerController, InName, false);

	//FVector SavedLocation = FVector::ZeroVector;
	//FRotator SavedRotation = FRotator::ZeroRotator;
	// GetSavedLocationRotation(UniqueId->ToString(), SavedLocation, SavedRotation)
	//if (UniqueId.IsValid())
	//{
	//	NewPlayerController->SetInitialLocationAndRotation(SavedLocation, SavedRotation);
	//}
	//else
	//{
	
	if (CharSelectPoint)
	{
		check(CharSelectPoint->Camera);
		FRotator InitialControllerRot = CharSelectPoint->Camera->GetComponentRotation();
		//InitialControllerRot.Roll = 0.f;
		NewPlayerController->SetInitialLocationAndRotation(CharSelectPoint->Camera->GetComponentLocation(), InitialControllerRot);
		NewPlayerController->StartSpot = CharSelectPoint;
	} else {
		return TEXT("INVALID_CHARSELECTPOINT");
	}

	return TEXT("");
}

APlayerController* AIGameMode::Login(UPlayer* NewPlayer, ENetRole InRemoteRole, const FString& Portal, const FString& Options, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	AIGameSession* IGameSession = Cast<AIGameSession>(GameSession);
	check(IGameSession);

	APlayerController* Controller = Super::Login(NewPlayer, InRemoteRole, Portal, Options, UniqueId, ErrorMessage);

	if (Controller)
	{
		// Check for IP Bans
		UNetConnection* NetConnection = NetConnection = Cast<UNetConnection>(NewPlayer);

		//check(NetConnection);
		if (NetConnection)
		{
			AIPlayerState* IPlayerState = Controller->GetPlayerState<AIPlayerState>();
			check(IPlayerState);

			NetConnection->PlayerId = IPlayerState->GetUniqueId();

			auto ConnectionAddress = NetConnection->GetRemoteAddr();
			if (ConnectionAddress.IsValid())
			{
				FString PlayerIP = ConnectionAddress->ToString(false);
				if (IGameSession->IsIPAddressBanned(PlayerIP))
				{
					ErrorMessage = FString(TEXT("BANNED"));
					Controller->Destroy();
					Controller = nullptr;
				}
			}
		}
	}

	return Controller;
}

float AIGameMode::ModifyDamage(float Damage, AActor* DamagedActor, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) const
{
	float ActualDamage = Damage;

	AIBaseCharacter* DamagedPawn = Cast<AIBaseCharacter>(DamagedActor);
	if (DamagedPawn && EventInstigator)
	{
		AIPlayerState* DamagedPlayerState = DamagedPawn->GetPlayerState<AIPlayerState>();
		AIPlayerState* InstigatorPlayerState = EventInstigator->GetPlayerState<AIPlayerState>();

		// Check for friendly fire
		if (!CanDealDamage(InstigatorPlayerState, DamagedPlayerState))
		{
			ActualDamage = 0.f;
			return ActualDamage;
		}
	}

	// Scale damage by weight class
	if (AIDinosaurCharacter* DamagedDino = Cast<AIDinosaurCharacter>(DamagedActor))
	{
		if (AIDinosaurCharacter* DamageCauserDino = Cast<AIDinosaurCharacter>(DamageCauser))
		{
			ActualDamage = (DamageCauserDino->GetBodyWeight() / DamagedDino->GetBodyWeight()) * Damage;
		}
	}
	
	return ActualDamage;
}

/* Used by RestartPlayer() to determine the pawn to create and possess when a bot or player spawns */
//UClass* AIGameMode::GetDefaultPawnClassForController_Implementation(AController* InController)
//{
//	return Super::GetDefaultPawnClassForController_Implementation(InController);
//}

bool AIGameMode::CanSpectate_Implementation(APlayerController* Viewer, APlayerState* ViewTarget)
{
	/* Don't allow spectating of other non-player bots */
	return (ViewTarget && !ViewTarget->IsABot());
}

void AIGameMode::Killed(AController* const Killer, AController* VictimPlayer, APawn* const VictimPawn, const EDamageType DamageType)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("AIGameMode::Killed"))
	
	// Add Deaths, Kill, Update Quests
	{
		// Increase Death Count
		if (VictimPlayer)
		{
			AIPlayerState* VictimState = Cast<AIPlayerState>(VictimPlayer->PlayerState);
			if (VictimState)
			{
				VictimState->AddDeath();

				AIBaseCharacter* VictimBaseCharRevKill = Cast<AIBaseCharacter>(VictimPawn);
				check(VictimBaseCharRevKill);
				if (VictimBaseCharRevKill)
				{
					// Handle Revenge Kill Flag
					FVector VictimRevengeKillLocation = VictimPawn->GetActorLocation();

					// Reset SaveCharacterPosition to prevent applying RevengeKill to dead characters.
					VictimBaseCharRevKill->SaveCharacterPosition = FVector::ZeroVector;
					if (FCharacterData* CharacterData = VictimState->CharactersData.Find(VictimBaseCharRevKill->GetCharacterID()))
					{
						CharacterData->LastKnownPosition = FVector::ZeroVector;
					}

					if (!VictimRevengeKillLocation.IsZero())
					{
						FlagRevengeKill(VictimBaseCharRevKill->GetCharacterID(), VictimState, VictimRevengeKillLocation);						
					}
				}
			}
		}

		// Don't Add Kill Count when you kill yourself
		if (Killer != VictimPlayer)
		{
			// Increase Kill Count
			if (Killer)
			{
				AIPlayerState * KillerState = Cast<AIPlayerState>(Killer->PlayerState);
				if (KillerState)
				{
					KillerState->AddKill();
				}

				// Quests that involve dying
				AIQuestManager* QuestMgr = AIWorldSettings::GetWorldSettings(this)->QuestManager;
				if (QuestMgr)
				{
					AIBaseCharacter* KillerCharacter = Cast<AIBaseCharacter>(Killer->GetPawn());
					AIBaseCharacter* VictimCharacter = Cast<AIBaseCharacter>(VictimPawn);
					QuestMgr->OnCharacterKilled(KillerCharacter, VictimCharacter);
				}
			}
		}
	}

	// Player Killed Webhook
	if (AIGameSession::UseWebHooks(WEBHOOK_PlayerKilled))
	{
		int32 TimeOfDay = -1;

		AIWorldSettings* IWorldSettings = AIWorldSettings::GetWorldSettings(this);
		if (IWorldSettings)
		{
			AIUltraDynamicSky* Sky = IWorldSettings->UltraDynamicSky;
			if (Sky)
			{
				//Precision loss on purpose.
				TimeOfDay = Sky->LocalTimeOfDay;
			}
		}

		FText DamageTypeName{};
		//Finds the localized display name or native display name as a fallback.
		UEnum::GetDisplayValueAsText(DamageType, DamageTypeName);
		
		AIGameSession* IGameSession = Cast<AIGameSession>(GameSession);
		if (IGameSession)
		{
			FString VictimName, VictimAlderonId, VictimDinosaurType, VictimRoleName, VictimLocation, POI = FString();
			FString KillerName, KillerAlderonId, KillerDinosaurType, KillerRoleName, KillerLocation = FString();
			bool bKillerIsAdmin = false;
			bool bVictimIsAdmin = false;
			float VictimGrowth = -1.0f;
			float KillerGrowth = -1.0f;

			if (VictimPlayer)
			{
				AIPlayerController* VictimPlayerController = Cast<AIPlayerController>(VictimPlayer);
				if (VictimPlayerController)
				{
					AIPlayerState* IPlayerState = VictimPlayerController->GetPlayerState<AIPlayerState>();
					if (IPlayerState)
					{
						VictimName = IPlayerState->GetPlayerName();
						VictimAlderonId = IPlayerState->GetAlderonID().ToDisplayString();
						VictimRoleName = IPlayerState->GetPlayerRole().bAssigned ? IPlayerState->GetPlayerRole().Name : TEXT("");

						bVictimIsAdmin = AIChatCommandManager::Get(this)->CheckAdmin(VictimPlayerController);

						AIBaseCharacter* VictimIPlayer = Cast<AIBaseCharacter>(VictimPawn);
						if (VictimIPlayer)
						{
							VictimDinosaurType = IPlayerState->GetCharacterSpecies().ToString();
							VictimGrowth = VictimIPlayer->GetGrowthPercent();
							POI = VictimIPlayer->LocationDisplayName.ToString();
						}
					}
					VictimLocation = VictimPlayerController->GetMapBug();
				}
			}

			if (Killer)
			{
				AIPlayerController* KillerPlayerController = Cast<AIPlayerController>(Killer);
				if (KillerPlayerController)
				{
					AIPlayerState* IPlayerState = KillerPlayerController->GetPlayerState<AIPlayerState>();
					if (IPlayerState)
					{
						KillerName = IPlayerState->GetPlayerName();
						KillerAlderonId = IPlayerState->GetAlderonID().ToDisplayString();
						KillerRoleName = IPlayerState->GetPlayerRole().bAssigned ? IPlayerState->GetPlayerRole().Name : TEXT("");

						bKillerIsAdmin = AIChatCommandManager::Get(this)->CheckAdmin(KillerPlayerController);

						AIBaseCharacter* KillerPlayer = Cast<AIBaseCharacter>(KillerPlayerController->GetPawn());
						if (KillerPlayer)
						{
							KillerDinosaurType = IPlayerState->GetCharacterSpecies().ToString();
							KillerGrowth = KillerPlayer->GetGrowthPercent();
						}
					}

					KillerLocation = KillerPlayerController->GetMapBug();
				}
			}

			if (VictimName != "" && VictimAlderonId != "")
			{
				TMap<FString, TSharedPtr<FJsonValue>> WebHookProperties
				{
					{ TEXT("TimeOfDay"), MakeShareable(new FJsonValueNumber(TimeOfDay)) },
					{ TEXT("DamageType"), MakeShareable(new FJsonValueString(DamageTypeName.ToString())) },
					{ TEXT("VictimPOI"), MakeShareable(new FJsonValueString(POI)) },
					
					{ TEXT("VictimName"), MakeShareable(new FJsonValueString(VictimName)) },
					{ TEXT("VictimAlderonId"), MakeShareable(new FJsonValueString(VictimAlderonId)) },
					{ TEXT("VictimDinosaurType"), MakeShareable(new FJsonValueString(VictimDinosaurType)) },
					{ TEXT("VictimRole"), MakeShareable(new FJsonValueString(VictimRoleName)) },
					{ TEXT("VictimIsAdmin"), MakeShareable(new FJsonValueBoolean(bVictimIsAdmin)) },
					{ TEXT("VictimGrowth"), MakeShareable(new FJsonValueNumber(VictimGrowth)) },
					{ TEXT("VictimLocation"), MakeShareable(new FJsonValueString(VictimLocation)) },

					{ TEXT("KillerName"), MakeShareable(new FJsonValueString(KillerName)) },
					{ TEXT("KillerAlderonId"), MakeShareable(new FJsonValueString(KillerAlderonId)) },
					{ TEXT("KillerDinosaurType"), MakeShareable(new FJsonValueString(KillerDinosaurType)) },
					{ TEXT("KillerRole"), MakeShareable(new FJsonValueString(KillerRoleName)) },
					{ TEXT("KillerIsAdmin"), MakeShareable(new FJsonValueBoolean(bKillerIsAdmin)) },
					{ TEXT("KillerGrowth"), MakeShareable(new FJsonValueNumber(KillerGrowth)) },
					{ TEXT("KillerLocation"), MakeShareable(new FJsonValueString(KillerLocation)) },
				};

				IGameSession->TriggerWebHook(WEBHOOK_PlayerKilled, WebHookProperties);
			}
		}
	}


	// Trigger Death Save
	TStrongObjectPtr<AIBaseCharacter> CharacterToSave = TStrongObjectPtr<AIBaseCharacter>(Cast<AIBaseCharacter>(VictimPawn));

	if (CharacterToSave.IsValid())
	{
		// Handle Special Case where a player dies and or combat log AI
		FAsyncOperationCompleted OnPlayerKilledSaved = FAsyncOperationCompleted::CreateLambda([this,CharacterToSave](bool bSuccess)
		{
			if (CharacterToSave.IsValid())
			{
				if (CharacterToSave->GetCharacterID().IsValid())
				{
					// Handle Special Edge Cases with Combat Log AI
					if (CharacterToSave->IsCombatLogAI())
					{
						bool bDestroy = false;
						bool bRemoveTimestamp = true;
						bool bSave = false;
						FAlderonUID SaveCharacterID = CharacterToSave->GetCharacterID();
						RemoveCombatLogAI(SaveCharacterID, bDestroy, bRemoveTimestamp, bSave);
					}
				}
			}
		});

		SaveCharacterAsync(CharacterToSave.Get(), OnPlayerKilledSaved, ESavePriority::High);

		if (DatabaseEngine)
		{
			const bool bServerShutdown = false;
			DatabaseEngine->Flush(bServerShutdown);
		}
	}
}

void AIGameMode::FlagRevengeKill(const FAlderonUID& SkipCharacterId, AIPlayerState* IPlayerState, FVector RevengeKillLocation)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("AIGameMode::FlagRevengeKill"))

	if (!IPlayerState)
	{
		UE_LOG(LogTemp, Error, TEXT("AIGameMode::FlagRevengeKill: IPlayerState is nullptr"));
		return;
	}

	AIGameSession* IGameSession = Cast<AIGameSession>(GameSession);
	check(IGameSession);

	if (!IGameSession->bServerAntiRevengeKill)
	{
		//UE_LOG(LogTemp, Error, TEXT("AIGameMode::FlagRevengeKill: bServerAntiRevengeKill is disabled, skipping."));
		return;
	}

	// Support was added
	//if (DatabaseEngine->GetDatabaseType() != EDatabaseType::Remote)
	//{
	//	UE_LOG(LogTemp, Error, TEXT("AIGameMode::FlagRevengeKill: bServerAntiRevengeKill currently only works with Remote Database."));
	//	return;
	//}

	for (auto& Elem : IPlayerState->CharactersData)
	{
		const FAlderonUID& CharacterId = Elem.Key;
		FCharacterData& CharacterData = Elem.Value;

		if (CharacterId == SkipCharacterId)
		{
			continue;
		}

		if (CharacterData.LastKnownPosition.IsZero())
		{
			// Skip Character if we don't have a last known position
			continue;
		}

		float FoundDistance = (RevengeKillLocation - CharacterData.LastKnownPosition).Size();
		if (FoundDistance <= IGameSession->RevengeKillDistance)
		{
			//UE_LOG(LogTemp, Verbose, TEXT("AIGameMode::FlagRevengeKill: Adding Revenge Kill Flag on Character: %s (%s) Distance: %f"), *CharacterData.Name, *CharacterId.ToString(), FoundDistance);

			FDatabaseOperationCompleted OnCompleted = FDatabaseOperationCompleted::CreateLambda([this, CharacterId](const FDatabaseOperationData& Data)
			{
				PendingRevengeKillFlags.Remove(CharacterId);

				if (Data.bSuccess)
				{
					//UE_LOG(LogTemp, Verbose, TEXT("AIGameMode::FlagRevengeKill: Set Timestamp RevengeKill for CharcterId %s"), *CharacterId.ToString());
				} else {
					UE_LOG(LogTemp, Error, TEXT("AIGameMode::FlagRevengeKill: Failed to set Timestamp RevengeKill for CharacterId %s"), *CharacterId.ToString());
				}
			});

			PendingRevengeKillFlags.Add(CharacterId);
			DatabaseEngine->SetCharacterTimestamp(TEXT("RevengeKill"), 300, CharacterId, OnCompleted);
		}
		else
		{
			//UE_LOG(LogTemp, Verbose, TEXT("AIGameMode::FlagRevengeKill: Skipping Revenge Kill Flag on Character: %s (%s) Distance: %f"), *CharacterData.Name, *CharacterId.ToString(), FoundDistance);
		}
	}
}

void AIGameMode::AddCombatLogAI(AIBaseCharacter* Character, const FAlderonUID& CharacterId)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("AIGameMode::AddCombatLogAI"))

	// Null Characters should never be passed in
	check(Character);
	if (Character == nullptr || !Character->IsValidLowLevel())
	{
		UE_LOG(LogTemp, Error, TEXT("AIGameMode::AddCombatLogAI: Character is null / invalid: CharacterId %s"), *CharacterId.ToString());
		return;
	}

	if (!ShouldSpawnCombatLogAI())
	{
		Character->Destroy();
		return;
	}

	//check(CharacterId.IsValid());
	if (!CharacterId.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("AIGameMode::AddCombatLogAI: Invalid CharacterId %s"), *CharacterId.ToString());
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("AIGameMode::AddCombatLogAI: CharcterId %s"), *CharacterId.ToString());

	// Don't allow Combat Log AI to be added that are dead
	if (!Character->IsAlive())
	{
		UE_LOG(LogTemp, Log, TEXT("AIGameMode::AddCombatLogAI: Skipping add of Combat Log AI as it's dead. CharacterId: %s"), *CharacterId.ToString());
		return;
	}

	check(!CombatLogAI.Contains(CharacterId));
	if (CombatLogAI.Contains(CharacterId))
	{
		UE_LOG(LogTemp, Error, TEXT("AIGameMode::AddCombatLogAI: Duplicate Key already found CharacterId: %s"), *CharacterId.ToString());
		return;
	}

	// Backup Head Movement Rotation
	FRotator ViewRotation = Character->GetControlRotation();
	
	CombatLogAI.Add(CharacterId, Character);
	
	Character->SetCombatLogAI(true);

	AAIController* LoggedOutController = GetWorld()->SpawnActor<AAIController>();
	if (LoggedOutController)
	{
		// Solves an issue where the CMC becomes largely inactive without a controller attached
		LoggedOutController->Possess(Character);

		// Restore Head Movement Rotation
		LoggedOutController->SetControlRotation(ViewRotation);
	}

	// Only attempt to apply timestamps on Cross Server Remote Databases
	if (DatabaseEngine->GetDatabaseType() == EDatabaseType::Remote)
	{
		FDatabaseOperationCompleted OnCompleted = FDatabaseOperationCompleted::CreateLambda([=](const FDatabaseOperationData& Data)
		{
			if (Data.bSuccess)
			{
				UE_LOG(LogTemp, Log, TEXT("AIGameMode::AddCombatLogAI: Set Timestamp CombatLog for CharcterId %s"), *CharacterId.ToString());
			} else {
				UE_LOG(LogTemp, Error, TEXT("AIGameMode::AddCombatLogAI: Failed to set Timestamp CombatLog for CharacterId %s"), *CharacterId.ToString());
			}
		});

		DatabaseEngine->SetCharacterTimestamp(TEXT("CombatLog"), 120, CharacterId, OnCompleted);
	}
}

AIBaseCharacter* AIGameMode::GetCombatLogAI(const FAlderonUID& CharacterId)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("AIGameMode::GetCombatLogAI"))

	AIBaseCharacter* CombatAI = nullptr;

	if (CombatLogAI.Contains(CharacterId))
	{
		CombatAI = CombatLogAI[CharacterId];
		if (!IsValid(CombatAI))
		{
			CombatLogAI.Remove(CharacterId);
			return nullptr;
		}
	}

	return CombatAI;
}

void AIGameMode::RemoveCombatLogAI(const FAlderonUID& CharacterId, bool bDestroy, bool bRemoveTimestamp, bool bSave)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("AIGameMode::RemoveCombatLogAI"))

	UE_LOG(LogTemp, Log, TEXT("AIGameMode::RemoveCombatLogAI: CharcterId %s bDestroy: %i bRemoveTimestamp: %i"), *CharacterId.ToString(), bDestroy, bRemoveTimestamp);

	AIBaseCharacter* Character = nullptr;
	CombatLogAI.RemoveAndCopyValue(CharacterId, Character);

	if (Character)
	{
		if (bDestroy)
		{
			TArray<UIQuest*> ActiveQuests = Character->GetActiveQuests();
			for (int i = ActiveQuests.Num(); i-- > 0; )
			{
				UIQuest* ActiveQuest = ActiveQuests[i];

				if (!ActiveQuest) continue;

				if (ActiveQuest->GetPlayerGroupActor() || ActiveQuest->QuestData->QuestType == EQuestType::TrophyDelivery)
				{
					ActiveQuests.Remove(ActiveQuest);
				}
			}

			if (ActiveQuests.Num() != Character->GetActiveQuests().Num())
			{
				Character->GetActiveQuests_Mutable() = ActiveQuests;
			}

			if (bSave)
			{
				SaveCombatLogAI(Character, bDestroy);
			} else {
				Character->Destroy();
			}

		} else {
			if (Character->IsCombatLogAI())
			{
				Character->SetCombatLogAI(false);
			}
		}

		// Expire Combat Log AI Timestamp Only on Cross Server Remote Databases
		if (bRemoveTimestamp && DatabaseEngine->GetDatabaseType() == EDatabaseType::Remote)
		{
			FDatabaseOperationCompleted OnCompleted = FDatabaseOperationCompleted::CreateLambda([=](const FDatabaseOperationData& Data)
			{
				if (Data.bSuccess)
				{
					UE_LOG(LogTemp, Log, TEXT("AIGameMode::RemoveCombatLogAI: Removed Timestamp CombatLog for CharacterId %s"), *CharacterId.ToString());
				} else {
					UE_LOG(LogTemp, Error, TEXT("AIGameMode::RemoveCombatLogAI: Failed to remove Timestamp CombatLog for CharacterId %s"), *CharacterId.ToString());
				}
			});

			// Setting a timestamp to -1 should force it to be expired
			// if you want to be super safe use -1 incase you somehow land a request on the same second and it's still "not expired" technically.
			DatabaseEngine->SetCharacterTimestamp(TEXT("CombatLog"), 1, CharacterId, OnCompleted);
		}
	}
}

AActor* AIGameMode::FindPlayerStart_Implementation(AController* Player, const FString& IncomingName)
{
	return CharSelectPoint;
}

void AIGameMode::SaveDataForShutdown()
{
#if UE_SERVER
	// Skip duplicate shutdown saves
	if (bSavedDataForShutdown) return;

	bSavedDataForShutdown = true;

	UE_LOG(TitansCharacter, Log, TEXT("--------------[SAVE DATA FOR SHUTDOWN]----------------"));

	if (AIWorldSettings* IWorldSettings = AIWorldSettings::GetWorldSettings(this))
	{
		if (AIWaterManager* WaterManager = IWorldSettings->WaterManager)
		{
			UE_LOG(TitansCharacter, Log, TEXT("--------------[SAVING WATER DATA]----------------"));
			WaterManager->SaveWaterData();
		}

		if (AIWaystoneManager* WaystoneManager = IWorldSettings->WaystoneManager)
		{
			UE_LOG(TitansCharacter, Log, TEXT("--------------[SAVING WAYSTONE DATA]----------------"));
			WaystoneManager->SaveWaystoneData();
		}
	}

	const FString& CharacterDataSaving = FString(TEXT("Saving Character Data for Server Shutdown"));

	UE_LOG(TitansCharacter, Log, TEXT("--------------[SAVING PLAYER DATA]----------------"));
	for (FConstPlayerControllerIterator Iterator = GetWorld()->GetPlayerControllerIterator(); Iterator; ++Iterator)
	{
		AIPlayerController* IPlayerController = Cast<AIPlayerController>(*Iterator);
		if (!IPlayerController) continue;
		if (!IsValid(IPlayerController)) continue;

		IPlayerController->ClientRecieveAnnouncement(CharacterDataSaving);

		// Save Players Info
		SaveAll(IPlayerController, ESavePriority::Medium);
	}
	
	if (DatabaseEngine)
	{
		const bool bServerShutdown = true;
		DatabaseEngine->Flush(bServerShutdown);
	}

	// Flush Database HTTPs Requests so they get sent out!
	// Infinite wait, should only be used in non-game scenarios where longer waits are acceptable
	FHttpManager& HttpManager = FHttpModule::Get().GetHttpManager();
	HttpManager.Flush(EHttpFlushReason::FullFlush);
#endif
}

void AIGameMode::ProcessRestart()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("AIGameMode::ProcessRestart"))

	if (AIGameSession::UseWebHooks(WEBHOOK_ServerRestartCountdown))
	{

		// Call Time Remaining Webhook
		{
			TMap<FString, TSharedPtr<FJsonValue>> WebHookProperties
			{
				{ TEXT("RestartTimeRemaining"), MakeShareable(new FJsonValueNumber(0))},
			};
			AIGameSession::TriggerWebHookFromContext(this, WEBHOOK_ServerRestartCountdown, WebHookProperties);
		}

		// Call Server Restart Webhook
		if(AIGameSession::UseWebHooks(WEBHOOK_ServerRestart))
		{
			TMap<FString, TSharedPtr<FJsonValue>> WebHookProperties{ };
			AIGameSession::TriggerWebHookFromContext(this, WEBHOOK_ServerRestart, WebHookProperties);
		}
	}

#if UE_SERVER
	SaveDataForShutdown();

	// Unregister the server
	IAlderonCommon::Get().UnregisterServer();

	// Shutdown
	FGenericPlatformMisc::RequestExit(false);
#endif
}

void AIGameMode::SendRestartNotices(bool bForce /*= false*/)
{
	const UWorld* const CurrentWorld = GetWorld();
	if (!CurrentWorld)
	{
		UE_LOG(LogTemp, Error, TEXT("AIGameMode::SendRestartNotices() - World is invalid!"));
		return;
	}

	FTimerManager& TimerManager = CurrentWorld->GetTimerManager();

	TimerManager.ClearTimer(TimerHandle_RestartNoticeDelay);

	const int32 TimeTillRestart = TimerManager.GetTimerRemaining(TimerHandle_RestartDelay);
	
	UE_LOG(LogTemp, Log, TEXT("AIGameMode::SendRestartNotices() - Sending restart notice to clients! - time till restart: %d seconds"), TimeTillRestart);
	
	// Send restart notice to all clients
	int32 SentNotices = 0;
	for (FConstPlayerControllerIterator Iterator = CurrentWorld->GetPlayerControllerIterator(); Iterator; ++Iterator)
	{
		AIPlayerController* const IPlayerController = Cast<AIPlayerController>(*Iterator);
		if (!IsValid(IPlayerController)) continue;
		IPlayerController->ClientRecieveRestartNotice(bForce ? TimeTillRestart : LastRestartNoticeTimestamp);
		SentNotices++;
	}
	
	UE_LOG(LogTemp, Log, TEXT("AIGameMode::SendRestartNotices() - Sent %d notices out"), SentNotices);

	if (LastRestartNoticeTimestamp > 1 || bForce)
	{
		StartRestartNoticeTimer();
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("AIGameMode::SendRestartNotices() - restart notice is now unscheduled"));
	}
}

void AIGameMode::StartRestartNoticeTimer()
{
	UWorld* CurrentWorld = GetWorld();
	AIGameSession* const IGameSession = Cast<AIGameSession>(GameSession);
	if (!CurrentWorld || !IGameSession)
	{
		UE_LOG(LogTemp, Error, TEXT("AIGameMode::StartRestartNoticeTimer() - World or GameSession is invalid!"));
		return;
	}

	FTimerManager& TimerManager = CurrentWorld->GetTimerManager();
	
	if (!TimerManager.IsTimerActive(TimerHandle_RestartDelay))
	{
		UE_LOG(LogTemp, Error, TEXT("AIGameMode::StartRestartNoticeTimer() - Restart timer is not active or doesnt exist, cannot start the notice timer."));
		return;
	}
	
	const int32 TimeTillRestart = TimerManager.GetTimerRemaining(TimerHandle_RestartDelay);
	int32 RestartNotificationTimestamp = 0;

	const TArray<int32>& NotificationTimestamps = IGameSession->GetRestartNotificationTimestamps();

	if (!NotificationTimestamps.IsEmpty())
	{
		for (int32 i = 0; i < NotificationTimestamps.Num(); i++)
		{
			if (LastRestartNoticeTimestamp == -1)
			{
				if (TimeTillRestart > NotificationTimestamps[i])
				{
					RestartNotificationTimestamp = NotificationTimestamps[i];
					break;
				}
			}
			else
			{
				if (LastRestartNoticeTimestamp == NotificationTimestamps[i])
				{
					if (NotificationTimestamps.IsValidIndex(i + 1))
					{
						RestartNotificationTimestamp = NotificationTimestamps[i + 1];
						break;
					}
				}
			}
		}
	}

	const int32 TimeTillNextNotification = TimeTillRestart - RestartNotificationTimestamp;
	const int32 TimeTillNextNotificationClamped = FMath::Clamp(TimeTillNextNotification, 1, 86400);

	const FTimerDelegate SendNotificationDel = FTimerDelegate::CreateUObject(this, &AIGameMode::SendRestartNotices, false);
	
	TimerManager.SetTimer(TimerHandle_RestartNoticeDelay, SendNotificationDel, TimeTillNextNotificationClamped, false);

	if (AIGameSession::UseWebHooks(WEBHOOK_ServerRestartCountdown))
	{
		TMap<FString, TSharedPtr<FJsonValue>> WebHookProperties
		{
			{ TEXT("RestartTimeRemaining"), MakeShareable(new FJsonValueNumber(TimeTillRestart))},
		};
		AIGameSession::TriggerWebHookFromContext(this, WEBHOOK_ServerRestartCountdown, WebHookProperties);
	}

	UE_LOG(LogTemp, Log, TEXT("AIGameMode::StartRestartNoticeTimer - Restart notice timer started - seconds till next notice %d"), TimeTillNextNotificationClamped);
	LastRestartNoticeTimestamp = RestartNotificationTimestamp;
}


void AIGameMode::StartRestartTimer(int32 Seconds)
{
	UE_LOG(LogTemp, Log, TEXT("AIGameMode::StartRestartTimer - Restart timer started! - Seconds %d"), Seconds);
	UWorld* CurrentWorld = GetWorld();
	if (!CurrentWorld) return;
	
	IAlderonCommon& AlderonCommon = IAlderonCommon::Get();
	if (AlderonCommon.IsShutdownPending())
	{
		UE_LOG(LogTemp, Log, TEXT("AIGameMode::StartRestartTimer - Cannot override update restart timer unless the cancelrestart command is used first!"));
		return;
	}

	FTimerManager& TimerManager = CurrentWorld->GetTimerManager();
	TimerManager.ClearTimer(TimerHandle_RestartNoticeDelay);
	LastRestartNoticeTimestamp = -1;

	TimerManager.ClearTimer(TimerHandle_RestartDelay);
	TimerManager.SetTimer(TimerHandle_RestartDelay, this, &AIGameMode::ProcessRestart, Seconds, false);

	AlderonCommon.SetRestartTimerHandle(TimerHandle_RestartDelay);

	// Force show start of restart delay
	SendRestartNotices(true);
}

void AIGameMode::StopAutoRestart()
{
	UWorld* CurrentWorld = GetWorld();
	if (!CurrentWorld)
	{
		UE_LOG(LogTemp, Error, TEXT("AIGameMode::StopAutoRestart() - World is invalid!"));
		return;
	}

	FTimerManager& TimerManager = CurrentWorld->GetTimerManager();
	IAlderonCommon& AlderonCommon = IAlderonCommon::Get();
	AlderonCommon.CancelShutdownPending(); // Allow the game to check for updates again

	TimerManager.ClearTimer(TimerHandle_RestartDelay);
	TimerManager.ClearTimer(TimerHandle_RestartNoticeDelay);

	UE_LOG(LogTemp, Log, TEXT("AIGameMode::StopAutoRestart() - Restart timer stopped!"));
}

EAcceptInviteResult AIGameMode::CanJoinGroup(AIPlayerGroupActor* CurrentPlayerGroupActor, AIPlayerState* SourceNewLeader, AIPlayerState* SourceNewMember, bool bNewGroup /*= false*/)
{
	if (!EnoughSlotsInGroup(CurrentPlayerGroupActor, SourceNewLeader, SourceNewMember, bNewGroup))
	{
		return EAcceptInviteResult::NotEnoughSlots;
	}
	
	if (!GroupAllowsCharacterType(CurrentPlayerGroupActor, SourceNewLeader, SourceNewMember))
	{
		return EAcceptInviteResult::CharacterTypeMismatch;
	}

	return EAcceptInviteResult::Success;
}

EAcceptInviteResult AIGameMode::CanAcceptWaystone(AIPlayerState* SourceNewLeader, AIPlayerState* SourceNewMember)
{
	if (SourceNewLeader->GetCharacterType() != SourceNewMember->GetCharacterType())
	{
		return EAcceptInviteResult::CharacterTypeMismatch;
	}

	if (AIBaseCharacter* MemberPawn = SourceNewMember->GetPawn<AIBaseCharacter>())
	{
		if (MemberPawn->IsWaystoneInProgress())
		{
			return EAcceptInviteResult::WaystoneInUse;
		}
	}

	return EAcceptInviteResult::Success;
}

bool AIGameMode::EnoughSlotsInGroup(AIPlayerGroupActor* CurrentPlayerGroupActor, AIPlayerState* SourceNewLeader, AIPlayerState* SourceNewMember, bool bNewGroup /*= false*/)
{
	if (GetWorld())
	{
		if (AIGameMode* IGameMode = GetWorld()->GetAuthGameMode<AIGameMode>())
		{
			if (bNewGroup && SourceNewLeader && SourceNewMember)
			{
				// Enough Slots
				if (SourceNewLeader && SourceNewLeader->GetGroupSlotRequirement() + SourceNewMember->GetGroupSlotRequirement() <= IGameMode->MaxGroupSize)
				{
					//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::Printf(TEXT("AIGameMode::EnoughSlotsInGroup - Source Group Slots: %s | Member Group Slots: %s"), *FString::FromInt(SourceNewLeader->GetGroupSlotRequirement()), *FString::FromInt(SourceNewMember->GetGroupSlotRequirement())));
					//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::Printf(TEXT("AIGameMode::EnoughSlotsInGroup - IGameMode->MaxGroupSize: %s"), *FString::FromInt(IGameMode->MaxGroupSize)));
					return true;
				}
			}
			else if (CurrentPlayerGroupActor && SourceNewMember)
			{
				// Enough Slots
				if (CurrentPlayerGroupActor->GetGroupSlotsFilled() + SourceNewMember->GetGroupSlotRequirement() <= IGameMode->MaxGroupSize)
				{
					//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::Printf(TEXT("AIGameMode::EnoughSlotsInGroup - GroupSlotsFilled: %s | Member Group Slots: %s"), *FString::FromInt(CurrentPlayerGroupActor->GroupSlotsFilled), *FString::FromInt(SourceNewMember->GetGroupSlotRequirement())));
					//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::Printf(TEXT("AIGameMode::EnoughSlotsInGroup - IGameMode->MaxGroupSize: %s"), *FString::FromInt(IGameMode->MaxGroupSize)));

					return true;
				}
			}
		}
	}

	return false;
}

bool AIGameMode::GroupAllowsCharacterType(AIPlayerGroupActor* CurrentPlayerGroupActor, AIPlayerState* SourceNewLeader, AIPlayerState* SourceNewMember)
{
	if (!SourceNewLeader || !SourceNewMember) return false;

	if (SourceNewLeader->GetCharacterType() == SourceNewMember->GetCharacterType())
	{
		if (SourceNewLeader->GetCharacterType() == ECharacterType::CARNIVORE)
		{
			if (bServerRestrictCarnivoreGrouping)
			{
				// If we are restricting carnivore grouping, only allow it if they are the same species
				if (SourceNewLeader->GetCharacterAssetId() != SourceNewMember->GetCharacterAssetId()) return false;
			}

			return true;
		}
		else if (SourceNewLeader->GetCharacterType() == ECharacterType::HERBIVORE)
		{
			if (bServerRestrictHerbivoreGrouping)
			{
				// If we are restricting carnivore grouping, only allow it if they are the same species
				if (SourceNewLeader->GetCharacterAssetId() != SourceNewMember->GetCharacterAssetId()) return false;
			}

			return true;
		}
	}

	return false;
}

void AIGameMode::ProcessGroupInvite(AIPlayerState* SourcePlayerState, AIPlayerState* TargetPlayerState, bool bAccepted)
{
	if (!SourcePlayerState) return;
	if (!TargetPlayerState) return;

	AIPlayerController* LeaderPlayerController = SourcePlayerState->GetOwner<AIPlayerController>();
	if (!LeaderPlayerController) return;

	AIPlayerController* NewMemberPlayerController = TargetPlayerState->GetOwner<AIPlayerController>();
	if (!NewMemberPlayerController) return;

	AIPlayerGroupActor* SourceGroupActor = SourcePlayerState->GetPlayerGroupActor();

	if (bAccepted)
	{
		// Cancel out early as new member is already in a group
		if (TargetPlayerState->GetPlayerGroupActor())
		{
			NewMemberPlayerController->ClientHandleInviteResult(EInviteResult::AlreadyInGroup_ToMember, SourcePlayerState, TargetPlayerState);
			return;
		}

		EInstanceType SourceInstance = SourcePlayerState->GetInstanceType();
		EInstanceType TargetInstance = TargetPlayerState->GetInstanceType();
		if (SourceInstance != TargetInstance &&
			(SourceInstance == EInstanceType::IT_BabyCave || TargetInstance == EInstanceType::IT_BabyCave))
		{
			NewMemberPlayerController->ClientHandleInviteResult(EInviteResult::GroupRejected_ToMember, SourcePlayerState, TargetPlayerState);
			return;
		}

		if (SourcePlayerState->GetGroupSlotRequirement() != 0 && TargetPlayerState->GetGroupSlotRequirement() != 0)
		{
			bool bLeaderHasValidGroup = IsValid(SourceGroupActor);

			//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::Printf(TEXT("AIGameMode::EnoughSlotsInGroup - bLeaderHasValidGroup: %s"), bLeaderHasValidGroup ? TEXT("true") : TEXT("false")));

			switch (CanJoinGroup(SourceGroupActor, SourcePlayerState, TargetPlayerState, !bLeaderHasValidGroup))
			{
				case EAcceptInviteResult::CharacterTypeMismatch:
				{
					// Notify leader of charactertype mismatch
					LeaderPlayerController->ClientHandleInviteResult(EInviteResult::GroupCharacterTypeMismatch_ToLeader, SourcePlayerState, TargetPlayerState);

					// Notify member of charactertype mismatch
					NewMemberPlayerController->ClientHandleInviteResult(EInviteResult::GroupCharacterTypeMismatch_ToMember, SourcePlayerState, TargetPlayerState);
					break;
				}
				case EAcceptInviteResult::NotEnoughSlots:
				{
					// Notify Leader of group size excess
					LeaderPlayerController->ClientHandleInviteResult(EInviteResult::GroupJoinWouldExceed_ToLeader, SourcePlayerState, TargetPlayerState);

					// Notify Member of group size excess
					NewMemberPlayerController->ClientHandleInviteResult(EInviteResult::GroupJoinWouldExceed_ToMember, SourcePlayerState, TargetPlayerState);
					break;
				}
				case EAcceptInviteResult::Success:
				{
					if (bLeaderHasValidGroup)
					{
						LeaderPlayerController->ServerAddGroupMember(TargetPlayerState);
						//TODO: Determine if this should be automatic.
						#if WITH_VIVOX
						NewMemberPlayerController->JoinGroupChannel();
						#endif
					}
					else
					{
						// Cancel out early as new member or leader are already in a group
						if (bLeaderHasValidGroup || SourceGroupActor)
						{
							LeaderPlayerController->ClientHandleInviteResult(EInviteResult::Invalid, SourcePlayerState, TargetPlayerState);
							NewMemberPlayerController->ClientHandleInviteResult(EInviteResult::Invalid, SourcePlayerState, TargetPlayerState);
							return;
						}

						CreateNewGroup(SourcePlayerState, TargetPlayerState);

						if (SourcePlayerState->GetPlayerGroupActor())
						{
							SendJoinOrLeaveGroupWebhook(SourcePlayerState, SourcePlayerState->GetPlayerGroupActor()->GetGroupLeader(), true);
						}

						#if WITH_VIVOX
						LeaderPlayerController->JoinGroupChannel();
						NewMemberPlayerController->JoinGroupChannel();
						#endif
					}

					SendJoinOrLeaveGroupWebhook(TargetPlayerState, SourcePlayerState, true);

					// Both branches above should result in having a valid Player Group Actor here. Avoids duplicate call
					if (ensureAlways(SourcePlayerState->GetPlayerGroupActor())) 
					{
						AIPlayerGroupActor* const LocalPlayerGroupActor = SourcePlayerState->GetPlayerGroupActor();

						if (!LocalPlayerGroupActor->ValidateAndPropagateGroupMemberQuestAvailabilityStatus())
						{
							LocalPlayerGroupActor->FailAllGroupUserQuests();
						}
					}
					break;
				}
			}
		}
		else
		{
			// Character Slots are not properly initialized
			LeaderPlayerController->ClientHandleInviteResult(EInviteResult::Invalid, SourcePlayerState, TargetPlayerState);
			NewMemberPlayerController->ClientHandleInviteResult(EInviteResult::Invalid, SourcePlayerState, TargetPlayerState);
		}
	}
	else
	{
		RejectGroupInvite(SourcePlayerState, TargetPlayerState);
	}
}

void AIGameMode::SendJoinOrLeaveGroupWebhook(const AIPlayerState* const MovedPlayerState, const AIPlayerState* const GroupLeaderPlayerState, const bool bJoined) const
{
	AIGameSession* const IGameSession = Cast<AIGameSession>(GameSession);

	if (IGameSession == nullptr || !AIGameSession::UseWebHooks(WEBHOOK_PlayerJoinedGroup))
	{
		return;
	}

	const FString MovedPlayerName = MovedPlayerState->GetPlayerName();
	const FString MovedPlayerAlderonId = MovedPlayerState->GetAlderonID().ToDisplayString();
	const FString GroupLeaderName = GroupLeaderPlayerState->GetPlayerName();
	const FString GroupLeaderAlderonId = GroupLeaderPlayerState->GetAlderonID().ToDisplayString();
	const FString GroupID = MovedPlayerState->GetPlayerGroupActor()->GetID().ToString();

	if (MovedPlayerName != "" && GroupLeaderName != "")
	{
		const TMap<FString, TSharedPtr<FJsonValue>> WebHookProperties
		{
			{ TEXT("Player"), MakeShareable(new FJsonValueString(MovedPlayerName)) },
			{ TEXT("PlayerAlderonId"), MakeShareable(new FJsonValueString(MovedPlayerAlderonId)) },
			{ TEXT("Leader"), MakeShareable(new FJsonValueString(GroupLeaderName)) },
			{ TEXT("LeaderAlderonId"), MakeShareable(new FJsonValueString(GroupLeaderAlderonId)) },
			{ TEXT("GroupID"), MakeShareable(new FJsonValueString(GroupID)) },
		};

		IGameSession->TriggerWebHook(bJoined ? WEBHOOK_PlayerJoinedGroup : WEBHOOK_PlayerLeftGroup, WebHookProperties);
	}
}

void AIGameMode::ProcessWaystoneInvite(AIPlayerState* SourcePlayerState, AIPlayerState* TargetPlayerState, bool bAccepted, bool bTimedOut, FName WaystoneTag)
{
	if (!SourcePlayerState) return;
	if (!TargetPlayerState) return;

	if (AIPlayerController* LeaderPlayerController = SourcePlayerState->GetOwner<AIPlayerController>())
	{
		if (AIPlayerController* NewMemberPlayerController = TargetPlayerState->GetOwner<AIPlayerController>())
		{
			if (bAccepted)
			{
				const FWaystoneInvite& OutgoingWaystoneInvite = LeaderPlayerController->GetOutgoingWaystoneInvite();
				if (OutgoingWaystoneInvite.InviteFromPlayerState != TargetPlayerState && OutgoingWaystoneInvite.InviteFromPlayerState != nullptr)
				{
					// This can happen if a player sends an invite to 2 players and they both accept. The 2nd accept will receive this notice
					NewMemberPlayerController->ClientHandleInviteResult(EInviteResult::WaystoneInviteExpired_ToMember, SourcePlayerState, TargetPlayerState);
					return;
				}

				switch (CanAcceptWaystone(SourcePlayerState, TargetPlayerState))
				{
				case EAcceptInviteResult::CharacterTypeMismatch:
				{
					// Notify leader of charactertype mismatch
					LeaderPlayerController->ClientHandleInviteResult(EInviteResult::WaystoneCharacterTypeMismatch_ToLeader, SourcePlayerState, TargetPlayerState);
	
					// Notify member of charactertype mismatch
					NewMemberPlayerController->ClientHandleInviteResult(EInviteResult::WaystoneCharacterTypeMismatch_ToMember, SourcePlayerState, TargetPlayerState);
					break;
				}
				case EAcceptInviteResult::WaystoneInUse:
				{
					// Notify leader of charactertype mismatch
					LeaderPlayerController->ClientHandleInviteResult(EInviteResult::WaystoneInUse_ToLeader, SourcePlayerState, TargetPlayerState);
	
					// Notify member of charactertype mismatch
					NewMemberPlayerController->ClientHandleInviteResult(EInviteResult::WaystoneInUse_ToMember, SourcePlayerState, TargetPlayerState);
					break;
				}
				case EAcceptInviteResult::Success:
				{
					if (bAccepted)
					{
						if (UIGameplayStatics::IsInGameWaystoneAllowed(this))
						{
							const FWaystoneInvite& CurrentWaystoneInvite = NewMemberPlayerController->GetWaystoneInvite();
							if (CurrentWaystoneInvite.WaystoneTag != NAME_None && CurrentWaystoneInvite.InviteFromPlayerState != nullptr)
							{
								// Already have an invite, we need to cancel it.
								if (AIPlayerController* ExistingInviter = Cast<AIPlayerController>(CurrentWaystoneInvite.InviteFromPlayerState->GetPlayerController()))
								{
									// Ensure their invite is actually this client
									if (ExistingInviter->GetOutgoingWaystoneInvite().WaystoneTag != NAME_None && ExistingInviter->GetOutgoingWaystoneInvite().InviteFromPlayerState == TargetPlayerState)
									{
										ExistingInviter->ServerCancelOutgoingWaystoneInvite_Implementation();
									}
								}
							}

							if (LeaderPlayerController->GetOutgoingWaystoneInvite().WaystoneTag != NAME_None)
							{
								// Already have an outstanding Waystone Invite. This should show a prompt to cancel it on the client.
								return;
							}

							LeaderPlayerController->SetOutgoingWaystoneInvite(FWaystoneInvite(TargetPlayerState, WaystoneTag, TargetPlayerState->GetCharacterType()));
						}

						FTimerDelegate Del = FTimerDelegate::CreateUObject(LeaderPlayerController, &AIPlayerController::ClearOutgoingWaystoneInvite);
						GetWorldTimerManager().SetTimer(LeaderPlayerController->TimerHandle_WaystoneOutgoingInviteTimeout, Del, 120.0f, false);

						LeaderPlayerController->ClientHandleInviteResult(EInviteResult::WaystoneAccepted_ToMembers, SourcePlayerState, TargetPlayerState);

						NewMemberPlayerController->SetWaystoneInvite(FWaystoneInvite(SourcePlayerState, WaystoneTag, SourcePlayerState->GetCharacterType()));
						NewMemberPlayerController->ApplyWaystoneInviteEffect(false);
						NewMemberPlayerController->ClientHandleInviteResult(EInviteResult::WaystoneAccepted_ToMember, SourcePlayerState, TargetPlayerState);

					}
	
					break;
				}
				}
			}
			else if (bTimedOut)
			{
				//LeaderPlayerController->ClientHandleInviteResult(EInviteResult::WaystoneInviteExpired_ToMember, SourcePlayerState, TargetPlayerState);
				NewMemberPlayerController->ClientHandleInviteResult(EInviteResult::WaystoneInviteExpired_ToMember, SourcePlayerState, TargetPlayerState);
			}
			else
			{
				RejectWaystoneInvite(SourcePlayerState, TargetPlayerState);
			}
		}
	}
}

void AIGameMode::RejectWaystoneInvite(AIPlayerState* SourcePlayerState, AIPlayerState* TargetPlayerState)
{
	if (AIPlayerController* LeaderPlayerController = SourcePlayerState->GetOwner<AIPlayerController>())
	{
		LeaderPlayerController->ClientHandleInviteResult(EInviteResult::WaystoneRejected_ToLeader, SourcePlayerState, TargetPlayerState);
	}

	if (AIPlayerController* NewMemberPlayerController = TargetPlayerState->GetOwner<AIPlayerController>())
	{
		NewMemberPlayerController->ClientHandleInviteResult(EInviteResult::WaystoneRejected_ToMember, SourcePlayerState, TargetPlayerState);
	}
}

void AIGameMode::RejectGroupInvite(AIPlayerState* SourcePlayerState, AIPlayerState* TargetPlayerState)
{
	if (AIPlayerController* LeaderPlayerController = SourcePlayerState->GetOwner<AIPlayerController>())
	{
		LeaderPlayerController->ClientHandleInviteResult(EInviteResult::GroupRejected_ToLeader, SourcePlayerState, TargetPlayerState);
	}

	if (AIPlayerController* NewMemberPlayerController = TargetPlayerState->GetOwner<AIPlayerController>())
	{
		NewMemberPlayerController->ClientHandleInviteResult(EInviteResult::GroupRejected_ToMember, SourcePlayerState, TargetPlayerState);
	}
}

void AIGameMode::CreateNewGroup(AIPlayerState* SourcePlayerState, AIPlayerState* TargetPlayerState)
{
	//GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Red, FString::Printf(TEXT("AIGameMode::CreateNewGroup()")));

	const AIGameState* const IGameState = UIGameplayStatics::GetIGameState(this);
	if (IGameState && IGameState->GetGameStateFlags().bDisableGrouping)
	{
		return;
	}

	if (SourcePlayerState && TargetPlayerState)
	{
		AIPlayerGroupActor* NewPlayerGroup = GetWorld()->SpawnActorDeferred<AIPlayerGroupActor>(PlayerGroupActor, FTransform(FRotator(0.0f, 0.0f, 0.0f), FVector(0.0f, 0.0f, 0.0f), FVector(1.0f, 1.0f, 1.0f)), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		check(NewPlayerGroup);

		NewPlayerGroup->GroupCharacterType = SourcePlayerState->GetCharacterType();
		NewPlayerGroup->AddGroupMember(SourcePlayerState);
		NewPlayerGroup->AddGroupMember(TargetPlayerState);
		NewPlayerGroup->UpdateGroupSlotsFilled();
		NewPlayerGroup->UpdateGroupLeader(SourcePlayerState);

		UGameplayStatics::FinishSpawningActor(NewPlayerGroup, FTransform(FRotator(0.0f, 0.0f, 0.0f), FVector(0.0f, 0.0f, 0.0f), FVector(1.0f, 1.0f, 1.0f)));

		SourcePlayerState->SetPlayerGroupActor(NewPlayerGroup);
		TargetPlayerState->SetPlayerGroupActor(NewPlayerGroup);
		SourcePlayerState->SetIsSearchingForParty(false);
		TargetPlayerState->SetIsSearchingForParty(false);

		if (AIPlayerController* SourceController = SourcePlayerState->GetOwner<AIPlayerController>())
		{
			SourceController->UpdateGroupMemberLocations();
			SourceController->InitializeGroupMapMarkers();
			SourceController->UpdateGroupEffect(true);

			// Notify Leader of Group Creation
			SourceController->ClientHandleInviteResult(EInviteResult::GroupCreated_ToLeader, SourcePlayerState, TargetPlayerState);
		}

		if (AIPlayerController* TargetController = TargetPlayerState->GetOwner<AIPlayerController>())
		{
			TargetController->UpdateGroupMemberLocations();
			TargetController->InitializeGroupMapMarkers();
			TargetController->UpdateGroupEffect(true);

			// Notify Member of Group Join/Creation
			TargetController->ClientHandleInviteResult(EInviteResult::GroupCreated_ToMember, SourcePlayerState, TargetPlayerState);
		}
	}
}

void AIGameMode::DisbandGroup(AIPlayerState* SourcePlayerState, AIPlayerGroupActor* CurrentPlayerGroupActor)
{
	if (CurrentPlayerGroupActor)
	{
		if (CurrentPlayerGroupActor->GetGroupLeader())
		{
			// If the instance is a homecave then we can remove all but the owner
			if (AIPlayerCaveMain* IPlayerCave = Cast<AIPlayerCaveMain>(CurrentPlayerGroupActor->GetGroupLeader()->GetCachedCurrentInstance()))
			{
				IPlayerCave->RemoveAllButOwner();
			}
		}

		CurrentPlayerGroupActor->UpdateMembersGroupQuestAndBuff(true);

		const TArray<AIPlayerState*>& GroupMembers = CurrentPlayerGroupActor->GetGroupMembers();
		for (int32 i = 0; i < GroupMembers.Num(); i++)
		{
			if (AIPlayerState* RemotePlayerState = Cast<AIPlayerState>(GroupMembers[i]))
			{
				CurrentPlayerGroupActor->LeavingGroupMembers.Add(RemotePlayerState);
				if (AIPlayerController* RemotePlayerController = RemotePlayerState->GetOwner<AIPlayerController>())
				{
					if (AIGameMode* const IGameMode = GetWorld()->GetAuthGameMode<AIGameMode>())
					{
						IGameMode->SendJoinOrLeaveGroupWebhook(RemotePlayerState, CurrentPlayerGroupActor->GetGroupLeader(), false);
					}

					RemotePlayerController->UpdateGroupEffect(false);
					RemotePlayerController->ServerGroupDisbanded();
					RemotePlayerState->SetPlayerGroupActor(nullptr);
				}
			}
		}
		
		CurrentPlayerGroupActor->EmptyMembers();
		CurrentPlayerGroupActor->ReleaseGroupColor();
		CurrentPlayerGroupActor->Destroy(true);
	}
}

void AIGameMode::AsyncSpawnInstancedTile(AActor* InOwner, FPrimaryAssetId TileId, FInstancedTileSpawned Delegate)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(TEXT("AIGameMode::AsyncSpawnInstancedTile"))

	PERF_SCOPE_TIMER();

	if (!Delegate.IsBound())
	{
		UE_LOG(TitansLog, Error, TEXT("AIGameMode::AsyncSpawnInstancedTile: Delegate is not bound, skipping load."));
		return;
	}

	UTitanAssetManager& AssetManager = UTitanAssetManager::Get();

	TileId = AssetManager.GetRedirectForAsset(TileId);

	if (!TileId.IsValid())
	{
		UE_LOG(TitansLog, Error, TEXT("AIGameMode::AsyncSpawnInstancedTile: TileId %s is invalid."), *TileId.ToString());
		Delegate.ExecuteIfBound(FInstancedTile());
		return;
	}

	UInstancedTileDataAsset* TileDataAsset = Cast<UInstancedTileDataAsset>(AssetManager.GetPrimaryAssetObject(TileId));
	if (TileDataAsset)
	{
		UE_LOG(TitansLog, Log, TEXT("AIGameMode:AsyncSpawnInstancedTile: Data loaded, skipping load %s"), *TileId.ToString());

		SpawnInstancedTile(InOwner, TileDataAsset, TileId, Delegate);
		return;
	}

	// If it's not loaded, call this func again once it's done
	FStreamableDelegate Del = FStreamableDelegate::CreateUObject(this, &AIGameMode::AsyncSpawnInstancedTile, InOwner, TileId, Delegate);
	AssetManager.LoadPrimaryAsset(TileId, {}, Del, FStreamableManager::AsyncLoadHighPriority);
}

TSharedPtr<FStreamableHandle> AIGameMode::SpawnInstancedTile(AActor* InOwner, const class UInstancedTileDataAsset* TileAsset, FPrimaryAssetId TileId, FInstancedTileSpawned Delegate)
{
	if (TileAsset == nullptr || (TileAsset->InstancedTileClass.IsNull() /* && TileAsset->LevelName == NAME_None*/))
	{
		Delegate.ExecuteIfBound(FInstancedTile());
		return nullptr;
	}

	const TSoftClassPtr<AActor> InstancedTileClassSoft = TileAsset->InstancedTileClass;

	FStreamableDelegate PostClassLoad = FStreamableDelegate::CreateLambda([this, InstancedTileClassSoft, Delegate, InOwner, TileAsset, TileId]
	{
		UClass* TileClass = InstancedTileClassSoft.Get();
		if (!TileClass)
		{
			UE_LOG(TitansLog, Log, TEXT("AIGameMode:SpawnInstancedTile: TILE FAILED TO LOAD: %s"), *InstancedTileClassSoft.ToString());
			Delegate.ExecuteIfBound(FInstancedTile());
		}


		AIPlayerCaveBase* TileActor = Cast<AIPlayerCaveBase>(UGameplayStatics::BeginDeferredActorSpawnFromClass(
			this, TileClass, FTransform(), ESpawnActorCollisionHandlingMethod::AlwaysSpawn, InOwner == nullptr ? this : InOwner));

		FTransform SpawnTransform = TileActor->ReserveSafeInstanceSpawnLocation();

		if (AIGameState* IGS = GetGameState<AIGameState>())
		{
			UE_LOG(TitansLog, Log, TEXT("AIGameMode:SpawnInstancedTile: TILE FINISHED SPAWNING: %s"), *TileActor->GetName());
			int32 Index = IGS->InstancedTiles.Add(FInstancedTile(TileActor, SpawnTransform.GetLocation(), TileAsset, TileId));
			UGameplayStatics::FinishSpawningActor(TileActor, SpawnTransform);
			TileActor->Tile = IGS->InstancedTiles[Index];

			Delegate.ExecuteIfBound(IGS->InstancedTiles[Index]);
		}
		else
		{
			Delegate.ExecuteIfBound(FInstancedTile());
		}

	});


	if (!InstancedTileClassSoft.Get())
	{
		// Start Async Loading
		FStreamableManager& Streamable = UIGameplayStatics::GetStreamableManager(this);
		return Streamable.RequestAsyncLoad(
			InstancedTileClassSoft.ToSoftObjectPath(),
			PostClassLoad,
			FStreamableManager::AsyncLoadHighPriority, false
		);
	}
	else
	{
		PostClassLoad.Execute();
		return nullptr;
	}

	
}

FTransform AIGameMode::GetTileSpawnTransform() const
{
	//For now we only spawn from one pivot location. This might suffice.

	static FVector StartTileSpawn = FVector(0, 0, 50000.f);

	static int32 TileCount = 0;

	return FTransform(StartTileSpawn + FVector(0.f, -50000.f * TileCount++, 0));
}

FPrimaryAssetId AIGameMode::GetHatchlingCaveForCharacter_Implementation(AIBaseCharacter* IBaseCharacter)
{
	check(IBaseCharacter);
	if (!IBaseCharacter)
	{
		return FPrimaryAssetId(); 
	} 

	AIDinosaurCharacter* IDinosaurCharacter = Cast<AIDinosaurCharacter>(IBaseCharacter);
	bool IsAquatic = IDinosaurCharacter && IDinosaurCharacter->Tags.Contains("Aquatic");
	bool HasFlying = IDinosaurCharacter && IDinosaurCharacter->HasFlying();

	switch (IBaseCharacter->DietRequirements)
	{
	case EDietaryRequirements::CARNIVORE:
		return (IsAquatic && !HasFlying) ? CarnivoreAquaticHatchlingCave : CarnivoreHatchlingCave;
	case EDietaryRequirements::HERBIVORE:
		return HerbivoreHatchlingCave;
	default:
		return FPrimaryAssetId();
	break;
	}

	return FPrimaryAssetId();
}

bool AIGameMode::IsWebServerEnabled()
{
	bool bEnabled;
	GConfig->GetBool(TEXT("WebServer"), TEXT("bEnabled"), bEnabled, GGameIni);
	return bEnabled;
}

FString AIGameMode::GetWebServerPassword()
{
	FString Password = TEXT("");
	GConfig->GetString(TEXT("WebServer"), TEXT("Password"), Password, GGameIni);
	FParse::Value(FCommandLine::Get(), TEXT("WebServerPassword="), Password);
	return Password;
}

int AIGameMode::GetWebServerPort()
{
	int Port = 8080;
	GConfig->GetInt(TEXT("WebServer"), TEXT("Port"), Port, GGameIni);
	FParse::Value(FCommandLine::Get(), TEXT("WebServerPort="), Port);
	return Port;
}

bool AIGameMode::IsWebServerConnectionValidated(UConnection* Connection)
{
	if (!Connection) return false;

	if (Connection->GetCOOKIEVar(TEXT("Token")) == WebServerGeneratedToken) return true;

	return false;
}

void AIGameMode::HandleWebServerConnection(UConnection* Connection)
{
	if (WebServerGeneratedToken.IsEmpty())
	{
		WebServerGeneratedToken = FString::Printf(TEXT("%i-%i-%i"), FMath::Rand() % 10000, FMath::Rand() % 10000, FMath::Rand() % 10000);
	}

	check (Connection);
	if (!Connection) return;
	UE_LOG(TitansLog, Log, TEXT("Webserver Connection! Method: %s, Path: %s, Cookie: %s, Query = %s"), *Connection->GetUriMethod(), *Connection->GetUriPath(), *Connection->GetHeader(TEXT("Cookie")), *Connection->GetQueryString());

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	FString DocRoot = FPaths::ProjectContentDir().Append(WebServerDocumentRoot);
	FString WebFile;
	UResponse* Response = UResponse::ConstructResponse(this);

	// If no path is specified, use home
	FString UriPath = Connection->GetUriPath();
	if (UriPath.IsEmpty() || UriPath == TEXT("/"))
	{
		UriPath = TEXT("/home.html");
	}

	bool bRequestingLoginPage = false;

	if (UriPath == TEXT("/login.html"))
	{
		bRequestingLoginPage = true;
		Response->AddResponseCookie(UCookie::ConstructCookieExt(this, TEXT("Token"), TEXT("")));
	}

	// validate password
	FString PasswordAttempt = Connection->GetDataValue(TEXT("pswd"));
	UE_LOG(TitansLog, Log, TEXT("PasswordAttempt: %s"), *PasswordAttempt);
	FString Password = GetWebServerPassword();
	bool bCorrectPassword = false;
	if (PasswordAttempt == Password)
	{
		Response->AddResponseCookie(UCookie::ConstructCookieExt(this, TEXT("Token"), WebServerGeneratedToken));
		bCorrectPassword = true;
	}

	if (IsWebServerConnectionValidated(Connection) || bRequestingLoginPage || bCorrectPassword)
	{
		// Get path to file to send
		WebFile = FPaths::Combine(*DocRoot, *Connection->GetUriPath());
		EHttpStatusCode StatusCode = EHttpStatusCode::VE_OK;

		if (!PlatformFile.FileExists(*WebFile))
		{
			UE_LOG(TitansLog, Log, TEXT("AIGameMode::HandleWebServerConnection: File doesnt exist, %s"), *WebFile);
			FString File404 = FPaths::Combine(*DocRoot, TEXT("404.html"));
			if (PlatformFile.FileExists(*File404))
			{
				WebFile = File404;
				StatusCode = EHttpStatusCode::VE_NotFound;
			}
			else
			{
				UE_LOG(TitansLog, Log, TEXT("AIGameMode::HandleWebServerConnection: Could not find 404 File, %s"), *File404);
				return;
			}
		}

		TArray<uint8> FileData;
		FFileHelper::LoadFileToArray(FileData, *WebFile);
		Response->SetResponseData(FileData);
		Response->SetResponseContentType(EMediaType::TEXT_HTML);
		Response->SetResponseStatusCode(StatusCode);

	}
	else
	{
		// Redirect to login page
		Response->SetResponseStatusCode(EHttpStatusCode::VE_TemporaryRedirect);
		Response->SetResponseRedirection(TEXT("/login.html"));
	}


	TArray<uint8> ResponseData = Response->GetResponseData();
	Connection->SendRawResponseBytes(ResponseData.GetData(), ResponseData.Num());

}

void AIGameMode::OnPlayerReady_Implementation(AIPlayerController* ReadyPlayer)
{
	ReadyPlayer->ClientShowCharacterMenu();
}

bool AIGameMode::OnCharacterTakeDamage_Implementation(AIBaseCharacter* Target, AIBaseCharacter* Source, EDamageType DamageType, float Damage, TMap<EDamageEffectType, float>& OtherDamage)
{
	// Player Damaged Webhook, should only trigger if the player is damaged by another player
	if (AIGameSession::UseWebHooks(WEBHOOK_PlayerDamagedPlayer) && IsValid(Target) && IsValid(Source))
	{
		AIGameSession* const IGameSession = Cast<AIGameSession>(GameSession);
		if (IGameSession)
		{
			FString TargetName, TargetAlderonId, TargetDinosaurType, TargetRoleName = "";
			FString SourceName, SourceAlderonId, SourceDinosaurType, SourceRoleName = "";
			bool bSourceIsAdmin = false;
			bool bTargetIsAdmin = false;
			float TargetGrowth = 1.0f;
			float SourceGrowth = 1.0f;


			AIPlayerState* const ITargetPlayerState = Cast<AIPlayerState>(Target->GetPlayerState());
			if (ITargetPlayerState)
			{
				TargetName = ITargetPlayerState->GetPlayerName();
				TargetAlderonId = ITargetPlayerState->GetAlderonID().ToDisplayString();
				TargetRoleName = ITargetPlayerState->GetPlayerRole().bAssigned ? ITargetPlayerState->GetPlayerRole().Name : TEXT("");
				
				bTargetIsAdmin = AIChatCommandManager::Get(this)->CheckAdmin(Cast<AAlderonPlayerController>(Target->GetController()));
				
				TargetDinosaurType = ITargetPlayerState->GetCharacterSpecies().ToString();
				TargetGrowth = Target->GetGrowthPercent();
			}

			AIPlayerState* const ISourcePlayerState = Cast<AIPlayerState>(Source->GetPlayerState());
			if (ISourcePlayerState)
			{
				SourceName = ISourcePlayerState->GetPlayerName();
				SourceAlderonId = ISourcePlayerState->GetAlderonID().ToDisplayString();
				SourceRoleName = ISourcePlayerState->GetPlayerRole().bAssigned ? ISourcePlayerState->GetPlayerRole().Name : TEXT("");

				bSourceIsAdmin = AIChatCommandManager::Get(this)->CheckAdmin(Cast<AAlderonPlayerController>(Source->GetController()));
				
				SourceDinosaurType = ISourcePlayerState->GetCharacterSpecies().ToString();
				SourceGrowth = Source->GetGrowthPercent();
			}
			
			FText DamageTypeName{};
			//Finds the localized display name or native display name as a fallback.
			UEnum::GetDisplayValueAsText(DamageType, DamageTypeName);

			if (TargetName != "" && TargetAlderonId != "")
			{
				const TMap<FString, TSharedPtr<FJsonValue>> WebHookProperties
				{
					{ TEXT("SourceName"), MakeShareable(new FJsonValueString(SourceName)) },
					{ TEXT("SourceAlderonId"), MakeShareable(new FJsonValueString(SourceAlderonId)) },
					{ TEXT("SourceDinosaurType"), MakeShareable(new FJsonValueString(SourceDinosaurType)) },
					{ TEXT("SourceRole"), MakeShareable(new FJsonValueString(SourceRoleName)) },
					{ TEXT("SourceIsAdmin"), MakeShareable(new FJsonValueBoolean(bSourceIsAdmin)) },
					{ TEXT("SourceGrowth"), MakeShareable(new FJsonValueNumber(SourceGrowth)) },

					//Finds the localized display name or native display name as a fallback.
					{ TEXT("DamageType"), MakeShareable(new FJsonValueString(DamageTypeName.ToString())) },
					{ TEXT("DamageAmount"), MakeShareable(new FJsonValueNumber(Damage)) },

					{ TEXT("TargetName"), MakeShareable(new FJsonValueString(TargetName)) },
					{ TEXT("TargetAlderonId"), MakeShareable(new FJsonValueString(TargetAlderonId)) },
					{ TEXT("TargetDinosaurType"), MakeShareable(new FJsonValueString(TargetDinosaurType)) },
					{ TEXT("TargetRole"), MakeShareable(new FJsonValueString(TargetRoleName)) },
					{ TEXT("TargetIsAdmin"), MakeShareable(new FJsonValueBoolean(bTargetIsAdmin)) },
					{ TEXT("TargetGrowth"), MakeShareable(new FJsonValueNumber(TargetGrowth)) },
				};

				IGameSession->TriggerWebHook(WEBHOOK_PlayerDamagedPlayer, WebHookProperties);
			}
		}
	}
	
	return true;
}

void AIGameMode::OnPlayerSpawn_Implementation(AIPlayerController* Player, AIBaseCharacter* Character, bool bOverrideSpawn, const FTransform& Transform)
{
}

void AIGameMode::OnPlayerSpawnInstance_Implementation(AIPlayerController* Player, AIBaseCharacter* Character, bool bOverrideSpawn, const FTransform& Transform, AIPlayerCaveBase* Instance)
{
}

void AIGameMode::OnPlayerSpawnWaypoint_Implementation(AIPlayerController* Player, const FAlderonUID& CharacterID, const FTransform& Transform, const FWaystoneInvite& WaystoneInvite)
{
}

bool AIGameMode::OnPlayerTrySpawn_Implementation(AIPlayerController* Player, FAlderonUID CharacterUID, bool bOverrideSpawn, FTransform OverrideTransform, const FAsyncCharacterSpawned& OnSpawn)
{
	return true;
}

void AIGameMode::RestartGame()
{
	AIGameState* IGameState = Cast<AIGameState>(GetWorld()->GetGameState());
	for (APlayerState* PlayerState : IGameState->PlayerArray)
	{
		if (AIPlayerController* IPlayerController = PlayerState->GetOwner<AIPlayerController>())
		{
			IPlayerController->bIsTravelling = true;
		}
	}

	// travel
	Super::RestartGame();
}

void AIGameMode::GenericPlayerInitialization(AController* C)
{
	Super::GenericPlayerInitialization(C);

	if (AIPlayerController* IPlayerController = Cast<AIPlayerController>(C))
	{
		if (IPlayerController->bIsTravelling)
		{
			IPlayerController->RefreshLogin();
		}
	}
}

void AIGameMode::SetServerAutoRecord(bool bAutoRecord)
{
	bHandleDedicatedServerReplays = bAutoRecord;
	SaveConfig();
}

bool AIGameMode::IsHandlingReplays()
{
	// If we're running in PIE, don't record server demos
	if (GetWorld() != nullptr && GetWorld()->IsPlayInEditor())
	{
		return false;
	}

	return bWasHandlingDedicatedServerReplays && GetNetMode() == ENetMode::NM_DedicatedServer;
}

void AIGameMode::RemoveCreatorModeObjects(const FString& SaveName, FAsyncChatCommandCallback Callback)
{
	check(DatabaseEngine);
	if (!DatabaseEngine) return;

	if (bCreatorModePendingSave)
	{
		Callback.ExecuteIfBound(FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdCreatorModePendingSave")));
		return;
	}
	bCreatorModePendingSave = true;

	FDatabaseLoadCompleted OnCompleted;
	OnCompleted.BindUObject(this, &ThisClass::RemoveCreatorModeObjects_Stage2, SaveName, Callback);
	DatabaseEngine->Load(GetCreatorModeSavePath() / TEXT("SaveList"), OnCompleted);
}

void AIGameMode::RemoveCreatorModeObjects_Stage2(const FDatabaseLoad& Data, FString SaveName, FAsyncChatCommandCallback Callback)
{
	check(DatabaseEngine);
	if (!DatabaseEngine) return;

	TWeakObjectPtr<AIGameMode> ThisPtr = this;

	TSharedPtr<FJsonObject> SaveList;
	if (Data.bSuccess)
	{
		SaveList = Data.JsonObject;
	}
	else
	{
		SaveList = MakeShared<FJsonObject>();
	}

	TArray<TSharedPtr<FJsonValue>> Saves;
	const TArray<TSharedPtr<FJsonValue>>* TempSaves;
	if (SaveList->TryGetArrayField(TEXT("SaveList"), TempSaves))
	{
		Saves = *TempSaves;
	}
	for (int Index = Saves.Num() - 1; Index >= 0; Index--)
	{
		TSharedPtr<FJsonValue>& Save = Saves[Index];

		if (!Save.Get()) continue;

		FString SaveString;
		if (!Save->TryGetString(SaveString)) continue;

		if (SaveString != SaveName) continue;

		Saves.RemoveAt(Index);
	}
	SaveList->SetArrayField(TEXT("SaveList"), Saves);

	// First delete existing save data, then save the new data. 
	// We must delete the existing data first so that any old creator mode objects
	// that dont exist now don't remain.
	FDatabaseOperationCompleted OnDeleteComplete;
	OnDeleteComplete.BindLambda([=](FDatabaseOperationData Data)
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("SaveName"), FText::FromString(SaveName));
		Callback.ExecuteIfBound(FText::Format(FText::FromStringTable(TEXT("ST_ChatCommands"), Data.bSuccess ? TEXT("CmdRemoveCreatorSucceeded") : TEXT("CmdRemoveCreatorFailed")), Args));

		if (ThisPtr.Get())
		{
			ThisPtr->bCreatorModePendingSave = false;
		}
	});
	DatabaseEngine->Delete(GetCreatorModeSavePath() / SaveName / TEXT(""), OnDeleteComplete);
	SaveCreatorModeSaveList(SaveList);
}

void AIGameMode::SaveCreatorModeObjects(const FString& SaveName, FAsyncChatCommandCallback Callback)
{
	check(DatabaseEngine);
	if (!DatabaseEngine) return;

	if (bCreatorModePendingSave)
	{
		Callback.ExecuteIfBound(FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdCreatorModePendingSave")));
		return;
	}
	bCreatorModePendingSave = true;

	FDatabaseLoadCompleted OnCompleted;
	OnCompleted.BindUObject(this, &ThisClass::SaveCreatorModeObjects_Stage2, SaveName, Callback);
	DatabaseEngine->Load(GetCreatorModeSavePath() / TEXT("SaveList"), OnCompleted);
}

void AIGameMode::SaveCreatorModeSaveList(TSharedPtr<FJsonObject> SaveList)
{
	TWeakObjectPtr<AIGameMode> ThisPtr = this;
	FDatabaseOperationCompleted OnDeleteComplete;
	OnDeleteComplete.BindLambda([ThisPtr, this, SaveList](FDatabaseOperationData Data)
	{
		DatabaseEngine->Save(SaveList, GetCreatorModeSavePath() / TEXT("SaveList"), FDatabaseOperationCompleted());

		if (ThisPtr.Get())
		{
			ThisPtr->bCreatorModePendingSave = false;
		}

	});
	DatabaseEngine->Delete(GetCreatorModeSavePath() / TEXT("SaveList"), OnDeleteComplete);
}

void AIGameMode::AddLoginDebuffToCharacter(const AIBaseCharacter& IBaseCharacter, bool bServerAntiRevengeKill, bool bWasPreviouslyAdminCharacter)
{
	UPOTAbilitySystemGlobals& PASG = UPOTAbilitySystemGlobals::Get();
	const bool bShouldApplyLoginDebuffToChar = PASG.LoginEffect &&
											bServerAntiRevengeKill &&
											!bWasPreviouslyAdminCharacter
											&& IBaseCharacter.GetGrowthPercent() >= 0.75f
											&& !IBaseCharacter.bRespawn;
	if (!bShouldApplyLoginDebuffToChar)
	{
		return;
	}
	IBaseCharacter.AbilitySystem->BP_ApplyGameplayEffectToSelf(PASG.LoginEffect, 1.f, IBaseCharacter.AbilitySystem->MakeEffectContext());
}

void AIGameMode::SaveCreatorModeObjects_Stage2(const FDatabaseLoad& LoadData, FString SaveName, FAsyncChatCommandCallback Callback)
{
	check(DatabaseEngine);
	if (!DatabaseEngine) return;

	TWeakObjectPtr<AIGameMode> ThisPtr = this;
	
	TSharedPtr<FJsonObject> SaveList;
	if (LoadData.bSuccess)
	{
		SaveList = LoadData.JsonObject;
	}
	else
	{
		SaveList = MakeShared<FJsonObject>();
	}

	bool bContainsSave = false;
	TArray<TSharedPtr<FJsonValue>> Saves;
	const TArray<TSharedPtr<FJsonValue>>* TempSaves;
	if (SaveList->TryGetArrayField(TEXT("SaveList"), TempSaves))
	{
		Saves = *TempSaves;
	}
	for (TSharedPtr<FJsonValue> Save : Saves)
	{
		if (!Save.Get()) continue;
		
		FString SaveString;
		if (!Save->TryGetString(SaveString)) continue;
		
		if (SaveString != SaveName) continue;
		
		bContainsSave = true;
		break;		
	}
	if (!bContainsSave)
	{
		if (Saves.Num() >= MaxCreatorSaves)
		{
			Callback.ExecuteIfBound(FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdCreatorObjectsSaveSlotsExceeded")));
			bCreatorModePendingSave = false;
			return;
		}

		auto NewSave = MakeShared<FJsonValueString>(SaveName);
		Saves.Push(NewSave);
		SaveList->SetArrayField(TEXT("SaveList"), Saves);
	}

	// First delete existing save data, then save the new data. 
	// We must delete the existing data first so that any old creator mode objects
	// that dont exist now don't remain.
	FDatabaseOperationCompleted OnDeleteComplete;
	OnDeleteComplete.BindLambda([this, ThisPtr, Callback, SaveName, SaveList, LoadData](FDatabaseOperationData Data)
	{
		TArray<FDatabaseBunchEntry> Entries;
		if (!ThisPtr.Get()) return;
		ThisPtr->GetDirtyCreatorModeObjects(Entries, false);

		if (Entries.Num() > 0)
		{
			FDatabaseOperationCompleted OnSaveComplete = FDatabaseOperationCompleted::CreateLambda([=](const FDatabaseOperationData& Data)
			{
				FFormatNamedArguments Args;
				Args.Add(TEXT("SaveName"), FText::FromString(SaveName));
				Callback.ExecuteIfBound(FText::Format(FText::FromStringTable(TEXT("ST_ChatCommands"), Data.bSuccess ? TEXT("CmdSaveCreatorSucceeded") : TEXT("CmdSaveCreatorFailed")), Args));
			});
			DatabaseEngine->SaveBunch(Entries, GetCreatorModeSavePath() / SaveName / TEXT(""), OnSaveComplete);
			SaveCreatorModeSaveList(SaveList);

		}
		else // nothing to save so we can delete the save slot. Save is identical to calling /ResetCreatorMode
		{
			FFormatNamedArguments Args;
			Args.Add(TEXT("SaveName"), FText::FromString(SaveName));
			Callback.ExecuteIfBound(FText::Format(FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdSaveCreatorNothingToSave")), Args));
			ThisPtr->RemoveCreatorModeObjects_Stage2(LoadData, SaveName, FAsyncChatCommandCallback()); // Call with empty chat command callback to remove this save from the save list.
		}
	});
	DatabaseEngine->Delete(GetCreatorModeSavePath() / SaveName / TEXT(""), OnDeleteComplete);

}

void AIGameMode::GetDirtyCreatorModeObjects(TArray<FDatabaseBunchEntry>& Entries, bool bGetAll /* = false */)
{
	Entries.Empty();
	for (UICreatorModeObjectComponent* CreatorModeComponent : UIGameplayStatics::GetIGameInstance(this)->AllCreatorModeComponents)
	{
		check(CreatorModeComponent);
		if (!CreatorModeComponent) continue;

		if (!CreatorModeComponent->bShouldBeSaved) continue;

		check(CreatorModeComponent->GetOwner());
		if (!CreatorModeComponent->GetOwner()) continue;

		// Don't save creator mode components that were in the original map and haven't been marked dirty. 
		// All player-placed creator objects need to be saved every time.
		// If bSaveAllCreatorModeObjects = true, then save everything.
		// if bGetAll, save everything
		if ((!CreatorModeComponent->bIsDirty && CreatorModeComponent->WasInOriginalMap()) && !bSaveAllCreatorModeObjects && !bGetAll) continue;

		CreatorModeComponent->PrepareForSave();

		Entries.Add({ CreatorModeComponent->UniqueIdentifier, IAlderonDatabase::SerializeObject(CreatorModeComponent->GetOwner(), true) });
	}

	UE_LOG(TitansLog, Log, TEXT("AIGameMode::GetDirtyCreatorModeObjects: Gathered %i Creator Mode Objects for saving."), Entries.Num());
}


void AIGameMode::ListCreatorModeSaves(FAsyncChatCommandCallback Callback)
{
	check(DatabaseEngine);
	if (!DatabaseEngine) return;

	FDatabaseLoadCompleted OnCompleted;
	OnCompleted.BindUObject(this, &ThisClass::ListCreatorModeSaves_Stage2, Callback);
	DatabaseEngine->Load(GetCreatorModeSavePath() / TEXT("SaveList"), OnCompleted);
}

void AIGameMode::ListCreatorModeSaves_Stage2(const FDatabaseLoad& Data, FAsyncChatCommandCallback Callback)
{
	check(DatabaseEngine);
	if (!DatabaseEngine) return;

	TSharedPtr<FJsonObject> SaveList;
	if (Data.bSuccess)
	{
		SaveList = Data.JsonObject;
	}
	else
	{
		SaveList = MakeShared<FJsonObject>();
	}

	TArray<FString> SaveNames{};
	SaveList->TryGetStringArrayField(TEXT("SaveList"), SaveNames);

	if (SaveNames.IsEmpty())
	{
		const FFormatNamedArguments Args {
			{ TEXT("Max"), MaxCreatorSaves }
		};
		Callback.ExecuteIfBound(FText::Format(FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdListCreatorSavedResultEmpty")), Args));
		return;
	}

	FString Reply = TEXT("");
	Reply.Reserve(128);

	const FFormatNamedArguments Args {
		{ TEXT("Num"), SaveNames.Num() },
		{ TEXT("Max"), MaxCreatorSaves }
	};
	Reply.Append(FText::Format(FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdListCreatorSavedResult")), Args).ToString());

	for (const FString& SaveName : SaveNames)
	{
		Reply.Append(FString::Printf(TEXT("\n%s"), *SaveName));
	}

	Callback.ExecuteIfBound(FText::FromString(Reply));
}

void AIGameMode::LoadCreatorModeObjects(const FString& SaveName, FAsyncChatCommandCallback Callback)
{
	check(DatabaseEngine);
	if (!DatabaseEngine) return;

	if (bCreatorModePendingSave)
	{
		Callback.ExecuteIfBound(FText::FromStringTable(TEXT("ST_ChatCommands"), TEXT("CmdCreatorModePendingSave")));
		return;
	}
	bCreatorModePendingSave = true;

	TWeakObjectPtr<AIGameMode> ThisPtr = this;

	FDatabaseLoadBunchCompleted OnComplete;
	OnComplete.BindLambda([this, ThisPtr, SaveName, Callback](FDatabaseLoadBunch Bunch)
	{
		START_PERF_TIME();

		// Resolves a crash when the game server is shutting down and creator mode objects is loading
		UIGameInstance* IGameInstance = UIGameplayStatics::GetIGameInstance(this);
		if (!IGameInstance) return;

		if (!ThisPtr.IsValid()) return;

		TArray<UICreatorModeObjectComponent*> CreatorModeComponentsToRemove = IGameInstance->AllCreatorModeComponents;

		int ObjectsModified = 0;
		int ObjectsSpawned = 0;
		int ObjectsRemoved = 0;

		if (Bunch.bSuccess)
		{
			for (FDatabaseBunchEntry Entry : Bunch.Entries)
			{
				TSharedPtr<FJsonObject>& JsonObject = Entry.JsonObject;
				check (JsonObject.Get());
				if (!JsonObject.Get()) continue;

				TSharedPtr<FJsonObject> CreatorModeComponentJson = JsonObject->GetObjectField(UICreatorModeObjectComponent::StaticClass()->GetName());
				check(CreatorModeComponentJson.Get());
				if (!CreatorModeComponentJson.Get()) continue;

				FString UniqueIdentifier = CreatorModeComponentJson->GetStringField(TEXT("uniqueIdentifier"));
				check(!UniqueIdentifier.IsEmpty());
				if (UniqueIdentifier.IsEmpty()) continue;

				// find existing creator mode actor
				if (AActor* CreatorModeActor = UICreatorModeObjectComponent::GetCreatorModeObjectWithIdentifier(ThisPtr.Get(), UniqueIdentifier))
				{
					UICreatorModeObjectComponent* CreatorModeComponent = Cast<UICreatorModeObjectComponent>(CreatorModeActor->GetComponentByClass(UICreatorModeObjectComponent::StaticClass()));
					check(CreatorModeComponent);
					if (CreatorModeComponent)
					{
						CreatorModeComponent->SetupFromJson(JsonObject);
						CreatorModeComponent->MarkDirty();
						CreatorModeComponentsToRemove.Remove(CreatorModeComponent);
						ObjectsModified++;
					}
				}
				else // Existing creator mode actor does not exist, we can create it using the stored class path
				{
					FString ClassPathStr = JsonObject->GetStringField("ClassPathName");
					if (ClassPathStr.IsEmpty()) continue;

					FSoftObjectPath ClassPath = FSoftObjectPath(ClassPathStr);

					UTitanAssetManager& AssetManager = UTitanAssetManager::Get();

					TSoftClassPtr<AActor> ClassSoftPtr = TSoftClassPtr<AActor>(ClassPath);
					check(!ClassSoftPtr.IsNull());

					FStreamableDelegate OnClassLoad = FStreamableDelegate::CreateUObject(ThisPtr.Get(), &AIGameMode::SpawnCreatorModeActor, ClassSoftPtr, JsonObject);
					FStreamableManager& Streamable = UIGameplayStatics::GetStreamableManager(this);
					Streamable.RequestAsyncLoad(
						ClassPath,
						OnClassLoad,
						FStreamableManager::AsyncLoadHighPriority, false
					);
					ObjectsSpawned++;
				}
			}


			for (UICreatorModeObjectComponent* ComponentToRemove : CreatorModeComponentsToRemove)
			{
				if (!ComponentToRemove->bShouldBeSaved) continue;

				if (ComponentToRemove->WasInOriginalMap())
				{
					// If this was in the original map, we can use the pre-saved default info to restore it.
					RestoreOriginalCreatorModeObject(ComponentToRemove);
					ObjectsModified++;
					continue;
				}

				AActor* CompOwner = ComponentToRemove->GetOwner();
				check(CompOwner);
				if (!CompOwner) continue;

				CompOwner->Destroy();
				ObjectsRemoved++;
			}
		}

		FFormatNamedArguments Args;
		Args.Add(TEXT("SaveName"), FText::FromString(SaveName));
		Callback.ExecuteIfBound(FText::Format(FText::FromStringTable(TEXT("ST_ChatCommands"), Bunch.bSuccess ? TEXT("CmdLoadCreatorSucceeded") : TEXT("CmdLoadCreatorFailed")), Args));
		ThisPtr->bCreatorModePendingSave = false;
		END_PERF_TIME();
		UE_LOG(TitansLog, Log, TEXT("AIGameMode::LoadCreatorModeObjects(): Executed in %fms. %i modified, %i spawned, %i deleted"), (__pref_end - __pref_start) * 1000.f, ObjectsModified, ObjectsSpawned, ObjectsRemoved);

	});


	DatabaseEngine->LoadBunch(GetCreatorModeSavePath() / SaveName / TEXT(""), OnComplete);
	
}

void AIGameMode::ResetCreatorModeObjects()
{
	UIGameInstance* IGameInstance = UIGameplayStatics::GetIGameInstance(this);
	check(IGameInstance);
	if (!IGameInstance) return;

	START_PERF_TIME();

	int ObjectsModified = 0;
	int ObjectsRemoved = 0;
	TArray<UICreatorModeObjectComponent*> CreatorModeComponentsToRemove = IGameInstance->AllCreatorModeComponents;
	for (UICreatorModeObjectComponent* ComponentToRemove : CreatorModeComponentsToRemove)
	{
		if (!ComponentToRemove->bShouldBeSaved) continue;

		if (ComponentToRemove->WasInOriginalMap())
		{
			// If this was in the original map, we can use the pre-saved default info to restore it.
			RestoreOriginalCreatorModeObject(ComponentToRemove);
			ObjectsModified++;
			continue;
		}

		AActor* CompOwner = ComponentToRemove->GetOwner();
		check(CompOwner);
		if (!CompOwner) continue;

		CompOwner->Destroy();
		ObjectsRemoved++;
	}

	END_PERF_TIME();
	UE_LOG(TitansLog, Log, TEXT("AIGameMode::ResetCreatorModeObjects(): Executed in %fms. %i modified, %i deleted"), (__pref_end - __pref_start) * 1000.f, ObjectsModified, ObjectsRemoved);

}

void AIGameMode::RestoreOriginalCreatorModeObject(UICreatorModeObjectComponent* CMOComp)
{
	FString Identifier = CMOComp->UniqueIdentifier;
	
	TSharedPtr<FJsonObject>* JsonObject = DefaultCreatorModeActors.Find(Identifier);
	check(JsonObject != nullptr);
	if (!JsonObject) return;

	TSharedPtr<FJsonObject> ComponentJson = (*JsonObject)->GetObjectField(TEXT("ICreatorModeObjectComponent"));
	check(ComponentJson.Get());
	if (!ComponentJson.Get()) return;

	check (ComponentJson->GetStringField(TEXT("uniqueIdentifier")).Equals(Identifier));
	if (ComponentJson->GetStringField(TEXT("uniqueIdentifier")).Equals(Identifier))
	{
		CMOComp->SetupFromJson(*JsonObject);
		CMOComp->bIsDirty = false;
	}
}

void AIGameMode::SpawnCreatorModeActor(TSoftClassPtr<AActor> ClassSoftPtr, TSharedPtr<FJsonObject> JsonObject)
{
	UClass* Class = ClassSoftPtr.Get();
	check (Class);
	if (!Class) return;

	AActor* NewCreatorModeActor = GetWorld()->SpawnActorDeferred<AActor>(Class, FTransform::Identity, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	check (NewCreatorModeActor);
	if (!NewCreatorModeActor) return;

	NewCreatorModeActor->FinishSpawning(FTransform::Identity);

	UICreatorModeObjectComponent* CreatorModeComponent = Cast<UICreatorModeObjectComponent>(NewCreatorModeActor->GetComponentByClass(UICreatorModeObjectComponent::StaticClass()));
	check(CreatorModeComponent);
	if (CreatorModeComponent)
	{
		CreatorModeComponent->SetupFromJson(JsonObject);
		CreatorModeComponent->SetSpawnedByCreator(true);
	}

}

FString AIGameMode::GetCreatorModeSavePath()
{
	return FString(TEXT("CreatorMode/")) + (GetWorld() ? GetWorld()->GetMapName() : FString(TEXT("NoWorld"))) + FString(TEXT("/"));
}
