#include "game.h"
#include "logging.h"
#include "global.h"

#include "util.h"
#include "object.h"
#include "scriptable-object.h"
#include "vector.h"
#include "objects/player.h"
#include "collision.h"
#include "map.h"

#include "json/json.hpp"

#include "static-mesh.h"
#include "objects/spectator-box.h"
#include "scene.h"

#include <fstream>
#include <exception>
#include <thread>


#ifdef BUILD_SERVER
const int TickInterval = 16;
// const int TickInterval = 100;
#endif
#ifdef BUILD_CLIENT
const int TickInterval = 16;
// const int TickInterval = 100;
#endif
const int ReplicateInterval = 100;

Vector3 liveBoxStart(-1000, -100, -1000);
Vector3 liveBoxSize(2000, 2000, 2000);

Game::Game() :
    nextId(1),
    relationshipManager(*this),
    scriptManager(this) {
    if (GlobalSettings.RunTests) return;

    #ifdef BUILD_SERVER
        if (GlobalSettings.IsProduction) {
            LOG_INFO("==== PRODUCTION MODE ====");
        }
        else {
            LOG_INFO("==== DEVELOPMENT MODE ====");
        }

        LoadMap(RESOURCE_PATH(GlobalSettings.MapPath));

        CreateMapBaseObject();
    #endif
}

void Game::CreateMapBaseObject() {
    // Models[0] is always the base map
    MapObject* map = new MapObject(*this);
    // Model* mapModel = GetModel(obj);
    // StaticMeshObject* baseMap = new StaticMeshObject(*this, "ShootingRange.obj");
    AddObject(map);

    std::vector<TransformedNode> sceneNodes;
    scene.FlattenHierarchy(sceneNodes, &scene.root);
    for (TransformedNode& transformed : sceneNodes) {
        Node* node = transformed.node;
        Object* obj = nullptr;
        StaticMeshObject* staticMesh = nullptr;

        if (StaticModelNode* staticModel = dynamic_cast<StaticModelNode*>(node)) {
            staticMesh = new StaticMeshObject(*this, staticModel->model->name);
            obj = staticMesh;

            for (auto& mesh : staticModel->model->meshes) {
                if (Contains(mesh->name, "lootzone")) {
                    LootSpawnZone zone;
                    zone.spawnZone = AABB::FromMesh(*mesh);
                    map->lootSpawnZones.push_back(zone);
                }
            }
        }
        else if (GameObjectNode* gameObject = dynamic_cast<GameObjectNode*>(node)) {
            auto& ClassLookup = GetClassLookup();
            if (ClassLookup.find(gameObject->gameObjectClass) == ClassLookup.end()) {
                LOG_ERROR("Class " << gameObject->gameObjectClass << " is not registered!");
                throw std::runtime_error("Class " + gameObject->gameObjectClass + " is not registered!");
            }
            if (gameObject->object) {
                obj = gameObject->object;
            }
            else {
                obj = ClassLookup[gameObject->gameObjectClass](*this);
            }
        }
        else if (ScriptableObjectNode* scriptObject = dynamic_cast<ScriptableObjectNode*>(node)) {
            obj = new ScriptableObject(*this, scriptObject->scriptClass);
        }

        // else if (LightNode* lightNode = dynamic_cast<LightNode*>(node)) {
        //     obj = new LightObject(*this, *lightNode);
        // }

        if (obj) {
            Vector3 position, scale, skew;
            Quaternion rotation;
            Vector4 perspective;
            glm::decompose(transformed.transform, scale, rotation, position, skew, perspective);

            obj->SetPosition(position);
            // LOG_DEBUG(rotation);
            obj->SetRotation(glm::inverse(rotation));
            obj->SetScale(scale);

            if (staticMesh) {
                GenerateStaticMeshCollidersFromModel(staticMesh);
            }
            AddObject(obj);
        }
    }
#ifdef BUILD_SERVER
    map->InitializeMap();
#endif
}

