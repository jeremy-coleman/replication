#include "player.h"
#include "collision.h"
#include "game.h"
#include "logging.h"
#include "floating-text.h"
#include "util.h"
#include "weapons/gun.h"

static const int LEFT_MOUSE_BUTTON = 1;
static const int RIGHT_MOUSE_BUTTON = 3;
static const int A_KEY = 65;
static const int S_KEY = 83;
static const int D_KEY = 68;
static const int F_KEY = 70;
static const int V_KEY = 86;
static const int R_KEY = 82;
static const int G_KEY = 71;
static const int Q_KEY = 81;
static const int Z_KEY = 90;
static const int W_KEY = 87;
static const int K_KEY = 75;
static const int SPACE_KEY = 32;
static const int D1_KEY = 49;
static const int D2_KEY = 50;
static const int D3_KEY = 51;
static const int D4_KEY = 52;
static const int E_KEY = 69;

static const int LEFT_ARROW_KEY = 37;
static const int UP_ARROW_KEY = 38;
static const int RIGHT_ARROW_KEY = 39;
static const int DOWN_ARROW_KEY = 40;

static const int WEAPON_PICKUP_RANGE = 3.0f;

std::unordered_map<int, size_t> KEY_MAP = {
    { D_KEY, 0 },
    { S_KEY, 1 },
    { A_KEY, 2 },
    { F_KEY, 3 },
    { G_KEY, 4 },
    { R_KEY, 5 },
    { Q_KEY, 6 },
    { Z_KEY, 7 },
    { W_KEY, 8 },
    { SPACE_KEY, 9 },
    { K_KEY, 10 },
    { V_KEY, 11 },
    { D1_KEY, 12 },
    { D2_KEY, 13 },
    { D3_KEY, 14 },
    { D4_KEY, 15 },
    { E_KEY, 16 },
    { LEFT_ARROW_KEY, 17 },
    { UP_ARROW_KEY, 18 },
    { RIGHT_ARROW_KEY, 19 },
    { DOWN_ARROW_KEY, 20 },
};

PlayerObject::PlayerObject(Game& game) : PlayerObject(game, Vector3()) {
}

PlayerObject::PlayerObject(Game& game, Vector3 position) : ScriptableObject(game),
    inventoryManager(game, this) {
    SetTag(Tag::PLAYER);

    // SetTag(Tag::NO_GRAVITY);

    SetPosition(position);

    // collisionExclusion |= (uint64_t) Tag::PLAYER;

    SetModel(game.GetModel("NewPlayer2.obj"));
    // GenerateOBBCollidersFromModel(this);
    AddCollider(new CapsuleCollider(this, Vector3(0, 0, 0), Vector3(0, -1.35, 0), 0.5f));
    // AddCollider(new SphereCollider(this, Vector3(0, 0, 0), 0.25f));
    // AddCollider(new OBBCollider(this, Vector3(-0.375, -1.6, -0.25), Vector3(0.75, 1.30, 0.5)));
    SetScale(Vector3(1, 1, 1));
}

void PlayerObject::OnDeath() {
    ScriptableObject::OnDeath();
    // This calls before you get destructed, but client will already know you're
    //   dead (but you don't actually get GCed until next tick)
    LOG_DEBUG("Player Death " << GetCurrentWeapon());

#ifdef BUILD_SERVER
    inventoryManager.ClearInventory();
    if (qWeapon) {
        if (auto weapon = qWeapon.Get(game)) {
            game.DestroyObject(weapon->GetId());
            weapon->Detach();
        }
        qWeapon.Reset();
    }
    if (zWeapon) {
        if (auto weapon = zWeapon.Get(game)) {
            game.DestroyObject(weapon->GetId());
            weapon->Detach();
        }
        zWeapon.Reset();
    }

    // Notify game so it can replace a new character
    game.OnPlayerDead(this);
#endif
}

PlayerObject::~PlayerObject() {

}

void PlayerObject::PickupWeapon(WeaponObject* weapon) {
    if (inventoryManager.CanPickup(weapon) && canPickup) {
        inventoryManager.Pickup(weapon);
    }
}

void PlayerObject::InventoryDrop(int id) {
    inventoryManager.Drop(id);
}

void PlayerObject::InventorySwap() {
    inventoryManager.Swap();
}

