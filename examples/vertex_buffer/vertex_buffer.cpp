/*
* Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#include <donut/app/ApplicationBase.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/TextureCache.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/app/DeviceManager.h>
#include <donut/core/log.h>
#include <donut/core/vfs/VFS.h>
#include <nvrhi/utils.h>

using namespace donut;

static const char* g_WindowTitle = "Donut Example: Vertex Buffer";

struct Vertex
{
    math::float3 position;
    math::float2 uv;
};

static const Vertex g_Vertices[] = {
    { {-0.5f,  0.5f, -0.5f}, {0.0f, 0.0f} }, // front face
    { { 0.5f, -0.5f, -0.5f}, {1.0f, 1.0f} },
    { {-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f} },
    { { 0.5f,  0.5f, -0.5f}, {1.0f, 0.0f} },

    { { 0.5f, -0.5f, -0.5f}, {0.0f, 1.0f} }, // right side face
    { { 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f} },
    { { 0.5f, -0.5f,  0.5f}, {1.0f, 1.0f} },
    { { 0.5f,  0.5f, -0.5f}, {0.0f, 0.0f} },

    { {-0.5f,  0.5f,  0.5f}, {0.0f, 0.0f} }, // left side face
    { {-0.5f, -0.5f, -0.5f}, {1.0f, 1.0f} },
    { {-0.5f, -0.5f,  0.5f}, {0.0f, 1.0f} },
    { {-0.5f,  0.5f, -0.5f}, {1.0f, 0.0f} },

    { { 0.5f,  0.5f,  0.5f}, {0.0f, 0.0f} }, // back face
    { {-0.5f, -0.5f,  0.5f}, {1.0f, 1.0f} },
    { { 0.5f, -0.5f,  0.5f}, {0.0f, 1.0f} },
    { {-0.5f,  0.5f,  0.5f}, {1.0f, 0.0f} },

    { {-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f} }, // top face
    { { 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f} },
    { { 0.5f,  0.5f, -0.5f}, {1.0f, 1.0f} },
    { {-0.5f,  0.5f,  0.5f}, {0.0f, 0.0f} },

    { { 0.5f, -0.5f,  0.5f}, {1.0f, 1.0f} }, // bottom face
    { {-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f} },
    { { 0.5f, -0.5f, -0.5f}, {1.0f, 0.0f} },
    { {-0.5f, -0.5f,  0.5f}, {0.0f, 1.0f} },
};

static const uint32_t g_Indices[] = {
     0,  1,  2,   0,  3,  1, // front face
     4,  5,  6,   4,  7,  5, // left face
     8,  9, 10,   8, 11,  9, // right face
    12, 13, 14,  12, 15, 13, // back face
    16, 17, 18,  16, 19, 17, // top face
    20, 21, 22,  20, 23, 21, // bottom face
};

constexpr uint32_t c_NumViews = 4;

static const math::float3 g_RotationAxes[c_NumViews] = {
    math::float3(1.f, 0.f, 0.f),
    math::float3(0.f, 1.f, 0.f),
    math::float3(0.f, 0.f, 1.f),
    math::float3(1.f, 1.f, 1.f),
};

class VertexBuffer : public app::IRenderPass
{
private:
    nvrhi::ShaderHandle m_VertexShader;
    nvrhi::ShaderHandle m_PixelShader;
    nvrhi::BufferHandle m_ConstantBuffer;

    nvrhi::BufferHandle m_VertexBuffer;
    nvrhi::BufferHandle m_IndexBuffer;
    nvrhi::TextureHandle m_Texture;
    nvrhi::InputLayoutHandle m_InputLayout;
    nvrhi::BindingLayoutHandle m_BindingLayout;
    nvrhi::BindingSetHandle m_BindingSets[c_NumViews];

    nvrhi::GraphicsPipelineHandle m_Pipeline;
    nvrhi::CommandListHandle m_CommandList;
    float m_Rotation = 0.f;

public:
    using IRenderPass::IRenderPass;

    // This example uses a single large constant buffer with multiple views to draw multiple versions of the same model.
    // The alignment and size of partially bound constant buffers must be a multiple of 256 bytes,
    // so define a struct that represents one constant buffer entry or slice for one draw call.
    struct ConstantBufferEntry
    {
        dm::float4x4 viewProjMatrix;
        float padding[16*3];// 字节对齐256
    };

    static_assert(sizeof(ConstantBufferEntry) == nvrhi::c_ConstantBufferOffsetSizeAlignment, "sizeof(ConstantBufferEntry) must be 256 bytes");

    bool Init()
    {
        //  创建一个本地文件系统实例（nativeFS），用于加载本地文件如纹理等。
        auto nativeFS = std::make_shared<vfs::NativeFileSystem>();

        //  获取和设置着色器路径：
        //  frameworkShader和appShader的路径。这些路径依赖于执行文件的目录和图形API的类型。
        std::filesystem::path frameworkShaderPath = app::GetDirectoryWithExecutable() / "shaders/framework" / app::GetShaderTypeName(GetDevice()->getGraphicsAPI());
        std::filesystem::path appShaderPath = app::GetDirectoryWithExecutable() / "shaders/vertex_buffer" / app::GetShaderTypeName(GetDevice()->getGraphicsAPI());

        // 创建根文件系统（rootFS），并将不同的着色器路径挂载到指定的虚拟路径上。
		std::shared_ptr<vfs::RootFileSystem> rootFS = std::make_shared<vfs::RootFileSystem>();
		rootFS->mount("/shaders/donut", frameworkShaderPath);
		rootFS->mount("/shaders/app", appShaderPath);

        // 创建shaderFactory实例，用于加载着色器文件并创建着色器对象。
        std::shared_ptr<engine::ShaderFactory> shaderFactory = std::make_shared<engine::ShaderFactory>(GetDevice(), rootFS, "/shaders");
        m_VertexShader = shaderFactory->CreateShader("app/shaders.hlsl", "main_vs", nullptr, nvrhi::ShaderType::Vertex);
        m_PixelShader = shaderFactory->CreateShader("app/shaders.hlsl", "main_ps", nullptr, nvrhi::ShaderType::Pixel);

        if (!m_VertexShader || !m_PixelShader)
        {
            return false;
        }
        // Uniform buffer, 创建常量缓冲区
        m_ConstantBuffer = GetDevice()->createBuffer(nvrhi::utils::CreateStaticConstantBufferDesc(
                                        sizeof(ConstantBufferEntry) * c_NumViews,
                                        "ConstantBuffer")
                                        .setInitialState(nvrhi::ResourceStates::ConstantBuffer)
                                        .setKeepInitialState(true)
                                        );
        // Input layout, 设置输入布局
        nvrhi::VertexAttributeDesc attributes[] = {
            nvrhi::VertexAttributeDesc()
                .setName("POSITION")
                .setFormat(nvrhi::Format::RGB32_FLOAT)
                .setOffset(0)
                .setBufferIndex(0)
                .setElementStride(sizeof(Vertex)),
            nvrhi::VertexAttributeDesc()
                .setName("UV")
                .setFormat(nvrhi::Format::RG32_FLOAT)
                .setOffset(0)
                .setBufferIndex(1)
                .setElementStride(sizeof(Vertex)),
        };
        m_InputLayout = GetDevice()->createInputLayout(attributes, uint32_t(std::size(attributes)), m_VertexShader);

        // 创建和管理命令列表
        engine::CommonRenderPasses commonPasses(GetDevice(), shaderFactory);
        engine::TextureCache textureCache(GetDevice(), nativeFS, nullptr);

        //------------------------------- 场景加载 -------------------------------//
        // 对VertexBuffer，IndexBuffer进行数据搬运
        m_CommandList = GetDevice()->createCommandList();
        m_CommandList->open();

        nvrhi::BufferDesc vertexBufferDesc;
        vertexBufferDesc.byteSize = sizeof(g_Vertices);
        vertexBufferDesc.isVertexBuffer = true;
        vertexBufferDesc.debugName = "VertexBuffer";
        vertexBufferDesc.initialState = nvrhi::ResourceStates::CopyDest;
        m_VertexBuffer = GetDevice()->createBuffer(vertexBufferDesc);

        m_CommandList->beginTrackingBufferState(m_VertexBuffer, nvrhi::ResourceStates::CopyDest);
        m_CommandList->writeBuffer(m_VertexBuffer, g_Vertices, sizeof(g_Vertices));
        m_CommandList->setPermanentBufferState(m_VertexBuffer, nvrhi::ResourceStates::VertexBuffer);

        nvrhi::BufferDesc indexBufferDesc;
        indexBufferDesc.byteSize = sizeof(g_Indices);
        indexBufferDesc.isIndexBuffer = true;
        indexBufferDesc.debugName = "IndexBuffer";
        indexBufferDesc.initialState = nvrhi::ResourceStates::CopyDest;
        m_IndexBuffer = GetDevice()->createBuffer(indexBufferDesc);

        m_CommandList->beginTrackingBufferState(m_IndexBuffer, nvrhi::ResourceStates::CopyDest);
        m_CommandList->writeBuffer(m_IndexBuffer, g_Indices, sizeof(g_Indices));
        m_CommandList->setPermanentBufferState(m_IndexBuffer, nvrhi::ResourceStates::IndexBuffer);

        // Load the texture
        std::filesystem::path textureFileName = app::GetDirectoryWithExecutable().parent_path() / "media/nvidia-logo.png";
        std::shared_ptr<engine::LoadedTexture> texture = textureCache.LoadTextureFromFile(textureFileName, true, nullptr, m_CommandList);
        m_Texture = texture->texture;

        m_CommandList->close();// 关闭命令列表
        GetDevice()->executeCommandList(m_CommandList);// 执行命令列表

        if (!texture->texture)
        {
            log::error("Couldn't load the texture");
            return false;
        }

        // Create a single binding layout and multiple binding sets, one set per view.
        // The different binding sets use different slices of the same constant buffer.
        // 根据c_NumViews，为view创建一个绑定集。
        // 绑定集包含对常量缓冲区的引用，以及纹理和采样器的引用
        for (uint32_t viewIndex = 0; viewIndex < c_NumViews; ++viewIndex)
        {
            /*  cbuffer CB : register(b0)
                {
                    float4x4 g_Transform;
                };
             *  Texture2D t_Texture : register(t0);
             *  SamplerState s_Sampler : register(s0);
             *
             */
            nvrhi::BindingSetDesc bindingSetDesc;
            bindingSetDesc.bindings = {
                // Note: using viewIndex to construct a buffer range.
                nvrhi::BindingSetItem::ConstantBuffer(0,
                                                      m_ConstantBuffer,
                                                      nvrhi::BufferRange(sizeof(ConstantBufferEntry) * viewIndex,
                                                                                sizeof(ConstantBufferEntry))
                                                      ),
                // Texture and sampler are the same for all model views.
                nvrhi::BindingSetItem::Texture_SRV(0, m_Texture),
                nvrhi::BindingSetItem::Sampler(0, commonPasses.m_AnisotropicWrapSampler)
            };

            // Create the binding layout (if it's empty -- so, on the first iteration) and the binding set.
            if (!nvrhi::utils::CreateBindingSetAndLayout(GetDevice(),
                                                         nvrhi::ShaderType::All,
                                                         0,
                                                         bindingSetDesc,
                                                         m_BindingLayout,
                                                         m_BindingSets[viewIndex]))
            {
                log::error("Couldn't create the binding set or layout");
                return false;
            }
        }

        return true;
    }

    void Animate(float seconds) override
    {
        m_Rotation += seconds * 1.1f;
        GetDeviceManager()->SetInformativeWindowTitle(g_WindowTitle);
    }

    void BackBufferResizing() override
    {
        m_Pipeline = nullptr;
    }

    void Render(nvrhi::IFramebuffer* framebuffer) override
    {
        const nvrhi::FramebufferInfoEx& fbinfo = framebuffer->getFramebufferInfo();
        // 如果渲染管线未初始化，则进行初始化并设置管线的各种状态
        if (!m_Pipeline)
        {
            nvrhi::GraphicsPipelineDesc psoDesc;
            psoDesc.VS = m_VertexShader;
            psoDesc.PS = m_PixelShader;
            psoDesc.inputLayout = m_InputLayout;
            psoDesc.bindingLayouts = { m_BindingLayout };
            psoDesc.primType = nvrhi::PrimitiveType::TriangleList;
            psoDesc.renderState.depthStencilState.depthTestEnable = false;

            m_Pipeline = GetDevice()->createGraphicsPipeline(psoDesc, framebuffer);
        }

        m_CommandList->open();
        // 清除帧缓冲区的颜色附件
        nvrhi::utils::ClearColorAttachment(m_CommandList, framebuffer, 0, nvrhi::Color(0.f));

        // Fill out the constant buffer slices for multiple views of the model.
        // 修改UniformBuffer的数据
        ConstantBufferEntry modelConstants[c_NumViews];
        for (uint32_t viewIndex = 0; viewIndex < c_NumViews; ++viewIndex)
        {
            math::affine3 viewMatrix = math::rotation(normalize(g_RotationAxes[viewIndex]), m_Rotation)
                * math::yawPitchRoll(0.f, math::radians(-30.f), 0.f)
                * math::translation(math::float3(0, 0, 2));
            math::float4x4 projMatrix = math::perspProjD3DStyle(math::radians(60.f), float(fbinfo.width) / float(fbinfo.height), 0.1f, 10.f);
            math::float4x4 viewProjMatrix = math::affineToHomogeneous(viewMatrix) * projMatrix;
            modelConstants[viewIndex].viewProjMatrix = viewProjMatrix;
        }

        // Upload all constant buffer slices at once.
        // 在外部修改UniformBuffer的数据，然后再上传到GPU
        m_CommandList->writeBuffer(m_ConstantBuffer, modelConstants, sizeof(modelConstants));

        // 进行多视口绘制指令
        for (uint32_t viewIndex = 0; viewIndex < c_NumViews; ++viewIndex)
        {
            nvrhi::GraphicsState state;
            // Pick the right binding set for this view.
            // m_BindingSets[viewIndex] 理解为 DescriptorSet 对象
            state.bindings = { m_BindingSets[viewIndex] };
            state.indexBuffer = { m_IndexBuffer, nvrhi::Format::R32_UINT, 0 };
            // Bind the vertex buffers in reverse order to test the NVRHI implementation of binding slots
            state.vertexBuffers = {
                { m_VertexBuffer, 1, offsetof(Vertex, uv) },
                { m_VertexBuffer, 0, offsetof(Vertex, position) }
            };
            state.pipeline = m_Pipeline;// 管线对象
            state.framebuffer = framebuffer;// 帧缓冲区对象

            // Construct the viewport so that all viewports form a grid.
            // 配置视口位置和裁剪矩形
            const float width = float(fbinfo.width) * 0.5f;
            const float height = float(fbinfo.height) * 0.5f;
            const float left = width * float(viewIndex % 2);
            const float top = height * float(viewIndex / 2);

            const nvrhi::Viewport viewport = nvrhi::Viewport(left, left + width, top, top + height, 0.f, 1.f);
            state.viewport.addViewportAndScissorRect(viewport);

            // Update the pipeline, bindings, and other state.
            m_CommandList->setGraphicsState(state);

            // Draw the model.
            nvrhi::DrawArguments args;
            args.vertexCount = dim(g_Indices);
            m_CommandList->drawIndexed(args);
        }

        m_CommandList->close();
        GetDevice()->executeCommandList(m_CommandList);
    }
};

#ifdef WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
#else
int main(int __argc, const char** __argv)
#endif
{
    nvrhi::GraphicsAPI api = app::GetGraphicsAPIFromCommandLine(__argc, __argv);
    app::DeviceManager* deviceManager = app::DeviceManager::Create(api);

    app::DeviceCreationParameters deviceParams;
#ifdef _DEBUG
    deviceParams.enableDebugRuntime = true; 
    deviceParams.enableNvrhiValidationLayer = true;
#endif

    if (!deviceManager->CreateWindowDeviceAndSwapChain(deviceParams, g_WindowTitle))
    {
        log::fatal("Cannot initialize a graphics device with the requested parameters");
        return 1;
    }

    {
        VertexBuffer example(deviceManager);
        if (example.Init())
        {
            deviceManager->AddRenderPassToBack(&example);
            deviceManager->RunMessageLoop();
            deviceManager->RemoveRenderPass(&example);
        }
    }

    deviceManager->Shutdown();

    delete deviceManager;

    return 0;
}