void Game::LoadMap(std::string mapPath) {
    LOG_INFO("Loading Map " << mapPath);

    scene.assetManager.LoadDataFromDirectory(scriptManager);

    scene.LoadFromFile(mapPath);

    // Calculate Hierarchy Transforms
    #ifdef BUILD_CLIENT
        std::vector<TransformedNode> sceneNodes;
        scene.FlattenHierarchy(sceneNodes, &scene.root);
        for (TransformedNode& transformed : sceneNodes) {
            if (LightNode* lightNode = dynamic_cast<LightNode*>(transformed.node)) {
                lightNodes.push_back(new TransformedLight(transformed));
            }
        }
    #endif

    // Collision Testing
    // if (!GlobalSettings.IsProduction) {
    //     auto obj1 = new SphereObject(*this);
    //     obj1->SetPosition(Vector3(20, 20, 20));
    //     obj1->SetIsStatic(true);

    //     auto obj2 = new BoxObject(*this);
    //     obj2->SetPosition(Vector3(25, 20, 25));
    //     // obj2->SetRotation(DirectionToQuaternion(Vector3(1, 1, 1)));
    //     obj2->SetIsStatic(true);
    //     AddObject(obj1);
    //     AddObject(obj2);
    // }

    AddObject(new SpectatorBox(*this));
    // LoadScriptedObject("TestObject");
}

Game::~Game() {
    for (auto& t : gameObjects) {
        delete t.second;
    }
}

#ifdef BUILD_CLIENT

PlayerObject* Game::GetLocalPlayer() {
    return GetObject<PlayerObject>(localPlayerId);
}

#endif

void Game::AssignParent(Object* child, Object* parent) {
    relationshipManager.SetParent(child->GetId(), parent->GetId());
}

void Game::DetachParent(Object* child) {
    relationshipManager.RemoveParent(child->GetId());
}

#ifdef BUILD_SERVER
bool Game::IsOnTickThread() {
    // Not set means we're on main thread, still initializing
    return !tickThreadIdSet || std::this_thread::get_id() == tickThreadId;
}

void Game::FlushNewObjects() {
    newObjectsMutex.lock();
    std::unordered_map<ObjectID, Object*> newObjectsCopy = newObjects;
    newObjects.clear();
    newObjectsMutex.unlock();

    for (auto& newObject : newObjectsCopy) {
        LOG_DEBUG("Flush New Object (" << (void*)newObject.second << ") " << newObject.second);
        gameObjects[newObject.first] = newObject.second;
        gameObjects[newObject.first]->OnCreate();
        RequestReplication(newObject.first);
    }
}

#endif

void Game::Tick(Time time) {
    gameTime = time;

#ifdef BUILD_SERVER

    tickThreadId = std::this_thread::get_id();
    tickThreadIdSet = true;

    queuedCallsMutex.lock();
    for (auto& call : queuedCalls) {
        call(*this);
    }
    queuedCalls.clear();
    queuedCallsMutex.unlock();

    // New objects get queued in from HandleReplication and EnsureObjectExists
    //   on the client, not through this set.
    FlushNewObjects();
#endif

    relationshipManager.Tick(time);

    // if (time % 1024 == 0) LOG_DEBUG("Average Object Tick Time: " << averageObjectTickTime.GetAverage());

#ifdef BUILD_SERVER
    for (auto& object : gameObjects) {
        // if (!IsZero(GetVelocity() - lastFrameVelocity)) {
        //     if (IsTagged(Tag::WEAPON)) {
        //         LOG_DEBUG(GetId() << ": Velocity " << GetVelocity() << " " << lastFrameVelocity);
        //     }
        //     SetDirty(true);
        // }

        if (!object.second->IsStatic() &&
            !IsPointInAABB(liveBoxStart, liveBoxSize, object.second->GetPosition()) &&
            !object.second->IsTagged(Tag::NO_KILLPLANE)) {
            // TODO: Deal damage instead of insta kill
            // You're out of the range
            LOG_INFO("Kill Planed: " << object.second << " " << object.second->GetPosition());

            DestroyObject(object.second->GetId());
        }
    }
#endif

    // OnDeath could potentially add more stuff to DeadObjects
    std::vector<ObjectID> deadObjectsThisTick;
    deadObjectsThisTick.insert(deadObjectsThisTick.begin(),
        deadObjects.begin(), deadObjects.end());
    deadObjects.clear();
    for (auto& objectId : deadObjectsThisTick) {
        if (gameObjects.find(objectId) != gameObjects.end()) {
            Object* object = gameObjects[objectId];
            LOG_DEBUG("Destroy Object (" << (void*)object << ") (" << objectId << ") " << object->GetClass());

            // Move an object into root before destroying
            DetachParent(object);
            object->OnDeath();
            gameObjects.erase(objectId);
        #ifdef BUILD_SERVER
            deadSinceLastReplicate.insert(objectId);
        #endif
            delete object;
        }
    }
#ifdef BUILD_SERVER
    Replicate(time);
    ReplicateAnimations(time);
#endif
}