ObjectReference<WeaponObject> PlayerObject::ScanPotentialWeapon() {
    // LOG_DEBUG("Scan Weapon");
    RayCastRequest castRay;
    castRay.startPoint = GetPosition() + GetLookDirection();
    castRay.direction = GetLookDirection();
    castRay.inclusionTags = (uint64_t)Tag::WEAPON;
    RayCastResult result = game.RayCastInWorld(castRay);
    if (result.isHit && result.zDepth < WEAPON_PICKUP_RANGE) {
        if (WeaponObject* potentialWeapon = dynamic_cast<WeaponObject*>(result.hitObject)) {
            return potentialWeapon->GetId();
        }
        else {
            LOG_ERROR("Scan Potential Weapon for Weapon only but did not hit weapon");
        }
    }
    return {};
}

void PlayerObject::TryPickupItem() {
    if (auto potentialWeapon = ScanPotentialWeapon()) {
        if (auto weapon = potentialWeapon.Get(game)) {
            PickupWeapon(weapon);
        }
    }
}

void PlayerObject::Tick(Time time) {
    {
        #ifdef BUILD_SERVER
            Time clientTime = lastClientInputTime + (ticksSinceLastProcessed * TickInterval);
        #else
            Time clientTime = time;
        #endif
        // if (glm::length(GetVelocity()) > 1) {
        //     // LOG_DEBUG(clientTime << ": " << GetPosition() << " " << GetVelocity());
        //     LOG_DEBUG(clientTime << ": " << GetPosition().y);
        // }
        // LOG_DEBUG("Input Vel " << inputVelocity);
        // LOG_DEBUG("Regular Vel " << velocity);
        std::scoped_lock lock(socketDataMutex);

        while (inputBuffer.size() > 0) {
            JSONDocument& front = inputBuffer.front();
            Time firstTime = front["time"].GetUint();
            // LOG_DEBUG("First " << firstTime << " client Time " << clientTime);
            if (firstTime == clientTime) {
                ProcessInputData(front);
                inputBuffer.pop();
            }
            else if (firstTime < clientTime) {
                // LOG_DEBUG("Input in the past! " << firstTime << " < " << clientTime);
                ProcessInputData(front);
                inputBuffer.pop();
            }
            else {
                break;
            }
        }
    }
    Vector3 leftRightComponent;
    Vector3 forwardBackwardComponent;
    Vector3 upDownComponent;

    bool hasMovement = false;

    if (keyboardState[KEY_MAP[A_KEY]]) {
        leftRightComponent = Vector::Left * rotation;
        hasMovement = true;
    }
    if (keyboardState[KEY_MAP[D_KEY]]) {
        leftRightComponent = -Vector::Left * rotation;
        hasMovement = true;
    }
    if (keyboardState[KEY_MAP[W_KEY]]) {
        forwardBackwardComponent = Vector::Forward * rotation;
        hasMovement = true;
    }
    if (keyboardState[KEY_MAP[S_KEY]]) {
        forwardBackwardComponent = -Vector::Forward * rotation;
        hasMovement = true;
    }
    if (keyboardState[KEY_MAP[E_KEY]] &&
        !lastKeyboardState[KEY_MAP[E_KEY]]) {
        TryPickupItem();
    }
    if (keyboardState[KEY_MAP[LEFT_ARROW_KEY]]) {
        rotationYaw -= 1.f;
    }
    if (keyboardState[KEY_MAP[RIGHT_ARROW_KEY]]) {
        rotationYaw += 1.f;
    }
    if (keyboardState[KEY_MAP[UP_ARROW_KEY]]) {
        rotationPitch += 1.f;
    }
    if (keyboardState[KEY_MAP[DOWN_ARROW_KEY]]) {
        rotationPitch -= 1.f;
    }

    if (IsTagged(Tag::NO_GRAVITY)) {
        if (keyboardState[KEY_MAP[F_KEY]]) {
            upDownComponent = Vector::Up;
            hasMovement = true;
        }
        if (keyboardState[KEY_MAP[V_KEY]]) {
            upDownComponent = -Vector::Up;
            hasMovement = true;
        }
    }

    // TODO: move speed

    float moveSpeed = IsTagged(Tag::NO_GRAVITY) ? 20.0f : 8.0f;
    if (WeaponObject* currWeapon = GetCurrentWeapon()) {
        if (GunBase* gun = dynamic_cast<GunBase*>(currWeapon)) {
            if (gun->isADS) {
                moveSpeed /= 2.0f;
            }
        }
    }

    if (hasMovement) {
        if (!IsTagged(Tag::NO_GRAVITY)) {
            leftRightComponent.y = 0;
            forwardBackwardComponent.y = 0;
        }
        inputAcceleration = glm::normalize(leftRightComponent +
            forwardBackwardComponent + upDownComponent) * moveSpeed * 20.0f;
    }
    else {
        inputAcceleration = Vector3(0);
        // inputVelocity *= 0.8;
    }

    Time delta = time - lastTickTime;
    float timeFactor = delta / 1000.0;
    Vector3 velocityDelta = inputAcceleration * timeFactor;
    inputVelocity += velocityDelta;

    float friction = (moveSpeed * 7.0) * timeFactor;
    if (glm::length(inputVelocity) > friction) {
        inputVelocity -= glm::normalize(inputVelocity) * friction;
    }
    else {
        inputVelocity = Vector3(0);
    }

    if (glm::length(inputVelocity) >= moveSpeed) {
        inputVelocity = glm::normalize(inputVelocity) * moveSpeed;
    }

    if (keyboardState[KEY_MAP[K_KEY]]) {
        if (!lastKeyboardState[KEY_MAP[K_KEY]]) {
            DealDamage(50.0f, GetId());
        }
    }
    if (keyboardState[KEY_MAP[D1_KEY]]) {
        inventoryManager.EquipPrimary();
    }
    if (keyboardState[KEY_MAP[D2_KEY]]) {
        inventoryManager.EquipSecondary();
    }
    if (keyboardState[KEY_MAP[D3_KEY]]) {
        inventoryManager.HolsterAll();
    }
    if (keyboardState[KEY_MAP[G_KEY]]) {
        inventoryManager.EquipGrenade();
    }
    if (keyboardState[KEY_MAP[D4_KEY]]) {
        inventoryManager.EquipMedicalSupplies();
    }
    if (mouseWheelDelta < 0) {
        inventoryManager.EquipPrevious();
        mouseWheelDelta = 0;
    }
    if (mouseWheelDelta > 0) {
        inventoryManager.EquipNext();
        mouseWheelDelta = 0;
    }
    if (keyboardState[KEY_MAP[SPACE_KEY]]) {
        // Can only jump if touching ground
        if (IsGrounded()) {
            // LOG_DEBUG("Applying Jump");
            velocity.y = 10;
        }
        // velocity.y = -300;
    }

    canPickup = time - lastPickupTime > 500;
    WeaponObject* currentWeapon = inventoryManager.GetCurrentWeapon();
    if (currentWeapon) {
        if (mouseState[LEFT_MOUSE_BUTTON]) {
            if (!lastMouseState[LEFT_MOUSE_BUTTON]) {
                currentWeapon->StartFire(time);
            }
            currentWeapon->Fire(time);
        }
        else if (lastMouseState[LEFT_MOUSE_BUTTON]) {
            currentWeapon->ReleaseFire(time);
        }
        if (keyboardState[KEY_MAP[R_KEY]]) {
            if (!lastKeyboardState[KEY_MAP[R_KEY]]) {
                currentWeapon->StartReload(time);
            }
        }

        if (mouseState[RIGHT_MOUSE_BUTTON]) {
            if (!lastMouseState[RIGHT_MOUSE_BUTTON]) {
                currentWeapon->StartAlternateFire(time);
            }
        }
        else if (lastMouseState[RIGHT_MOUSE_BUTTON]) {
            currentWeapon->ReleaseAlternateFire(time);
        }
    }

    if (auto weapon = qWeapon.Get(game)) {
        if (keyboardState[KEY_MAP[Q_KEY]]) {
            if (!lastKeyboardState[KEY_MAP[Q_KEY]]) {
                weapon->StartFire(time);
            }
            weapon->Fire(time);
        }
        else if (lastKeyboardState[KEY_MAP[Q_KEY]]) {
            weapon->ReleaseFire(time);
        }
    }

    if (auto weapon = zWeapon.Get(game)) {
        if (keyboardState[KEY_MAP[Z_KEY]]) {
            if (!lastKeyboardState[KEY_MAP[Z_KEY]]) {
                weapon->StartFire(time);
            }
            weapon->Fire(time);
        }
        else if (lastKeyboardState[KEY_MAP[Z_KEY]]) {
            weapon->ReleaseFire(time);
        }
    }

    rotationPitch += pitchYawVelocity.x;
    rotationYaw += pitchYawVelocity.y;
    rotationPitch = std::fmod(rotationPitch, 360);
    rotationYaw = std::fmod(rotationYaw, 360);

    pitchYawVelocity *= 0.8;

    Matrix4 matrix;
    matrix = glm::rotate(matrix, glm::radians(rotationYaw), Vector::Up);
    // matrix = glm::rotate(matrix, glm::radians(rotationPitch), Vector3(matrix[0][0], matrix[1][0], matrix[2][0]));
    rotation = glm::quat_cast(matrix);

#ifdef BUILD_CLIENT
    auto obj = ScanPotentialWeapon();
    pointedToObject = obj;
#endif

    ScriptableObject::Tick(time);

    lastMouseState = mouseState;
    lastKeyboardState = keyboardState;
    ticksSinceLastProcessed += 1;
    isDirty = true;
    // LOG_DEBUG("Position: " << position << " Velocity: " << GetVelocity());
}

