// Copyright (C) 2018 - Dennis "MazyModz" Andersson.

/*

	MIT License

 Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files
 (the "Software"), to deal in the Software without restriction, including without limitation the rights to use,
 copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
 persons to whom the Software is furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
 TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#include "DAReplicationGraph.h"
#include "Engine/LevelScriptActor.h"

#if WITH_GAMEPLAY_DEBUGGER
#include "GameplayDebuggerCategoryReplicator.h"
#endif

#include "DAProjectile.h"
#include "DABuildableWall.h"
#include "DACharacter.h"
#include "DAWeapon.h"

void UDAReplicationGraph::ResetGameWorldState()
{
	Super::ResetGameWorldState();
	AlwaysRelevantStreamingLevelActors.Empty();

	for (auto& ConnectionList : { Connections, PendingConnections })
	{
		for (UNetReplicationGraphConnection* Connection : ConnectionList)
		{
			for (UReplicationGraphNode* ConnectionNode : Connection->GetConnectionGraphNodes())
			{
				UDAReplicationGraphNode_AlwaysRelevant_ForConnection* Node = Cast<UDAReplicationGraphNode_AlwaysRelevant_ForConnection>(ConnectionNode);
				if (Node != nullptr)
				{
					Node->ResetGameWorldState();
				}
			}
		}
	}
}

void UDAReplicationGraph::InitConnectionGraphNodes(UNetReplicationGraphConnection* ConnectionManager)
{
	UDAReplicationGraphNode_AlwaysRelevant_ForConnection* Node = CreateNewNode<UDAReplicationGraphNode_AlwaysRelevant_ForConnection>();
	ConnectionManager->OnClientVisibleLevelNameAdd.AddUObject(Node, &UDAReplicationGraphNode_AlwaysRelevant_ForConnection::OnClientLevelVisibilityAdd);
	ConnectionManager->OnClientVisibleLevelNameRemove.AddUObject(Node, &UDAReplicationGraphNode_AlwaysRelevant_ForConnection::OnClientLevelVisibilityRemove);

	AddConnectionGraphNode(Node, ConnectionManager);
}

void UDAReplicationGraph::InitGlobalActorClassSettings()
{
	Super::InitGlobalActorClassSettings();

	// ----------------------------------------
	// Assign mapping to classes

	auto SetRule = [&](UClass* InClass, EClassRepPolicy Mapping) { ClassRepPolicies.Set(InClass, Mapping); };

	SetRule(AReplicationGraphDebugActor::StaticClass(),				EClassRepPolicy::NotRouted);
	SetRule(ALevelScriptActor::StaticClass(),						EClassRepPolicy::NotRouted);
	SetRule(AInfo::StaticClass(),									EClassRepPolicy::RelevantAllConnections);
	SetRule(ADAProjectile::StaticClass(),							EClassRepPolicy::Spatialize_Dynamic);
	SetRule(ADABuildableWall::StaticClass(),						EClassRepPolicy::Spatialize_Static);

#if WITH_GAMEPLAY_DEBUGGER
	SetRule(AGameplayDebuggerCategoryReplicator::StaticClass(),		EClassRepPolicy::NotRouted);
#endif

	TArray<UClass*> ReplicatedClasses;
	for (TObjectIterator<UClass> Itr; Itr; ++Itr)
	{
		UClass* Class = *Itr;
		AActor* ActorCDO = Cast<AActor>(Class->GetDefaultObject());

		// Do not add the actor if it does not replicate
		if (!ActorCDO || !ActorCDO->GetIsReplicated())
		{
			continue;
		}

		// Do not add SKEL and REINST classes
		FString ClassName = Class->GetName();
		if (ClassName.StartsWith("SKEL_") || ClassName.StartsWith("REINST_"))
		{
			continue;
		}

		ReplicatedClasses.Add(Class);

		// if we already have mapped it to the policy, dont do it again
		if (ClassRepPolicies.Contains(Class, false))
		{
			continue;
		}

		auto ShouldSpatialize = [](const AActor* Actor)
		{
			return Actor->GetIsReplicated() && (!(Actor->bAlwaysRelevant || Actor->bOnlyRelevantToOwner || Actor->bNetUseOwnerRelevancy));
		};

		UClass* SuperClass = Class->GetSuperClass();
		if (AActor* SuperCDO = Cast<AActor>(SuperClass->GetDefaultObject()))
		{
			if (SuperCDO->GetIsReplicated() == ActorCDO->GetIsReplicated()
				&& SuperCDO->bAlwaysRelevant == ActorCDO->bAlwaysRelevant
				&& SuperCDO->bOnlyRelevantToOwner == ActorCDO->bOnlyRelevantToOwner
				&& SuperCDO->bNetUseOwnerRelevancy == ActorCDO->bNetUseOwnerRelevancy)
			{
				continue;
			}

			if (ShouldSpatialize(ActorCDO) == false && ShouldSpatialize(SuperCDO) == true)
			{
				NonSpatializedClasses.Add(Class);
			}
		}

		if (ShouldSpatialize(ActorCDO) == true)
		{
			SetRule(Class, EClassRepPolicy::Spatialize_Dynamic);
		}
		else if (ActorCDO->bAlwaysRelevant && !ActorCDO->bOnlyRelevantToOwner)
		{
			SetRule(Class, EClassRepPolicy::RelevantAllConnections);
		}
	}

	// --------------------------------------
	// Explicitly set replication info for our classes

	TArray<UClass*> ExplicitlySetClasses;

	auto SetClassInfo = [&](UClass* InClass, FClassReplicationInfo& RepInfo)
	{
		GlobalActorReplicationInfoMap.SetClassInfo(InClass, RepInfo);
		ExplicitlySetClasses.Add(InClass);
	};

	FClassReplicationInfo PawnClassInfo;
	PawnClassInfo.SetCullDistanceSquared(300000.f * 300000.f);
	SetClassInfo(APawn::StaticClass(), PawnClassInfo);

	for (UClass* ReplicatedClass : ReplicatedClasses)
	{
		if (ExplicitlySetClasses.FindByPredicate([&](const UClass* InClass) { return ReplicatedClass->IsChildOf(InClass); }) != nullptr)
		{
			continue;
		}

		bool bSptatilize = IsSpatialized(ClassRepPolicies.GetChecked(ReplicatedClass));

		FClassReplicationInfo ClassInfo;
		InitClassReplicationInfo(ClassInfo, ReplicatedClass, bSptatilize, NetDriver->NetServerMaxTickRate);
		GlobalActorReplicationInfoMap.SetClassInfo(ReplicatedClass, ClassInfo);
	}

	// -------------------------------
	// Bind events here

	ADACharacter::OnNewWeapon.AddUObject(this, &UDAReplicationGraph::OnCharacterNewWeapon);

#if WITH_GAMEPLAY_DEBUGGER
	AGameplayDebuggerCategoryReplicator::NotifyDebuggerOwnerChange.AddUObject(this, &UDAReplicationGraph::OnGameplayDebuggerOwnerChange);
#endif
}

void UDAReplicationGraph::InitGlobalGraphNodes()
{
	// ---------------------------------
	// Create our grid node
	GridNode = CreateNewNode<UReplicationGraphNode_GridSpatialization2D>();
	GridNode->CellSize = GridCellSize;
	GridNode->SpatialBias = FVector2D(SpatialBiasX, SpatialBiasY);

	if (bDisableSpatialRebuilding == true)
	{
		GridNode->AddToClassRebuildDenyList(AActor::StaticClass());
	}

	AddGlobalGraphNode(GridNode);

	// ---------------------------------
	// Create our always relevant node
	AlwaysRelevantNode = CreateNewNode<UReplicationGraphNode_ActorList>();
	AddGlobalGraphNode(AlwaysRelevantNode);
}

void UDAReplicationGraph::RouteAddNetworkActorToNodes(const FNewReplicatedActorInfo& ActorInfo, FGlobalActorReplicationInfo& GlobalInfo)
{
	EClassRepPolicy MappingPolicy = GetMappingPolicy(ActorInfo.Class);
	switch (MappingPolicy)
	{
	case EClassRepPolicy::RelevantAllConnections:
	{
		if (ActorInfo.StreamingLevelName == NAME_None)
		{
			AlwaysRelevantNode->NotifyAddNetworkActor(ActorInfo);
		}
		else
		{
			FActorRepListRefView& RepList = AlwaysRelevantStreamingLevelActors.FindOrAdd(ActorInfo.StreamingLevelName);
			RepList.ConditionalAdd(ActorInfo.Actor);
		}
		break;
	}

	case EClassRepPolicy::Spatialize_Static:
	{
		GridNode->AddActor_Static(ActorInfo, GlobalInfo);
		break;
	}

	case EClassRepPolicy::Spatialize_Dynamic:
	{
		GridNode->AddActor_Dynamic(ActorInfo, GlobalInfo);
		break;
	}

	case EClassRepPolicy::Spatialize_Dormancy:
	{
		GridNode->AddActor_Dormancy(ActorInfo, GlobalInfo);
		break;
	}

	default:
	{
		break;
	}
	}
}

void UDAReplicationGraph::RouteRemoveNetworkActorToNodes(const FNewReplicatedActorInfo& ActorInfo)
{
	EClassRepPolicy MappingPolicy = GetMappingPolicy(ActorInfo.Class);
	switch (MappingPolicy)
	{
	case EClassRepPolicy::RelevantAllConnections:
	{
		if (ActorInfo.StreamingLevelName == NAME_None)
		{
			AlwaysRelevantNode->NotifyRemoveNetworkActor(ActorInfo);
		}
		else
		{
			FActorRepListRefView& RepList = AlwaysRelevantStreamingLevelActors.FindOrAdd(ActorInfo.StreamingLevelName);
			RepList.RemoveFast(ActorInfo.Actor);
		}
		break;
	}

	case EClassRepPolicy::Spatialize_Static:
	{
		GridNode->RemoveActor_Static(ActorInfo);
		break;
	}

	case EClassRepPolicy::Spatialize_Dynamic:
	{
		GridNode->RemoveActor_Dynamic(ActorInfo);
		break;
	}

	case EClassRepPolicy::Spatialize_Dormancy:
	{
		GridNode->RemoveActor_Dormancy(ActorInfo);
		break;
	}

	default:
	{
		break;
	}
	}
}

void UDAReplicationGraph::InitClassReplicationInfo(FClassReplicationInfo& Info, UClass* InClass, bool bSpatilize, float ServerMaxTickRate)
{
	if (AActor* CDO = Cast<AActor>(InClass->GetDefaultObject()))
	{
		if (bSpatilize == true)
		{
			Info.SetCullDistanceSquared(CDO->NetCullDistanceSquared);
		}

		Info.ReplicationPeriodFrame = FMath::Max<uint32>((uint32)FMath::RoundToFloat(ServerMaxTickRate / CDO->NetUpdateFrequency), 1);
	}
}

class UDAReplicationGraphNode_AlwaysRelevant_ForConnection* UDAReplicationGraph::GetAlwaysRelevantNode(APlayerController* PlayerController)
{
	if (PlayerController != NULL)
	{
		if (UNetConnection* NetConnection = PlayerController->NetConnection)
		{
			if (UNetReplicationGraphConnection* GraphConnection = FindOrAddConnectionManager(NetConnection))
			{
				for (UReplicationGraphNode* ConnectionNode : GraphConnection->GetConnectionGraphNodes())
				{
					UDAReplicationGraphNode_AlwaysRelevant_ForConnection* Node = Cast<UDAReplicationGraphNode_AlwaysRelevant_ForConnection>(ConnectionNode);
					if (Node != NULL)
					{
						return Node;
					}
				}
			}
		}
	}

	return nullptr;
}

#if WITH_GAMEPLAY_DEBUGGER
void UDAReplicationGraph::OnGameplayDebuggerOwnerChange(AGameplayDebuggerCategoryReplicator* Debugger, APlayerController* OldOwner)
{
	if (UDAReplicationGraphNode_AlwaysRelevant_ForConnection* Node = GetAlwaysRelevantNode(OldOwner))
	{
		Node->GameplayDebugger = nullptr;
	}

	if (UDAReplicationGraphNode_AlwaysRelevant_ForConnection* Node = GetAlwaysRelevantNode(Debugger->GetReplicationOwner()))
	{
		Node->GameplayDebugger = Debugger;
	}
}
#endif

void UDAReplicationGraph::OnCharacterNewWeapon(class ADACharacter* Pawn, class ADAWeapon* NewWeapon, class ADAWeapon* OldWeapon)
{
	if (!Pawn || Pawn->GetWorld() != GetWorld())
	{
		return;
	}

	if (NewWeapon != nullptr)
	{
		GlobalActorReplicationInfoMap.AddDependentActor(Pawn, NewWeapon);
	}

	if (OldWeapon != nullptr)
	{
		GlobalActorReplicationInfoMap.RemoveDependentActor(Pawn, NewWeapon);
	}
}

EClassRepPolicy UDAReplicationGraph::GetMappingPolicy(UClass* const InClass)
{
	return ClassRepPolicies.Get(InClass) != NULL ? *ClassRepPolicies.Get(InClass) : EClassRepPolicy::NotRouted;
}

// --------------------------------------------------
// UDAReplicationGraphNode_AlwaysRelevant_ForConnection

void UDAReplicationGraphNode_AlwaysRelevant_ForConnection::GatherActorListsForConnection(const FConnectionGatherActorListParameters& Params)
{
	Super::GatherActorListsForConnection(Params);

	UDAReplicationGraph* RepGraph = CastChecked<UDAReplicationGraph>(GetOuter());

	FPerConnectionActorInfoMap& ConnectionActorInfoMap = Params.ConnectionManager.ActorInfoMap;
	TMap<FName, FActorRepListRefView>& AlwaysRelevantStreamingLevelActors = RepGraph->AlwaysRelevantStreamingLevelActors;

	for (int32 Idx = AlwaysRelevantStreamingLevels.Num() - 1; Idx >= 0; --Idx)
	{
		FName StreamingLevel = AlwaysRelevantStreamingLevels[Idx];
		FActorRepListRefView* ListPtr = AlwaysRelevantStreamingLevelActors.Find(StreamingLevel);

		if (ListPtr == nullptr)
		{
			AlwaysRelevantStreamingLevels.RemoveAtSwap(Idx, 1, false);
			continue;
		}

		FActorRepListRefView& RepList = *ListPtr;
		if (RepList.Num() > 0)
		{
			bool bAllDormant = true;
			for (FActorRepListType Actor : RepList)
			{
				FConnectionReplicationActorInfo& ConnectionActorInfo = ConnectionActorInfoMap.FindOrAdd(Actor);
				if (ConnectionActorInfo.bDormantOnConnection == false)
				{
					bAllDormant = false;
					break;
				}
			}

			if (bAllDormant == true)
			{
				AlwaysRelevantStreamingLevels.RemoveAtSwap(Idx, 1, false);
			}
			else
			{
				Params.OutGatheredReplicationLists.AddReplicationActorList(RepList);
			}
		}
	}

#if WITH_GAMEPLAY_DEBUGGER
	if (GameplayDebugger != NULL)
	{
		ReplicationActorList.ConditionalAdd(GameplayDebugger);
	}
#endif
}

void UDAReplicationGraphNode_AlwaysRelevant_ForConnection::OnClientLevelVisibilityAdd(FName LevelName, UWorld* LevelWorld)
{
	AlwaysRelevantStreamingLevels.Add(LevelName);
}

void UDAReplicationGraphNode_AlwaysRelevant_ForConnection::OnClientLevelVisibilityRemove(FName LevelName)
{
	AlwaysRelevantStreamingLevels.Remove(LevelName);
}

void UDAReplicationGraphNode_AlwaysRelevant_ForConnection::ResetGameWorldState()
{
	AlwaysRelevantStreamingLevels.Empty();
}
