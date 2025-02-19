#include "relationship-manager.h"
#include "game.h"

#include <queue>

RelationshipManager::RelationshipManager(Game& game) : game(game) {}

void RelationshipManager::RemoveParent(ObjectID child) {
    parentChildren[childParent[child]].erase(child);
    childParent.erase(child);
    isDirty = true;
}

void RelationshipManager::SetParent(ObjectID child, ObjectID parent) {
    if (parent == child) {
        return;
    }
    if (childParent.find(child) != childParent.end()) {
        // Already have a parent
        RemoveParent(child);
    }
    childParent[child] = parent;
    parentChildren[parent].insert(child);
    isDirty = true;
}

Object* RelationshipManager::GetParent(ObjectID child) {
    if (childParent.find(child) == childParent.end()) {
        return nullptr;
    }
    Object* parent = game.GetObject(childParent[child]);
    if (!parent) {
        RemoveParent(child);
        return nullptr;
    }
    return parent;
}

std::unordered_set<Object*> RelationshipManager::GetChildren(ObjectID parent) {
    if (parentChildren.find(parent) == parentChildren.end()) {
        return {};
    }
    std::unordered_set<Object*> children;
    for (ObjectID child : parentChildren[parent]) {
        Object* childObj = game.GetObject(child);
        if (!childObj) {
            RemoveParent(child);
        }
        else {
            children.insert(childObj);
        }
    }
    return children;
}

#ifdef BUILD_CLIENT
    void RelationshipManager::PreDraw(Time time) {
        Execute([](Object* obj, Time time) {
            obj->PreDraw(time);
        }, time);
    }
#endif

void RelationshipManager::Tick(Time time) {
    Execute([](Object* obj, Time time) {
        obj->Tick(time);
    }, time);
}

void RelationshipManager::Execute(std::function<void(Object*, Time)> func, Time time) {
    std::queue<ObjectID> queue;

    for (auto& object : game.GetGameObjects()) {
        if (GetParent(object.first)) continue;
        queue.push(object.first);
    }

    while (!queue.empty()) {
        ObjectID object = queue.front();
        queue.pop();

        Time start = Timer::NowMicro();
        if (Object* obj = game.GetObject(object)) {
            func(obj, time);
        }
        else {
            LOG_ERROR("Object " << object << " got added to queue but did not find it in GameObjects!");
        }

        Time end = Timer::NowMicro();
        game.averageObjectTickTime.InsertValue(end - start);

        for (auto& child : parentChildren[object]) {
            if (!game.GetObject(child)) {
                RemoveParent(child);
                continue;
            }
            queue.push(child);
        }
    }
    auto it = parentChildren.begin();
    while (it != parentChildren.end()) {
        if (it->second.empty()) {
            it = parentChildren.erase(it);
            isDirty = true;
        }
        else {
            it++;
        }
    }
}

void RelationshipManager::Serialize(JSONWriter& obj) {
    Replicable::Serialize(obj);
    obj.Key("r");
    obj.StartArray();
    for (auto& pair : childParent) {
        obj.Uint64(pair.first);
        obj.Uint64(pair.second);
    }
    obj.EndArray();
    // PrintDebug();
}

void RelationshipManager::ProcessReplication(json& obj) {
    Replicable::ProcessReplication(obj);
    childParent.clear();
    parentChildren.clear();

    ObjectID child = 0;
    for (auto& entry : obj["r"].GetArray()) {
        if (child == 0) {
            child = entry.GetUint64();
        }
        else {
            SetParent(child, entry.GetUint64());
            child = 0;
        }
    }
    // PrintDebug();
}

bool RelationshipManager::IsDirty() const {
    return isDirty;
}

void RelationshipManager::ResetDirty() {
    isDirty = false;
}

void RelationshipManager::PrintDebug() {
    LOG_DEBUG("RelationshipManager:");
    for (auto& pair : childParent) {
        LOG_DEBUG("\tC" << pair.first << " -> P" << pair.second);
    }
}