#ifdef BUILD_SERVER
/* A Replication Packet:
    {
        event: "r",
        objs: [ list of replicated objects ],
        time: client timestamp of last input.
        ticks: ticks server has processed sicne that last input
    }
  A Animation packet:
    {
        event: "a",
        objs: [ list of animations ]
    }
*/
void Game::SendData(PlayerSocketData* player, std::string message) {
    player->eventLoop->defer([player, message] () {
        // LOG_DEBUG(message);
        if (!player->ws->send(message, uWS::OpCode::TEXT)) {
            LOG_ERROR("Could not send!");
        }
    });
}

void Game::ReplicateAnimations(Time time) {
    if (animationPackets.empty()) return;

    rapidjson::StringBuffer output;
    rapidjson::Writer<rapidjson::StringBuffer> writer(output);
    writer.StartObject();

    writer.Key("event");
    writer.String("a");
    writer.Key("objs");
    writer.StartArray();
    for (auto& animation : animationPackets) {
        animation->Serialize(writer);
        delete animation;
    }
    animationPackets.clear();
    writer.EndArray();
    writer.EndObject();

    std::string outputData { output.GetString() };
    for (auto& player : players) {
        /*for (auto& object : serialized) {
            player->ws->send(object.second, uWS::OpCode::TEXT);
        }*/
        if (!player->isReady) continue;
        if (!player->hasInitialReplication) continue;
        SendData(player, outputData);
    }
}

void Game::InitialReplication(PlayerSocketData* data) {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

    writer.StartObject();
    writer.Key("event");
    writer.String("r");
    writer.Key("time");
    writer.Int(0);
    writer.Key("ticks");
    writer.Int(0);

    writer.Key("game");
    writer.StartObject();
    Serialize(writer);
    writer.EndObject();

    writer.Key("objs");
    writer.StartArray();
    for (auto& object : gameObjects) {
        // All Objects
        writer.StartObject();
        object.second->Serialize(writer);
        writer.EndObject();
    }
    writer.EndArray();

    writer.EndObject();
    SendData(data, buffer.GetString());
}

void Game::RequestReplication(ObjectID objectId) {
    // Should replicate all parents too
    Object* obj = gameObjects[objectId];
    while (obj != nullptr) {
        replicateNextTick.insert(obj->GetId());
        obj = relationshipManager.GetParent(obj->GetId());
    }
}

void Game::QueueAllForReplication(Time time) {
    for (auto& object : gameObjects) {
        if (object.second->IsDirty()) {
            RequestReplication(object.first);
        }
    }
}

