#include "scripting.h"
#include "object.h"
#include "game.h"
#include "player.h"
#include "weapons/weapon.h"
#include "scripting-interface.h"
#include "script-manager.h"

#include <fstream>


// Contains a lot of internal interfacing with WendyScript's VM Runtime
//   If anything changes there things might break here.

// These have to be globals because the native calls
//   bound to the WendyVM has no context
struct vm* ScriptManager::vm = nullptr;
Game* ScriptManager::game = nullptr;

void Script::LoadAndCompile(const std::string& path) {
    LOG_INFO("Loading " << path);
    const char* file_name = path.c_str();
    FILE* file = fopen(file_name, "r");
    if (!file) {
        LOG_ERROR("Could not open file " << path);
        return;
    }
    fseek(file, 0, SEEK_END);
	size_t length = ftell(file);
	fseek (file, 0, SEEK_SET);

    init_source(file, file_name, length, true);
    // Text Source
    char* buffer = get_source_buffer();
    // Begin Processing the File
    size_t alloc_size = 0;
    struct token* tokens;
    size_t tokens_count;

    // Scanning and Tokenizing
    tokens_count = scan_tokens(buffer, &tokens, &alloc_size);

    // Build AST
    struct statement_list* ast = generate_ast(tokens, tokens_count);
    // Generate Bytecode, No WendyHeader
    // TODO: this shouldn't depend on this, generate_code should accept an argument
    bytecode = generate_code(ast, &size, false);
    free_token_list(tokens, tokens_count);
    free_ast(ast);
    free_source();
    fclose(file);
    LOG_INFO("Compiled " << path);
}

Script::~Script() {
    if (bytecode) {
        safe_free(bytecode);
        bytecode = nullptr;
    }
}


void ScriptInstance::InitializeInstance(const std::string& className, ObjectID id) {
    // Load VM up to create an instance
    this->className = className;

    // Load struct metaclass
    struct data* value = get_address_of_id(ScriptManager::vm->memory, className.c_str(), true, NULL);
    if (!value) {
        LOG_ERROR("Class " << className << " not found!");
        return;
    }
    push_arg(ScriptManager::vm->memory, make_data(D_END_OF_ARGUMENTS, data_value_num(0)));
    push_arg(ScriptManager::vm->memory, copy_data(*value));
    vm_run_instruction(ScriptManager::vm, OP_CALL);
    vm_run(ScriptManager::vm);
    classInstance = pop_arg(ScriptManager::vm->memory, ScriptManager::vm->line);
    if (classInstance.type != D_STRUCT_INSTANCE) {
        LOG_ERROR("Could not initialize script instance, not D_STRUCT_INSTANCE");
        throw "Could not initialize script instance, not D_STRUCT_INSTANCE";
    }

    // Setup ID
    push_arg(ScriptManager::vm->memory, make_data(D_NUMBER, data_value_num(id)));
    // Custom memptr
    struct data* ptr = struct_get_field(ScriptManager::vm, classInstance, "id");
    push_arg(ScriptManager::vm->memory, make_data(D_INTERNAL_POINTER, data_value_ptr(ptr)));
    vm_run_instruction(ScriptManager::vm, OP_WRITE);
}

void WendyCallMemberFunction(struct data structInstance,
    const std::string& member, const std::vector<struct data> arguments) {
    // Setup a member function call
    struct data* fn = struct_get_field(ScriptManager::vm, structInstance, member.c_str());
    if (!fn) {
        LOG_WARN("Tried to call member function " << member << " but it was not defined!");
        return;
    }

    push_arg(ScriptManager::vm->memory, make_data(D_END_OF_ARGUMENTS, data_value_num(0)));
    for (auto it = arguments.rbegin(); it != arguments.rend(); ++it) {
        push_arg(ScriptManager::vm->memory, copy_data(*it));
    }

    push_arg(ScriptManager::vm->memory, copy_data(structInstance));
    struct data fn_copy = copy_data(*fn);
    fn_copy.type = D_STRUCT_FUNCTION;

    push_arg(ScriptManager::vm->memory, fn_copy);
    vm_run_instruction(ScriptManager::vm, OP_CALL);
    vm_run(ScriptManager::vm);
    if (get_error_flag()) {
        throw "Scripting Error";
    }
    // TODO: probably clean up stack here, might have a noneret?
}

// Serialization of Internal Wendy Structs
void ScriptInstance::Serialize(JSONWriter& obj) {
    Replicable::Serialize(obj);
    // obj.Key("s");
    // obj.StartArray();
    // // The number of params are stored at the struct instance header
    // if (classInstance.type != D_STRUCT_INSTANCE ||
    //     classInstance.value.reference[0].type != D_STRUCT_INSTANCE_HEADER) {
    //     LOG_ERROR("Could not serialize Wendy Struct, not D_STRUCT_INSTANCE_HEADER");
    //     throw "Could not serialize Wendy Struct, not D_STRUCT_INSTANCE_HEADER";
    // }

    // int numParams = (int)classInstance.value.reference[0].value.number;
    // // [0] is header [1] is reference to underlying metadata
    // // numParams includes the underlying metadata entry, so we -1
    // for (int i = 2; i < numParams - 1; i++) {
    //     struct data param = classInstance.value.reference[i];
    //     if (is_numeric(param)) {
    //         obj.Double(param.value.number);
    //     }
    //     else if (is_reference(param)) {
    //         // TODO: just serialize Lists and Tables
    //         LOG_ERROR("Serialization of references not yet supported");
    //         throw "Serialization of references not yet supported";
    //     }
    //     else {
    //         obj.String(param.value.string);
    //     }
    // }
    // obj.EndArray();
}

void ScriptInstance::ProcessReplication(json& obj) {
    Replicable::ProcessReplication(obj);

    // if (classInstance.type != D_STRUCT_INSTANCE ||
    //     classInstance.value.reference[0].type != D_STRUCT_INSTANCE_HEADER) {
    //     // Here is a weird timing thing. The script instance gets initialized
    //     //   on first replication by the script object, which potentially
    //     //   initializes after ScriptInstance::ProcessReplication in
    //     //   ScriptObject::OnClientCreate. This means we actually miss the first
    //     //   replication data, and won't update / catchup until the second replication.
    //     return;
    // }

    // int i = 2;
    // // Load Data from JSON Back into the script instance in the VM
    // for (auto& elem : obj["s"].GetArray()) {
    //     // TODO: if we ever replicate references we cannot destroy before
    //     //   we replace in case we drop the ref count to 0
    //     destroy_data_runtime(ScriptManager::vm->memory,
    //         &classInstance.value.reference[i]);
    //     if (elem.IsDouble()) {
    //         classInstance.value.reference[i] = make_data(D_NUMBER, data_value_num(elem.GetDouble()));
    //     }
    //     else if (elem.IsString()) {
    //         classInstance.value.reference[i] = make_data(D_STRING, data_value_str(elem.GetString()));
    //     }
    //     else {
    //         LOG_ERROR("Could not deserialize non string / double");
    //         throw "Could not deserialize non string / double";
    //     }
    //     i += 1;
    // }
}

ScriptInstance::ScriptInstance() {
    classInstance.type = D_EMPTY;
}

ScriptInstance::~ScriptInstance() {
    destroy_data_runtime(ScriptManager::vm->memory, &classInstance);
}
