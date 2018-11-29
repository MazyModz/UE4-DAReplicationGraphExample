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

#pragma once

#include "CoreMinimal.h"
#include "ReplicationGraph.h"
#include "DAReplicationGraph.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogDAReplicationGraph, Display, All);

/** Class Policies for which UReplicationGraphNode we should route actors to */
enum class EClassRepPolicy : uint8
{
    NotRouted,
    RelevantAllConnections,

    // ------------------------------------------------------------
    // Spatialized routes. 
    // Routed into UReplicationGraphNode_GridSpatialization2D GridNode

    Spatialize_Static,          // Used for actors that should not send frequent updates and do not need to be updated every frame. 
    Spatialize_Dynamic,         // Used for actors that should send frequent updates, IE projectiles. Actors routes into this are updated every frame. 
    Spatialize_Dormancy         // While the actor NetDormancy state is set to dormant it routes to Static. When set to Awake it routes into dynamic. So it a sort of hybrid between static and dyanmic which we can control.
};

class UReplicationGraphNode_GridSpatialization2D;
class UReplicationGraphNode_ActorList;
class AGameplayDebuggerCategoryReplicator;

/** 
 * Main DA Replication graph. Slightly more advanced than BasicReplicationGraph. 
 * A good enough setup for most games.
 * 
 * You can create a child class from this object and use the following events:
 * 
 *   InitExplicitlySetClasses()     - Explicitly set replication info here
 *   AssignStaticEvents()           - Bind events for I.E. adding something to a DependantActorList
 *   InitClassRules()               - Set the Mapping Routing Policy for Actor Classes
 * 
 * 
 * ~ Dennis "MazyModz" Andersson
 * 
 */
UCLASS(Transient, config=Engine)
class UDAReplicationGraph : public ReplicationGraph
{
public:

    GENERATED_BODY()

    UDAReplicationGraph();

    // ~ begin UReplicationGraph implementation
    virtual void ResetGameWorldState() override;
    virtual void InitGlobalActorClassSettings() override;
	virtual void InitGlobalGraphNodes() override;
	virtual void InitConnectionGraphNodes(UNetReplicationGraphConnection* RepGraphConnection) override;
	virtual void RouteAddNetworkActorToNodes(const FNewReplicatedActorInfo& ActorInfo, FGlobalActorReplicationInfo& GlobalInfo) override;
    virtual void RouteRemoveNetworkActorToNodes(const FNewReplicatedActorInfo& ActorInfo) override;
    // ~ end UReplicationGraph



    // ------------------------------
    // DAReplicationGraph Events

    /** 
     * Called to handle classes that should have their replication info
     * explicitly set.
     * 
     * Should return a list of all classes that were explicitly set 
     */
    virtual void InitExplicitlySetClasses(TArray<UClass*>& ExplicitlySetClasses);

    /** Handles static event binding for I.E. adding something to a dependant actor list */
    virtual void AssignStaticEvents();

    /**
     * Here we set the rules for different actor classes and how they should route.
     * Use the SetClassRule() function.  
     */
    virtual void InitClassRules();



    // ------------------------------
    // ReplicationGraph containers

    /** 
     *  In here we store all classes that should spatialize it's replication
     *  in line with the Grid Spatialization Node.
     */
    UPROPERTY()
    TArray<UClass*> SpatializedClasses;

    /** Classes that should not spatialize */
    UPROPERTY()
    TArray<UClass*> NonSpatializedClasses;

    /** Classes that should always be relevant to send net updates to connections */
    UPROPERTY()
    TArray<UClass*> AlwaysRelevantClasses;


    // ------------------------------
    // Replication Graph Nodes

    /**
     * This is probably the most important node in the replication graph.
     * 
     * This node determines if a connection can receive net updates from a connection
     * by carving the map up into grids and from there on determining if it's allowed to send updates.
     */
    UPROPERTY()
    UReplicationGraphNode_GridSpatialization2D* GridNode;

    /**
     * This node is used for routing actors that are always relevant to send network updates to all connection
     */
    UPROPERTY()
    UReplicationGraphNode_ActorList* AlwaysRelevantNode;

    /** 
     * Gets the always relevant node for a specific connection (player controller)
     * This node is used for actors that should always be relavent send net updates to a specific connection 
     */
    class UDAReplicationGraphNode_AlwaysRelevant_ForConnection* GetAlwaysRelevantNode(APlayerController* PlayerController);

    /** Map for handling level steaming */
    TMap<FName, FActorRepListRefView> AlwaysRelevantStreamingLevelActors;

protected:

    /** Maps a class to a given policy */
    void SetClassRule(UClass* InClass, EClassRepPolicy MappingPolicy);
    
    /** Gets the mapping policy for routing into the different nodes we should use for the given class */
    EClassRepPolicy GetMappingPolicy(const UClass* InClass);
    
    /** Gets if the given mapping is a spatialized mapping */
    FORCEINLINE bool IsSpatialized(EClassRepPolicy Mapping) const
    {
        return (Mapping >= EClassRepPolicy::Spatialize_Static);
    }

    /** Maps a class to a policy. Used to store which policies to apply to different classes. */
    TClassMap<EClassRepPolicy> ClassRepPolicies;

public:

    // ------------------------------
    // Replication Graph Debugging

#if WITH_GAMEPLAY_DEBUGGER
    void OnGameplayDebuggerOwnerChange(AGameplayDebuggerCategoryReplicator* Debugger, APlayerController* OldOwner);
#endif

    /** Prints debug information regarding the nodes  */
    void PrintNodePolicies();

protected:

    // ------------------------------
    // Replication Graph Settings

    float DestructInfoMaxDistance = 175.f;
    float GridCellSize = 10000.f;                       // Size of a cell in the UReplicationGraphNode_GridSpatialization2D GridNode
    float SpatialBiasX = -150000.f;                     // "Min X" for replication. Value is initial and the system will reset itself if actors appears outside of this
    float SpatialBiasY = -200000.f;                     // "Min Y" for replication. Value is initial and the system will reset itself if actors appears outside of this
    bool bDisableSpatialRebuilds = true;
    bool bDisplayClientLevelStreaming = false;
};

/** Cusomt Always Relevant For Connection Node to handle client level streaming actors */
UCLASS()
class UDAReplicationGraphNode_AlwaysRelevant_ForConnection : public UReplicationGraphNode_AlwaysRelevant_ForConnection
{
public:

    GENERATED_BODY()

    UDAReplicationGraphNode_AlwaysRelevant_ForConnection();

    // ~ begin UReplicationGraphNode_AlwaysRelevant_ForConnection implementation
    virtual void GatherActorListsForConnection(const FConnectionGatherActorListParameters& Params) override;
    // ~ end UReplicationGraphNode_AlwaysRelevant_ForConnection

    void OnClientLevelVisibilityAdd(FName LevelName, UWorld* StreamingWorld);
	  void OnClientLevelVisibilityRemove(FName LevelName);

	  void ResetGameWorldState();

#if WITH_GAMEPLAY_DEBUGGER
	  AGameplayDebuggerCategoryReplicator* GameplayDebugger = nullptr;
#endif

protected:

    // The level streaming actors that needs to be replicated
    TArray<FName, TInlineAllocator<64>> AlwaysRelevantStreamingLevels;
};