#ifdef BUILD_CLIENT
void PlayerObject::PreDraw(Time now) {
    // clientRotationYaw = rotationYaw;
    // clientRotationPitch = rotationPitch;

    float lerpRatio = GetClientInterpolationRatio(now);
    // LOG_DEBUG("ClientRotationYaw " << clientRotationYaw << " rotationYaw " << rotationYaw << " lerpRatio " << lerpRatio);
    clientRotationYaw = AngleLerpDegrees(clientRotationYaw, rotationYaw, lerpRatio);
    clientRotationPitch = AngleLerpDegrees(clientRotationPitch, rotationPitch, lerpRatio);

    ScriptableObject::PreDraw(now);
}
#endif
void PlayerObject::Serialize(JSONWriter& obj) {
    // LOG_DEBUG("Player Object Serialize - Start");
    ScriptableObject::Serialize(obj);

    if (qWeapon) {
        obj.Key("wq");
        obj.Uint(qWeapon.GetId());
    }
    if (zWeapon) {
        obj.Key("wz");
        obj.Uint(zWeapon.GetId());
    }
    obj.Key("kb");
    obj.StartArray();
    for (const bool &kb : keyboardState) {
        obj.Bool(kb);
    }
    obj.EndArray();

    obj.Key("lkb");
    obj.StartArray();
    for (const bool &kb : lastKeyboardState) {
        obj.Bool(kb);
    }
    obj.EndArray();

    obj.Key("ms");
    obj.StartArray();
    for (const bool &ms : mouseState) {
        obj.Bool(ms);
    }
    obj.EndArray();

    obj.Key("lms");
    obj.StartArray();
    for (const bool &ms : lastMouseState) {
        obj.Bool(ms);
    }
    obj.EndArray();

#ifdef BUILD_CLIENT
    obj.Key("client_p");
    Vector3 cp = GetClientPosition();
    SerializeDispatch(cp, obj);

    obj.Key("ld");
    Vector3 ld = GetLookDirection();
    SerializeDispatch(ld, obj);

    if (WeaponObject* currentWeapon = GetCurrentWeapon()) {
        obj.Key("w");
        obj.Uint(currentWeapon->GetId());
    }
#endif

    // LOG_DEBUG("Player Object Serialize - End");
}