void Game::Replicate(Time time) {
    if (replicateNextTick.empty()) return;

    // LOG_DEBUG("Replicate (" << time << ") " << replicateNextTick.size() << " objects");

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    {
        writer.StartArray();

        for (auto& objectId : deadSinceLastReplicate) {
            writer.StartObject();
            writer.Key("id");
            writer.Uint(objectId);
            writer.Key("dead");
            writer.Bool(true);
            writer.EndObject();
        }

        deadSinceLastReplicate.clear();

        for (auto& objectId : replicateNextTick) {
            if (gameObjects.find(objectId) != gameObjects.end()) {
                gameObjects[objectId]->SetDirty(false);

                writer.StartObject();
                gameObjects[objectId]->Serialize(writer);
                writer.EndObject();
            }
        }
        replicateNextTick.clear();

        writer.EndArray();
    }

    rapidjson::StringBuffer gameBuffer;
    rapidjson::Writer<rapidjson::StringBuffer> gameWriter(gameBuffer);
    {
        gameWriter.StartObject();
        Serialize(gameWriter);
        gameWriter.EndObject();
    }

    // std::scoped_lock<std::mutex> lock(playersSetMutex);
    for (auto& player : players) {
        /*for (auto& object : serialized) {
            player->ws->send(object.second, uWS::OpCode::TEXT);
        }*/
        if (!player->isReady) continue;
        if (!player->hasInitialReplication) {
            LOG_DEBUG("Initial Replication");
            player->hasInitialReplication = true;
            InitialReplication(player);
        }
        else {
            rapidjson::StringBuffer output;
            rapidjson::Writer<rapidjson::StringBuffer> writer2(output);
            writer2.StartObject();

            writer2.Key("event");
            writer2.String("r");
            writer2.Key("time");
            writer2.Uint64(player->playerObject->lastClientInputTime);
            writer2.Key("ticks");
            writer2.Uint64(player->playerObject->ticksSinceLastProcessed);
            writer2.Key("objs");
            writer2.RawValue(buffer.GetString(), buffer.GetSize(), rapidjson::kArrayType);
            writer2.Key("game");
            writer2.RawValue(gameBuffer.GetString(), gameBuffer.GetSize(), rapidjson::kObjectType);

            writer2.EndObject();
            SendData(player, output.GetString());
        }
        if (player->playerObjectDirty) {
            if (player->playerObject->GetId() != 0) {
                player->playerObjectDirty = false;
            }
            rapidjson::StringBuffer output;
            rapidjson::Writer<rapidjson::StringBuffer> writer2(output);
            writer2.StartObject();
            writer2.Key("playerLocalObjectId");
            writer2.Uint(player->playerObject->GetId());
            writer2.EndObject();
            SendData(player, output.GetString());
        }
    }
}

#endif

#ifdef BUILD_CLIENT
void Game::RollbackTime(Time time) {
    gameTime = time;
    for (auto& object : gameObjects) {
        object.second->SetLastTickTime(time);
    }
}

void Game::EnsureObjectExists(json& object) {
    if (!object.HasMember("id")) {
        LOG_ERROR("EnsureObjectExists: no ID on replication packet!");
        throw std::runtime_error("EnsureObjectExists: no ID on replication packet!");
    }
    ObjectID id = object["id"].GetUint();
    if (object.HasMember("dead")) {
        // Kill
        if (gameObjects.find(id) != gameObjects.end()) {
            delete gameObjects[id];
            gameObjects.erase(id);
            return;
        }
        return;
    }
    if (!object["t"].IsString()) {
        LOG_ERROR("Tag t is not a string in packet!");
        throw std::runtime_error("Tag t is not a string in packet!");
    }
    std::string objectType (object["t"].GetString(), object["t"].GetStringLength());
    if (gameObjects.find(id) == gameObjects.end()) {
        LOG_DEBUG("Got new object (" << id << ") " << objectType);
        auto& ClassLookup = GetClassLookup();
        if (ClassLookup.find(objectType) == ClassLookup.end()) {
            LOG_ERROR("Class " << objectType << " is not registered!");
            throw std::runtime_error("Class " + objectType + " is not registered!");
        }
        Object* obj = GetClassLookup()[objectType](*this);
        obj->SetId(id);
        obj->createdThisFrameOnClient = true;
        gameObjects[id] = obj;
    }
}

void Game::ProcessReplicationForObject(json& object) {
    ObjectID id = object["id"].GetUint();
    if (object.HasMember("dead")) {
        return;
    }
    Object* obj = GetObject(id);
    if (obj == nullptr) {
        LOG_ERROR("Replicating on non-existant object!" << id);
        return;
    }
    obj->ProcessReplication(object);
    if (obj->createdThisFrameOnClient) {
        obj->OnClientCreate();
        obj->createdThisFrameOnClient = false;
    }
}

