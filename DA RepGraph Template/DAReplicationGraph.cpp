// Copyright (C) 2018 - Dennis "MazyModz" Andersson.

/*

    MIT License

 Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), 
 to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, 
 and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, 
 WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include "DAReplicationGraph.h"
#include "Engine/LevelStreaming.h"
#include "Engine/LevelScriptActor.h"
#include "CoreGlobals.h"

#if WITH_GAMEPLAY_DEBUGGER
#include "GameplayDebuggerCategoryReplicator.h"
#endif

#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameState.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Pawn.h"

DEFINE_LOG_CATEGORY(LogDAReplicationGraph);

UDAReplicationGraph::UDAReplicationGraph() 
{
}

/** Sets the culldistance and update frequency to a FClassReplicationInfo data structure */
void InitClassReplicationInfo(FClassReplicationInfo& Info, UClass* Class, bool bSpatialize, float ServerMaxTickRate)
{
    check(Class != NULL);
    if(AActor* CDO = Cast<AActor>(Class->GetDefaultObject()))
    {
        if(bSpatialize == true)
        {
            // Set the default cull stance for the repliation info data to feed the grid node
            Info.CullDistanceSquared = CDO->NetCullDistanceSquared;
            UE_LOG(LogDAReplicationGraph, Log, TEXT("Setting cull distance for %s to %f (%f)"), *Class->GetFName(), Info.CullDistanceSquared, FMath::Square(Info.CullDistanceSquared));
        }

        // Handle update frequency
        Info.ReplicationPeriodFrame = FMath::Max<uint32>((uint32)FMath::RoundToFloat(ServerMaxTickRate / CDO->NetUpdateFrequency), 1);

        UClass* NativeClass = Class;
        while(!NativeClass->IsNative() && NativeClass->GetSuperClass() && NativeClass->GetSuperClass != AActor::StaticClass())
        {
            NativeClass = NativeClass->GetSuperClass();
        }

        UE_LOG(LogDAReplicationGraph, Log, TEXT("Setting replication period for %s (%s) to %d frames (%0.2f"), 
            *Class->GetName(), 
            *NativeClass->GetName(), 
            Info.ReplicationPeriodFrame, 
            CDO->NetUpdateFrequency
        );
    }
    else
    {
        UE_LOG(LogDAReplicationGraph, Error, TEXT("Could not create CDO for: %s"), *Class->GetName());
    }
}

void UDAReplicationGraph::ResetGameWorldState()
{
    Super::ResetGameWorldState();

    // Empty the info for level streaming actors
    AlwaysRelevantStreamingLevelActors.Empty();

    // Reset the always relevant node world state
    for(auto& ConnectionList : { Connections, PendingConnections })
    {
        for(UNetReplicationGraphConnection* Connection : ConnectionList)
        {
            for (UReplicationGraphNode* ConnectionNode : Connection->GetConnectionGraphNodes())
	    {
                UDAReplicationGraphNode_AlwaysRelevant_ForConnection* RelevantNode = Cast<UDAReplicationGraphNode_AlwaysRelevant_ForConnection>(ConnectionNode);
                if(RelevantNode != NULL)
                {
                    RelevantNode->ResetGameWorldState();
                }
            }
        }
    }
}