void PlayerObject::ProcessReplication(json& obj) {
    ScriptableObject::ProcessReplication(obj);
    {
        std::scoped_lock lock(socketDataMutex);
        while (!inputBuffer.empty()) {
            inputBuffer.pop();
        }
    }

    if (obj.HasMember("wq")) {
        qWeapon = obj["wq"].GetUint();
    }
    else {
        qWeapon.Reset();
    }
    if (obj.HasMember("wz")) {
        zWeapon = obj["wz"].GetUint();
    }
    else {
        zWeapon.Reset();
    }

    size_t i = 0;
    for (const json &kb : obj["kb"].GetArray()) {
        keyboardState[i] = kb.GetBool();
        i++;
    }
    i = 0;
    for (const json &kb : obj["lkb"].GetArray()) {
        lastKeyboardState[i] = kb.GetBool();
        i++;
    }
    i = 0;
    for (const json &ms : obj["ms"].GetArray()) {
        mouseState[i] = ms.GetBool();
        i++;
    }
    i = 0;
    for (const json &ms : obj["lms"].GetArray()) {
        lastMouseState[i] = ms.GetBool();
        i++;
    }
}

void PlayerObject::DropWeapon(WeaponObject* weapon)  {
    inventoryManager.Drop(weapon);
}

void PlayerObject::HolsterAllWeapons() {
    inventoryManager.HolsterAll();
}