#endif

RayCastResult Game::RayCastInWorld(RayCastRequest request) {
    RayCastResult result;
    for (auto& object : gameObjects) {
        if (!object.second->IsTagged(request.inclusionTags)) {
            continue;
        }
        if (request.excludeObjects.find(object.first) != request.excludeObjects.end()) {
            continue;
        }
        RayCastResult tempResult;
        if (object.second->CollidesWith(request, tempResult)) {
            if (!result.isHit || tempResult.zDepth < result.zDepth) {
                result = tempResult;
            }
        }
    }
    return result;
}

void CollideBetween(Object* primary, Object* secondary, bool isGround,
        bool shouldExclude, bool shouldReportPrimary, bool shouldReportSecondary) {
    if (!isGround && shouldExclude && !shouldReportPrimary && !shouldReportSecondary) return;
    CollisionResult r = primary->CollidesWith(secondary);
    if (r.isColliding) {
        r.collidedWith = secondary;
        if (!shouldExclude) {
            primary->ResolveCollision(r.collisionDifference);
        }
        if (shouldReportPrimary) {
            primary->OnCollide(r);
        }
        if (shouldReportSecondary) {
            secondary->OnCollide(r);
        }
    }
}

void Game::HandleCollisions(Object* obj) {
    if (deadObjects.find(obj->GetId()) != deadObjects.end()) return;
    for (auto& object : gameObjects) {
        if (obj == object.second) continue;
        if (deadObjects.find(object.first) != deadObjects.end()) return;

        bool isGround = object.second->IsTagged(Tag::GROUND);
        if (!isGround && glm::distance(obj->GetPosition(), object.second->GetPosition()) > 10) {
            continue;
        }

        bool shouldExclude = obj->IsCollisionExcluded(object.second->GetTags()) ||
            object.second->IsCollisionExcluded(obj->GetTags());

        bool shouldReportPrimary = obj->ShouldReportCollision(object.second->GetTags());
        bool shouldReportSecondary = object.second->ShouldReportCollision(obj->GetTags());

        if (!isGround) {
            // Colliders are only convex
            CollideBetween(obj, object.second, isGround, shouldExclude, shouldReportPrimary, shouldReportSecondary);
        }
        else {
            // Do up to 3 collisions between concave static mesh
            Vector3 lastPosition = obj->GetPosition();
            for (size_t i = 0; i < 5; i++) {
                CollideBetween(obj, object.second, isGround, shouldExclude, shouldReportPrimary, shouldReportSecondary);
                if (IsZero(lastPosition - obj->GetPosition())) {
                    break;
                }
                lastPosition = obj->GetPosition();
            }
        }
    }
}

void Game::AddObject(Object* obj) {
    // Client does not do anything
#ifdef BUILD_SERVER
    ObjectID newId = RequestId();
    obj->SetId(newId);
    newObjectsMutex.lock();
    newObjects[newId] = obj;
    newObjectsMutex.unlock();
    // Immediately queue into main object system
    if (IsOnTickThread()) {
        FlushNewObjects();
    }
#endif
}

void Game::DestroyObject(ObjectID objectId) {
    // Let the client destroy an object (hopefully server responds back with correct)
    if (objectId == 0) {
        // Something has gone wrong
        LOG_ERROR("Tried to destroy object ID 0!");
        throw std::runtime_error("Invalid destroy of ID 0, probably a memory leak!");
    }
    if (gameObjects.find(objectId) == gameObjects.end()) {
        LOG_ERROR("Tried to queue destruction for object not exist " << objectId);
        return;
    }
    LOG_DEBUG("Queued for Destruction " << objectId);
    deadObjects.insert(objectId);
}


