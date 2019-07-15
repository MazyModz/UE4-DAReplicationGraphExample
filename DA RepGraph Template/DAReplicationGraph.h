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

#pragma once

#include "CoreMinimal.h"
#include "ReplicationGraph.h"
#include "DAReplicationGraph.generated.h"

enum class EClassRepPolicy : uint8
{
	NotRouted,
	RelevantAllConnections,

	// --------------------------------
	// Spatialized routes into the grid node

	Spatialize_Static,		// Used for actors for frequent updates / updates every frame
	Spatialize_Dynamic,		// For do need updates every frame
	Spatialize_Dormancy		// Actors that can either be Static or Dynamic determined by their AActor::NetDormancy state
};

class UReplicationGraphNode_ActorList;
class UReplicationGraphNode_GridSpatialization2D;
class UReplicationGraphNode_AlwaysRelevant_ForConnection;
class AGameplayDebuggerCategoryReplicator;

/**
 * 
 */
UCLASS(Transient, config=Engine)
class UDAReplicationGraph : public UReplicationGraph
{
public:

	GENERATED_BODY()

	// ~ begin UReplicationGraph implementation
	virtual void ResetGameWorldState() override;
	virtual void InitConnectionGraphNodes(UNetReplicationGraphConnection* ConnectionManager) override;
	virtual void InitGlobalActorClassSettings() override;
	virtual void InitGlobalGraphNodes() override;
	virtual void RouteAddNetworkActorToNodes(const FNewReplicatedActorInfo& ActorInfo, FGlobalActorReplicationInfo& GlobalInfo) override;
	virtual void RouteRemoveNetworkActorToNodes(const FNewReplicatedActorInfo& ActorInfo) override;
	// ~ end UReplicationGraph

	/** Sets class replication info for a class */
	void InitClassReplicationInfo(FClassReplicationInfo& Info, UClass* InClass, bool bSpatilize, float ServerMaxTickRate);

	UPROPERTY()
	TArray<UClass*> SpatializedClasses;
	
	UPROPERTY()
	TArray<UClass*> NonSpatializedClasses;

	UPROPERTY()
	TArray<UClass*> AlwaysRelevantClasses;

	// -------------------------------------
	// ReplicationGraph Nodes

	/**
	 * This is probably the most important node in the replication graph
	 *
	 * Carves the map up into grids and determines if an actor should send network updates
	 * to a connection depending on the different pre-defined grids
	 */
	UPROPERTY()
	UReplicationGraphNode_GridSpatialization2D* GridNode;

	UPROPERTY()
	UReplicationGraphNode_ActorList* AlwaysRelevantNode;

	/** Maps the actors the needs to be always relevant across streaming levels */
	TMap<FName, FActorRepListRefView> AlwaysRelevantStreamingLevelActors;

protected:

	/** Gets the connection always relevant node from a player controller */
	class UDAReplicationGraphNode_AlwaysRelevant_ForConnection* GetAlwaysRelevantNode(APlayerController* PlayerController);

#if WITH_GAMEPLAY_DEBUGGER
	void OnGameplayDebuggerOwnerChange(AGameplayDebuggerCategoryReplicator* Debugger, APlayerController* OldOwner);
#endif

	FORCEINLINE bool IsSpatialized(EClassRepPolicy Mapping)
	{
		return Mapping >= EClassRepPolicy::Spatialize_Static;
	}

	/** Gets the mapping to used for the given class */
	EClassRepPolicy GetMappingPolicy(const UClass* InClass);

	/** Maps a class to a mapping policy */
	TClassMap<EClassRepPolicy> ClassRepPolicies;

	float GridCellSize = 10000.f;			// The size of one grid cell in the grid node
	float SpatialBiasX = -150000.f;			// "Min X" for replication
	float SpatialBiasY = -200000.f;			// "Min Y" for replication
	bool bDisableSpatialRebuilding = true;
};

UCLASS()
class UDAReplicationGraphNode_AlwaysRelevant_ForConnection : public UReplicationGraphNode_AlwaysRelevant_ForConnection
{
public:

	GENERATED_BODY()

	// ~ begin UReplicationGraphNode_AlwaysRelevant_ForConnection implementation
	virtual void GatherActorListsForConnection(const FConnectionGatherActorListParameters& Params) override;
	// ~ end UReplicationGraphNode_AlwaysRelevant_ForConnection

	void OnClientLevelVisibilityAdd(FName LevelName, UWorld* LevelWorld);
	void OnClientLevelVisibilityRemove(FName LevelName);

	void ResetGameWorldState();

#if WITH_GAMEPLAY_DEBUGGER
	AGameplayDebuggerCategoryReplicator* GameplayDebugger = nullptr;
#endif

protected:

	/** Stores levelstreaming actors */
	TArray<FName, TInlineAllocator<64>> AlwaysRelevantStreamingLevels;
};