void UDAReplicationGraph::InitGlobalActorClassSettings()
{
    // Here we initialize the replication graph by assining the class routing policies

    Super::InitGlobalActorClassSettings();

    // ------------------------------------------------
    // Assign the mapping rules for classes
    InitClassRules();

    TArray<UClass*> AllReplicatedClasses;
    for(TObjectIterator<UClass> Itr; ++Itr)
    {
        UClass* Class = *Itr;
        AActor* ActorCDO = Cast<AActor>(Class->GetDefaultObject());

        // Do not add the actor if it is not set to replicate
        if(!ActorCDO || ActorCDO->GetIsReplicated() == false)
        {
            continue;
        }

        // Do not add SKEL and REINST classes
        const FName ClassName = Class->GetName();
        if(ClassName.StartsWith(TEXT("SKEL_")) || ClassName.StartsWith(TEXT("REINST_")))
        {
            continue;
        }

        // We have determined that this class should replicate and be used by this graph.
        AllReplicatedClasses.Add(Class);

        // If we have already mapped this class we can skip
        if(ClassRepPolicies.Contains(Class, false))
        {
            continue;
        }

        // Gets if the actor should considered for spatialization
        auto ShouldSpatialize = [] ( const AActor* Actor )
        {
            return Actor->GetIsReplicated() && (!(Actor->bAlwayRelevant || Actor->bOnlyRelevantToOwner || Actor->bNetUseOwnerRelevancy));
        }; 

        // Gets a debug string for an actor
        auto GetDebugStr = [] ( const AActor* Actor )
        {
            return FString::Printf(TEXT("%s [%d/%d/%d]")
                *Actor->GetClass()->GetName(), Actor->bAlwayRelevant, Actor->bOnlyRelevantToOwner, Actor->bNetUseOwnerRelevancy);
        };

        // ------------------------------------------------
        // Only handle this class if it differs from the Super Class

        UClass* SuperClass = Class->GetSuperClass();
        if(AActor* SuperCDO = Cast<AActor>(SuperClass->GetDefaultObject()))
        {
            if (SuperCDO->GetIsReplicated() == ActorCDO->GetIsReplicated()
                && SuperCDO->bAlwaysRelevant == ActorCDO->bAlwaysRelevant
                && SuperCDO->bOnlyRelevantToOwner == ActorCDO->bOnlyRelevantToOwner
                && SuperCDO->bNetUseOwnerRelevancy == ActorCDO->bNetUseOwnerRelevancy)
            {
                continue;;
            }

            if (ShouldSpatalize(ActorCDO) == false && ShouldSpatalize(SuperCDO) == true)
			      {
				        UE_LOG(LogDAReplicationGraph, Log, TEXT("Adding %s to NonSpatalizedChildClasses"), *GetLegacyDebugStr(ActorCDO));
				        NonSpatializedClasses.Add(Class);
			      }
        }

        if (ShouldSpatalize(ActorCDO) == true)
        {
            ClassRepPolicies.Set(Class, EClassRepPolicy::Spatialize_Dynamic);
        }
        else if (ActorCDO->bAlwayRelevant && !ActorCDO->bOnlyRelevantToOwner)
        {
            ClassRepPolicies.Set(Class, EClassRepPolicy::RelevantAllConnections);
        }
    }

    // --------------------------------------------------------
    // Explicitly set replication information about actors here
    TArray<UClass*> ExplicitlySetClass;
    InitExplicitlySetClasses(ExplicitlySetClass);

    // Set infos for all replicated classes that are not explicitly set
    for(UClass* ReplicatedClass : AllReplicatedClasses)
    {
        UClass* SetClass = ExplicitlySetClass.FindByPrediate([&] ( const UClass* InClass ) { return ReplicatedClass->IsChildOf(SetClass); });
        if (SetClass != NULL)
        {
            continue;
        }

        const bool bSpatialized = IsSpatialized(ClassRepPolicies.GetChecked(ReplicatedClass));

        FClassReplicationInfo ClassInfo;
        InitClassReplicationInfo(ClassInfo, ReplicatedClass, bSpatialized, NetDriver->NetServerMaxTickRate);
        GlobalActorReplicationInfoMap.SetClassInfo(ReplicatedClass, ClassInfo);
    }

    DestructInfoMaxDistanceSquared = DestructInfoMaxDistance * DestructInfoMaxDistance;

    // --------------------------------------------------------
    // Listen for static events so we can I.E add things to the 
    // always relevant for connection node and dependant actor list
    AssignStaticEvents();
}

void UDAReplicationGraph::InitGlobalGraphNodes()
{
    // Here we create our nodes

    PreAllocateRepList(3, 12);
	  PreAllocateRepList(6, 12);
	  PreAllocateRepList(128, 64);
    PreAllocateRepList(512, 16);
    
    // --------------------------------
    // Create the grid node for spatialization
    GridNode = CreateNewNode<UReplicationGraphNode_GridSpatialization2D>();
    GridNode->CellSize = GridCellSize;
    GridNode->SpatialBias = FVector2D(SpatialBiasX, SpatialBiasY);

    // Disable all spatial rebuilds
    if (bDisableSpatialRebuilds == true)
    {
        GridNode->AddSpatialRebuildBlacklistClass(AActor::StaticClass());
    }

    AddGlobalGraphNode(GridNode);

    // --------------------------------
    // Create the node for always relevant actors to all
    AlwaysRelevantNode = CreateNewNode<UReplicationGraphNode_ActorList>();
    AddGlobalGraphNode(AlwaysRelevantNode);
}