#ifdef BUILD_SERVER
void Game::OnPlayerDead(PlayerObject* playerObject) {
    std::scoped_lock<std::mutex> lock(playersSetMutex);
    for (auto& p : players) {
        if (p->playerObject == playerObject) {
            LOG_INFO("Found player! Respawning by setting to dirty!");
            // Found the player. Handle respawn mechanics here.
            p->playerObjectDirty = true;

            // Implement letting user select things
            PlayerObject* obj = nullptr;
            try {
                obj = dynamic_cast<PlayerObject*>(CreateScriptedObject(p->nextRespawnCharacter));
            }
            catch (...) {
                p->nextRespawnCharacter = "Archer";
                obj = dynamic_cast<PlayerObject*>(CreateScriptedObject(p->nextRespawnCharacter));
            }
            obj->SetPosition(RESPAWN_LOCATION);

            static_cast<PlayerObject*>(obj)->lastClientInputTime = playerObject->lastClientInputTime;
            static_cast<PlayerObject*>(obj)->ticksSinceLastProcessed = playerObject->ticksSinceLastProcessed;

            p->playerObject = static_cast<PlayerObject*>(obj);

            QueueNextTick([obj](Game& game) {
                game.AddObject(obj);
                LOG_INFO("Respawn Player! New ID " << obj->GetId());
            });
            return;
        }
    }
}
#endif

ObjectID Game::RequestId() {
    return nextId++;
}

#ifdef BUILD_SERVER
void Game::AddPlayer(PlayerSocketData* data, PlayerObject* playerObject) {
    std::scoped_lock<std::mutex> lock(playersSetMutex);
    players.insert(data);

    QueueNextTick([playerObject](Game& game) {
        game.AddObject(playerObject);
    });
}

void Game::RemovePlayer(PlayerSocketData* data) {
    std::scoped_lock<std::mutex> lock(playersSetMutex);
    players.erase(data);

    PlayerObject* playerObject = data->playerObject;
    LOG_INFO("Removing player " << playerObject);
    QueueNextTick([playerObject](Game& game) {
        game.DestroyObject(playerObject->GetId());
    });
}
#endif

void Game::GetUnitsInRange(const Vector3& position, float range,
    std::vector<RangeQueryResult>& results) {
    for (auto& pair : gameObjects) {
        Object* obj = pair.second;
        double actualRange = glm::distance(position, obj->GetPosition());
        if (actualRange < range) {
            results.emplace_back(obj, actualRange);
        }
    }
}

bool Game::CheckLineSegmentCollide(const Vector3& start,
    const Vector3& end, uint64_t includeTags) {
    for (auto& object : gameObjects) {
        if (((uint64_t)object.second->GetTags() & includeTags) != 0) {
            bool r = object.second->CollidesWith(start, end);
            if (r) {
                return true;
            }
        }
    }
    return false;
}

void Game::PlayAudio(const std::string& audio, float volume, const Vector3& position) {
    #ifdef BUILD_CLIENT
        LOG_DEBUG("Playing audio " << audio);
        audioRequests.emplace_back(GetAssetManager().GetAudio(audio), volume, position);
    #endif
}

void Game::PlayAudio(const std::string& audio, float volume, Object* boundObject) {
    #ifdef BUILD_CLIENT
        LOG_DEBUG("Playing audio " << audio);
        audioRequests.emplace_back(GetAssetManager().GetAudio(audio), volume, boundObject->GetId());
    #endif
}

Object* Game::CreateScriptedObject(const std::string& className) {
    // Get Base Type Name
    std::string baseType = scriptManager.GetBaseTypeFromScriptingType(className);

    LOG_DEBUG("BaseType Queried: " << className << "->" << baseType);

    auto& ClassLookup = GetClassLookup();
    if (ClassLookup.find(baseType) == ClassLookup.end()) {
        LOG_ERROR("Class " << baseType << " is not registered!");
        throw "Class " + baseType + " is not registered!";
    }

    ScriptableObject* obj = dynamic_cast<ScriptableObject*>(
        ClassLookup[baseType](*this));

    if (!obj) {
        LOG_ERROR("Class " << baseType << " is not a ScriptableObject!");
        throw "Class " + baseType + " is not a ScriptableObject!";
    }
    obj->className = className;
    return obj;
}

Object* Game::CreateAndAddScriptedObject(const std::string& className) {
    Object* obj = CreateScriptedObject(className);
    AddObject(obj);
    return obj;
}