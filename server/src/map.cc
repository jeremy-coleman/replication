#include "map.h"
#include "util.h"
#include "weapons/weapon.h"

static const int LOOT_PER_ZONE = 10;

std::unordered_map<std::string, size_t> LootTable = {
    { "AssaultRifleObject", 10 },
    { "PistolObject", 10 },
    { "AmmoObject", 50 },
    { "GrenadeThrower", 50 },
    // { "BowObject", 10 },
    { "ShotgunObject", 10 },
    { "MedkitObject", 30 },
    { "MachineGunObject", 10 },
    { "SubmachineGunObject", 10 }
};

void MapObject::InitializeMap() {
    // Get Loot Spawn Zones from Mesh
    LOG_INFO("Initializing Map - Spawning Loot");
    totalLootValue = 0;
    auto& classLookup = GetClassLookup();
    for (auto& pair : LootTable) {
        totalLootValue += pair.second;
        if (classLookup.find(pair.first) == classLookup.end()) {
            LOG_ERROR(pair.first << " in loot table not found in class table!");
            throw std::runtime_error("Class Table Mismatch");
        }
    }

    SpawnLoot(0);
}

bool IsAttachedWeapon(Game& game, ObjectID id) {
    WeaponObject* obj = dynamic_cast<WeaponObject*>(game.GetObject<Object>(id));
    return obj && obj->GetAttachedTo();
}

void MapObject::SpawnLoot(Time time) {
    lastLootSpawnTime = time;
    for (LootSpawnZone& zone : lootSpawnZones) {
        AABB& box = zone.spawnZone;
        for (auto it = zone.objects.begin(); it != zone.objects.end(); ) {
            ObjectID id = *it;
            if (!game.ObjectExists(id) || IsAttachedWeapon(game, id)) {
                it = zone.objects.erase(it);
            }
            else {
                it++;
            }
        }

        int total = LOOT_PER_ZONE - zone.objects.size();
        std::uniform_real_distribution<float> distribX(box.ptMin.x, box.ptMax.x);
        std::uniform_real_distribution<float> distribY(box.ptMin.y, box.ptMax.y);
        std::uniform_real_distribution<float> distribZ(box.ptMin.z, box.ptMax.z);

        for (int i = 0; i < total; i++) {
            Vector3 location {
                distribX(gen), distribY(gen), distribZ(gen),
            };
            Object* obj = LootSpawn();
            obj->SetPosition(location);
            game.AddObject(obj);
            zone.objects.insert(obj->GetId());
        }
    }
}

void MapObject::Tick(Time time) {
    // Server Only
// #ifdef BUILD_SERVER
//     if (time - lastLootSpawnTime > 60000) {
//         SpawnLoot(time);
//     }
// #endif
}

Object* MapObject::LootSpawn() {
    std::uniform_int_distribution<size_t> distrib(0, totalLootValue - 1);
    size_t i = distrib(gen);
    size_t accum = 0;
    auto& classLookup = GetClassLookup();
    for (auto& pair : LootTable) {
        accum += pair.second;
        if (i < accum) {
            Object* obj = classLookup[pair.first](game);
            return obj;
        }
    }
    LOG_ERROR("Generated too big of a value?? " << i << " " << totalLootValue << " " << accum);
    throw "What the heck";
}