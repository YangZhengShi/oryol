//------------------------------------------------------------------------------
//  Shapes.cc
//------------------------------------------------------------------------------
#include "Pre.h"
#include "Core/Main.h"
#include "Gfx/Gfx.h"
#include "Assets/Gfx/ShapeBuilder.h"
#include "glm/mat4x4.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "shaders.h"

using namespace Oryol;

class ShapeApp : public App {
public:
    AppState::Code OnRunning();
    AppState::Code OnInit();
    AppState::Code OnCleanup();
    glm::mat4 computeMVP(const glm::vec3& pos);

    Id pip;
    Bindings bind;
    Array<PrimitiveGroup> primGroups;
    Shader::params params;
    float angleX = 0.0f;
    float angleY = 0.0f;
};
OryolMain(ShapeApp);

//------------------------------------------------------------------------------
AppState::Code
ShapeApp::OnInit() {
    Gfx::Setup(GfxDesc()
        .SetWidth(600)
        .SetHeight(400)
        .SetSampleCount(4)
        .SetTitle("Oryol Shapes Sample")
        .SetHtmlTrackElementSize(true));
    auto shapes = ShapeBuilder()
        .RandomColors(true)
        .Positions("position", VertexFormat::Float3)
        .Colors("color0", VertexFormat::UByte4N)
        .Box(1.0f, 1.0f, 1.0f, 4)
        .Sphere(0.75f, 36, 20)
        .Cylinder(0.5f, 1.5f, 36, 10)
        .Torus(0.3f, 0.5f, 20, 36)
        .Plane(1.5f, 1.5f, 10)
        .Build();
    this->bind.VertexBuffers[0] = Gfx::CreateBuffer(shapes.VertexBufferDesc);
    this->bind.IndexBuffer = Gfx::CreateBuffer(shapes.IndexBufferDesc);
    this->pip = Gfx::CreatePipeline(PipelineDesc(shapes.PipelineDesc)
        .SetShader(Gfx::CreateShader(Shader::Desc()))
        .SetDepthWriteEnabled(true)
        .SetDepthCmpFunc(CompareFunc::LessEqual)
        .SetSampleCount(Gfx::Desc().SampleCount));
    this->primGroups = std::move(shapes.PrimitiveGroups);

    return App::OnInit();
}

//------------------------------------------------------------------------------
AppState::Code
ShapeApp::OnRunning() {
    
    this->angleY += 0.01f;
    this->angleX += 0.02f;
    
    Gfx::BeginPass();
    Gfx::ApplyPipeline(this->pip);
    Gfx::ApplyBindings(this->bind);
    static const glm::vec3 positions[] = {
        glm::vec3(-1.0, 1.0f, -6.0f),
        glm::vec3(1.0f, 1.0f, -6.0f),
        glm::vec3(-2.0f, -1.0f, -6.0f),
        glm::vec3(+2.0f, -1.0f, -6.0f),
        glm::vec3(0.0f, -1.0f, -6.0f)
    };
    int primGroupIndex = 0;
    for (const auto& pos : positions) {
        this->params.mvp = this->computeMVP(pos);
        Gfx::ApplyUniforms(this->params);
        Gfx::Draw(this->primGroups[primGroupIndex++]);
    }
    Gfx::EndPass();
    Gfx::CommitFrame();
    
    return Gfx::QuitRequested() ? AppState::Cleanup : AppState::Running;
}

//------------------------------------------------------------------------------
AppState::Code
ShapeApp::OnCleanup() {
    Gfx::Discard();
    return App::OnCleanup();
}

//------------------------------------------------------------------------------
glm::mat4
ShapeApp::computeMVP(const glm::vec3& pos) {
    glm::mat4 proj = glm::perspectiveFov(glm::radians(45.0f), float(Gfx::Width()), float(Gfx::Height()), 0.01f, 100.0f);
    glm::mat4 modelTform = glm::translate(glm::mat4(), pos);
    modelTform = glm::rotate(modelTform, this->angleX, glm::vec3(1.0f, 0.0f, 0.0f));
    modelTform = glm::rotate(modelTform, this->angleY, glm::vec3(0.0f, 1.0f, 0.0f));
    return proj * modelTform;
}

