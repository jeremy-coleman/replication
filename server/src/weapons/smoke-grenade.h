#pragma once

#include "input-hold-thrower.h"

class SmokeGrenadeObject : public ThrownProjectile {

#ifdef BUILD_SERVER
    bool exploded = false;
#endif

public:
    CLASS_CREATE(SmokeGrenadeObject)

    SmokeGrenadeObject(Game& game) : SmokeGrenadeObject(game, 0) {}
    SmokeGrenadeObject(Game& game, ObjectID playerId);

    void Explode();

    virtual void OnCollide(CollisionResult& result) override;
};

CLASS_REGISTER(SmokeGrenadeObject);
