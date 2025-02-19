#pragma once

#include "mesh.h"
#include "shader.h"
#include "buffers.h"
#include "asset-manager.h"
#include "scene.h"
#include "bloom.h"
#include "debug_renderer.h"

// Everything must be loaded in, don't depend on game instance

struct DrawParams {
    ObjectID id;
    Mesh* mesh = nullptr;
    Matrix4 transform;
    bool castShadows;
    bool hasOutline = false;
    bool isWireframe = false;
    Material* overrideMaterial = nullptr;
};

struct DrawLayer {
    // std::map<float, std::vector<DrawParams>> opaque;
    std::map<Material*, std::vector<DrawParams>> opaque;
    std::map<float, std::vector<DrawParams>> transparent;

    void Clear() {
        opaque.clear();
        transparent.clear();
    }

    DrawParams& PushOpaque(Material* material) {
        auto& list = opaque[material];
        list.reserve(500);
        return list.emplace_back();
    }

    DrawParams& PushTransparent(float depth) {
        auto& list = transparent[depth];
        list.reserve(500);
        return list.emplace_back();
    }
};

struct RenderFrameParameters {
    Vector3 viewPos;
    Vector3 viewDir;

    Texture* skydomeTexture = nullptr;

    enum class Projection { ORTHOGRAPHIC, PERSPECTIVE } projection = Projection::PERSPECTIVE;

    // For PERSPECTIVE
    float FOV = glm::radians(55.0f);

    // For ORTHOGRAPHIC
    float orthoSize = 10.0f;

    Vector3 viewUp = Vector::Up;
    float viewNear = 0.2f;
    float viewFar = 300.f;

    Matrix4 view;
    Matrix4 proj;
    Matrix4 shadowView;

    int width;
    int height;

    float ambientFactor;

    bool enableLighting = false;

    bool enableShadows = false;

    bool enableBloom = false;
    float bloomThreshold = 1.0f;

    bool enableToneMapping = false;
    float exposure = 1.0f;

    bool enableAntialiasing = false;

    float fxaaLumaThreshold = 0.5f;
    float fxaaMulReduceReciprocal = 1.0f / 8.0f;
    float fxaaMinReduceReciprocal = 1.0f / 128.0f;
    float fxaaMaxSpan = 8.0f;

    std::vector<TransformedLight*> lights;

    // Debug Settings
    struct {
        bool drawShadowMapDebug = false;
        bool overrideShadowView = false;
        Vector3 overrideShadowViewDir;
        Vector3 overrideShadowViewPos;
    } debugSettings;
};

class DeferredRenderer {
    bool isInitialized = false;
    AssetManager& assetManager;

    DebugRenderer debugRenderer;

    DeferredShadingGeometryShaderProgram* geometryShader;
    QuadShaderProgram* quadShader;

    GBuffer gBuffer;
    GBuffer transparencyGBuffer;

    RenderBuffer outputBuffer;

    RenderFrameParameters* renderFrameParameters = nullptr;

    // Different lighting shaders for each type of light
    DeferredShadingLightingShaderProgram* pointLightShader;
    DeferredShadingLightingShaderProgram* rectangleLightShader;
    DeferredShadingLightingShaderProgram* directionalLightShader;

    QuadShaderProgram* skydomeShader;
    ShadowMapShaderProgram* shadowMapShader;

    BloomShader* bloomShader;

    // Tone Mapping
    QuadShaderProgram* toneMappingShader;
    GLint uniformToneMappingExposure;

    QuadShaderProgram* fxaaShader;
    GLint uniformFXAALumaThreshold;
    GLint uniformFXAAMulReduceReciprocal;
    GLint uniformFXAAMinReduceReciprocal;
    GLint uniformFXAAMaxSpan;
    GLint uniformSkydomeDirection;
    GLint uniformSkydomeFOV;
    GLint uniformSkydomeWidth;
    GLint uniformSkydomeHeight;

public:

    DeferredRenderer(AssetManager& assetManager);

    void Initialize();
    void DrawShadowObjects(std::initializer_list<DrawLayer*> layers);
    void DrawObject(const DrawParams& params);

    void NewFrame(RenderFrameParameters* params);
    void EndFrame();

    void Draw(std::initializer_list<DrawLayer*> layers);
    void DrawShadowMaps(std::initializer_list<DrawLayer*> layers);

    QuadShaderProgram& GetQuadShader() { return *quadShader; }

    GLuint GetRenderedTexture() { return outputBuffer.BlitTexture(); }

    bool IsInitialized() {
        return isInitialized;
    }

    GBuffer& GetGBuffer() {
        return gBuffer;
    }

    GBuffer& GetTransparencyGBuffer() {
        return transparencyGBuffer;
    }

    BloomShader* GetBloomShader() {
        return bloomShader;
    }

    RenderBuffer& GetOutputBuffer() {
        return outputBuffer;
    }

    DebugRenderer& GetDebugRenderer() {
        return debugRenderer;
    }
};