void UDAReplicationGraph::InitConnectionGraphNodes(UNetReplicationGraphConnection* RepConnection)
{
    Super::InitConnectionGraphNodes(RepConnection);

    // Here we create nodes specific for connections and handle level streaming
    UDAReplicationGraphNode_AlwaysRelevant_ForConnection* ConnectionNode = CreateNewNode<UDAReplicationGraphNode_AlwaysRelevant_ForConnection>();
    ConnectionNode->OnClientVisibleLevelNameAdd.AddUObject(AlwaysRelevantNode, &UDAReplicationGraphNode_AlwaysRelevant_ForConnection::OnClientLevelVisibilityAdd);
    ConnectionNode->OnClientVisibleLevelNameRemove.AddUObject(AlwaysRelevantNode, &UDAReplicationGraphNode_AlwaysRelevant_ForConnection::OnClientLevelVisibilityRemove);    

    AddConnectionGraphNode(ConnectionNode, RepConnection);
}

void UDAReplicationGraph::RouteAddNetworkActorToNodes(const FNewReplicatedActorInfo& ActorInfo, FGlobalActorReplicationInfo& GlobalInfo)
{
    // Here we handle the routing of the actors into the different nodes

    EClassRepPolicy MappingPolicy = GetMappingPolicy(ActorInfo.Class);
    switch(MappingPolicy)
    {
        case EClassRepPolicy::RelevantAllConnections:
        {
            if (ActorInfo.StreamingLevelName == NAME_None)
            {
                AlwaysRelevantNode->NotifyAddNetworkActor(ActorInfo);
            }
            else
            {
                FActorRepListRefView& RepList = AlwaysRelevantStreamingLevelActors.FindOrAdd(ActorInfo.StreaminLevelName);
                RepList.PrepareForWrite();
                PreList.ConditionalAdd(ActorInfo.Actor);
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
    // Remove actors from their routes

    EClassRepPolicy MappingPolicy = GetMappingPolicy(ActorInfo.Class);
    switch(MappingPolicy)
    {
        case EClassRepPolicy::RelevantAllConnections:
        {
            if (ActorInfo.StreamingLevelName == NAME_None)
            {
                AlwaysRelevantNode->NotifyRemoveNetworkActor(ActorInfo);
            }
            else
            {
                FActorRepListRefView& RepList = AlwaysRelevantClasses.FindChecked(ActorInfo.StreamingLevelName);
                RepList.Remove(ActorInfo.Actor);
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

#if WITH_GAMEPLAY_DEBUGGER
void UDAReplicationGraph::OnGameplayDebuggerOwnerChange(AGameplayDebuggerCategoryReplicator* Debugger, APlayerController* OldOwner)
{
	  if (UDAReplicationGraphNode_AlwayRelevant_ForConnection* AlwaysRelevantConnectionNode = GetAlwaysRelevantNode(OldOwner))
	  {
		  AlwaysRelevantConnectionNode->GameplayDebugger = nullptr;
	  }

	  if (UDAReplicationGraphNode_AlwayRelevant_ForConnection* AlwaysRelevantConnectionNode = GetAlwaysRelevantNode(Debugger->GetReplicationOwner()))
	  {
        AlwaysRelevantConnectionNode->GameplayDebugger = Debugger;
	  }
}
#endif

EClassRepPolicy UDAReplicationGraph::GetMappingPolicy(const UClass* InClass)
{
    return ClassRepPolicies.Get(InClass) != nullptr ? *ClassRepPolicies.Get(InClass) : EClassRepPolicy::NotRouted;
}

UDAReplicationGraphNode_AlwaysRelevant_ForConnection* UDAReplicationGraph::GetAlwaysRelevantNode(APlayerController* PlayerController)
{
	  if (PlayerController != nullptr)
	  {
		    if (UNetConnection* NetConnection = PlayerController->GetNetConnection())
		    {
			        if (UNetReplicationGraphConnection* GraphConnection = FindOrAddConnectionManager(NetConnection))
			        {
				          for (UReplicationGraphNode* ConnectionNode : GraphConnection->GetConnectionGraphNodes())
				          {
					            if (UDAReplicationGraphNode_AlwayRelevant_ForConnection* AlwaysRelevantConnectionNode = Cast<UDAReplicationGraphNode_AlwayRelevant_ForConnection>(ConnectionNode))
					            {
						            return AlwaysRelevantConnectionNode;
					            }
				          }
			        }
		    }
	}
	
	return nullptr;
}

void UDAReplicationGraph::SetClassRule(UClass* InClass, EClassRepPolicy MappingPolicy)
{
    ClassRepPolicies.Set(InClass, MappingPolicy);
}

void UDAReplicationGraph::InitExplicitlySetClasses(TArray<UClass*>& ExplicitlySetClasses)
{
    FClassReplicationInfo PawnClassRepInfo;
    PawnClassRepInfo.DistancePriorityScale = 1.0f;
    PawnClassRepInfo.StarvationPriorityScale = 1.0f;
    PawnClassRepInfo.ActorChannelFrameTimeout = 4;
    PawnClassRepInfo.CullDistanceSquared = 50000.f * 50000.f;

    GlobalActorReplicationInfoMap.SetClassRule(APawn::StaticClass(), PawnClassRepInfo);
    ExplicitlySetClasses.Add(APawn::StaticClass());

    // #TODO: add your custom explicitly set replication infos here
}

void UDAReplicationGraph::AssignStaticEvents()
{
#if WITH_GAMEPLAY_DEBUGGER
	AGameplayDebuggerCategoryReplicator::NotifyDebuggerOwnerChange.AddUObject(this, &UDAReplicationGraph::OnGameplayDebuggerOwnerChange);
#endif

    // #TODO: add your custom event bindings here
}

void UDAReplicationGraph::InitClassRules()
{
    SetClassRule(ALevelScriptActor::StaticClass(),                       EClassRepPolicy::NotRouted);
    SetClassRule(AReplicationGraphDebugActor::StaticClass(),             EClassRepPolicy::NotRouted);
    SetClassRule(AInfo::StaticClass(),                                   EClassRepPolicy::RelevantAllConnections);

    // #TODO: route your classes here to the desired policies

#if WITH_GAMEPLAY_DEBUGGER
    SetClassRule(AGameplayDebuggerCategoryReplicator::StaticClass(),     EClassRepPolicy::NotRouted);
#endif
}

// -------------------------------------------------------
// UDAReplicationGraphNode_AlwaysRelevant_ForConnection

UDAReplicationGraphNode_AlwaysRelevant_ForConnection::UDAReplicationGraphNode_AlwaysRelevant_ForConnection()
{
}

void UDAReplicationGraphNode_AlwaysRelevant_ForConnection::GatherActorListsForConnection(const FConnectionGatherActorListParameters& Params)
{
    Super::GatherActorListsForConnection(Params);

    UDAReplicationGraph* RepGraph = CastChecked<UDAReplicationGraph>(GetOuter());

    FPerConnectionActorInfoMap& ConnectionActorInfoMap = Params.ConnectionManager.ActorInfoMap;
    TMap<FName, FActorRepListRefView>& AlwaysRelevantStreamingLevelActors = RepGraph->AlwaysRelevantStreamingLevelActors;

    for(int32 Idx = AlwaysRelevantStreamingLevels.Num() - 1; Idx >= 0; --Idx)
    {
        FName StreamingLevel = AlwaysRelevantStreamingLevels[Idx];
        FActorRepListRefView* LevelPtr = AlwaysRelevantStreamingLevelActors.Find(StreamingLevel);
        
        if (LevelPtr == NULL)
        {
            AlwaysRelevantStreamingLevels.RemoveAtSwap(Idx, 1, false);
            continue;
        }

        FActorRepListRefView& RepList = *LevelPtr;
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
	  if (GameplayDebugger)
	  {
		  ReplicationActorList.ConditionalAdd(GameplayDebugger);
	  }
#endif
}

void UDAReplicationGraphNode_AlwaysRelevant_ForConnection::OnClientLevelVisibilityAdd(FName LevelName, UWorld* StreamingWorld)
{
    AlwaysRelevantStreamingLevels.Add(LevelName);
}

void UDAReplicationGraphNode_AlwaysRelevant_ForConnection::OnClientLevelVisibilityRemove(FName LevelName)
{
    AlwaysRelevantStreamingLevels.Remove(LevelName);
}

void UDAReplicationGraphNode_AlwayRelevant_ForConnection::ResetGameWorldState()
{
    AlwaysRelevantStreamingLevels.Empty();
}
