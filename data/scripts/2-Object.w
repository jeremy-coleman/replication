// Base Object Interface

let object_GetPosition => (id) native object_GetPosition;
let object_GetScale => (id) native object_GetScale;
let object_GetRotation => (id) native object_GetRotation;
let object_GetVelocity => (id) native object_GetVelocity;
let object_GetSpawnTime => (id) native object_GetSpawnTime;

let object_SetModel => (id, m) native object_SetModel;
let object_SetPosition => (id, s) native object_SetPosition;
let object_SetVelocity => (id, s) native object_SetVelocity;
let object_SetRotation => (id, s) native object_SetRotation;
let object_SetScale => (id, s) native object_SetScale;
let object_SetAirFriction => (id, s) native object_SetAirFriction;

// Colliders
let object_GenerateOBBCollidersFromModel => (id) native object_GenerateOBBCollidersFromModel;
let object_AddSphereCollider => (id, offset, radius) native object_AddSphereCollider;

// Interface for Object
struct Object => (
    id
) [BaseType = "ScriptableObject"] {

    init => () {}

    GetPosition  => () ret object_GetPosition(this.id);
    GetScale     => () ret object_GetScale(this.id);
    GetRotation  => () ret object_GetRotation(this.id);
    GetVelocity  => () ret object_GetVelocity(this.id);
    GetSpawnTime => () ret object_GetSpawnTime(this.id);
    GetId        => () ret this.id;

    SetModel     => (m) ret object_SetModel(this.id, m);
    SetPosition  => (s) ret object_SetPosition(this.id, s);
    SetVelocity  => (v) ret object_SetVelocity(this.id, v);
    SetRotation  => (s) ret object_SetRotation(this.id, s);
    SetScale     => (s) ret object_SetScale(this.id, s);
    SetAirFriction => (s) ret object_SetAirFriction(this.id, s);

    GenerateOBBCollidersFromModel => () ret object_GenerateOBBCollidersFromModel(this.id);
    AddSphereCollider => (offset, radius) ret object_AddSphereCollider(this.id, offset, radius);

    // Init Function
    OnServerCreate => () {}
    OnClientCreate => () {}

    // Per Tick
    OnTick => (time) {}

    OnCollide => () {}
};

// Interface for Game
let PlayAudio => (audio, volume, positionOrId) native game_PlayAudio;
let DestroyObject => (id) native game_DestroyObject;
let GetUnitsInRange => (pos, scale) native game_GetUnitsInRange;
let CreateObject => (class) native game_CreateObject;
let CreateNativeObject => (class) native game_CreateNativeObject;