void PlayerObject::DealDamage(float damage, ObjectID from) {
    game.PlayAudio("ueh.wav", 1.0f, this);
#ifdef BUILD_SERVER
    health -= damage;
    game.QueueAnimation(new FloatingTextAnimation(from,
        GetPosition(), std::to_string(damage), "red"));
    if (health <= 0) {
        ObjectID id = GetId();
        game.DestroyObject(id);
    }
#endif
    OnTakeDamage(damage);
}

void PlayerObject::HealFor(int damage) {
    #ifdef BUILD_SERVER
        health += damage;
        if (health > 100) health = 100;
    #endif
}

void PlayerObject::OnInput(const JSONDocument& obj) {
    std::scoped_lock lock(socketDataMutex);
    inputBuffer.emplace();
    inputBuffer.back().CopyFrom(obj, inputBuffer.back().GetAllocator());
}

void PlayerObject::ProcessInputData(const JSONDocument& obj) {
    // if (obj["event"] != "mm") {
    //     // Happens too often!
    //     LOG_DEBUG("Process Input Data: [" << obj["time"].GetUint64()
    //                 << "] " << obj["event"].GetString());
    // }
    if (obj["event"] == "ku") {
        int key = obj["key"].GetInt();
        if (KEY_MAP.find(key) != KEY_MAP.end()) {
            keyboardState[KEY_MAP[key]] = false;
        }
    }
    else if (obj["event"] == "kd") {
        int key = obj["key"].GetInt();
        if (KEY_MAP.find(key) != KEY_MAP.end()) {
            keyboardState[KEY_MAP[key]] = true;
        }
    }
    else if (obj["event"] == "mm") {
        double moveX = obj["x"].GetDouble() / 10;
        double moveY = obj["y"].GetDouble() / 10;
        rotationYaw += playerSettings.sensitivity * moveX;
        rotationPitch -= playerSettings.sensitivity * moveY;
        rotationPitch = glm::clamp(rotationPitch, -89.f, 89.f);
    }
    else if (obj["event"] == "md") {
        int button = obj["button"].GetInt();
        if (button >= 0 && button < 5) {
            mouseState[button] = true;
        }
    }
    else if (obj["event"] == "mu") {
        int button = obj["button"].GetInt();
        if (button >= 0 && button < 5) {
            mouseState[button] = false;
        }
    }
    else if (obj["event"] == "mw") {
        mouseWheelDelta = obj["y"].GetInt();
    }
    else if (obj["event"] == "inventoryDrop") {
        InventoryDrop(obj["id"].GetInt());
    }
    else if (obj["event"] == "inventorySwap") {
        InventorySwap();
    }

    #ifdef BUILD_SERVER
        // LOG_DEBUG("Setting last client input time: " << obj["time"]);
        // TODO: assert this is monotonically growing
        lastClientInputTime = obj["time"].GetUint64();
        ticksSinceLastProcessed = 0;
    #endif
}

Vector3 PlayerObject::GetRightAttachmentPoint() const {
    #ifdef BUILD_SERVER
        return GetPosition()
            - glm::normalize(Vector::Left * GetRotation()) * 0.5f
            + GetLookDirection() * 0.5f;
    #endif
    #ifdef BUILD_CLIENT
        return GetClientPosition()
            - glm::normalize(Vector::Left * GetClientRotation()) * 0.5f
            + GetClientLookDirection() * 0.5f;
    #endif
}

Vector3 PlayerObject::GetLeftAttachmentPoint() const {
    #ifdef BUILD_SERVER
        return GetPosition()
            + glm::normalize(Vector::Left * GetRotation()) * 0.5f
            + GetLookDirection() * 0.5f;
    #endif

    #ifdef BUILD_CLIENT
        return GetClientPosition()
            + glm::normalize(Vector::Left * GetClientRotation()) * 0.5f
            + GetClientLookDirection() * 0.5f;
    #endif
}

Vector3 PlayerObject::GetCenterAttachmentPoint() const {
    #ifdef BUILD_SERVER
        return GetPosition() + GetLookDirection() * 0.5f;
    #endif

    #ifdef BUILD_CLIENT
        return GetClientPosition() + GetClientLookDirection() * 0.5f;
    #endif
}


Vector3 PlayerObject::GetAttachmentPoint(WeaponAttachmentPoint attachmentPoint) const {
    if (attachmentPoint == WeaponAttachmentPoint::LEFT)
        return GetLeftAttachmentPoint();
    else if (attachmentPoint == WeaponAttachmentPoint::RIGHT)
        return GetRightAttachmentPoint();
    else
        return GetCenterAttachmentPoint();
}