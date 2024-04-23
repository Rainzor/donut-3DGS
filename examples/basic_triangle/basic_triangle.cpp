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
#include <donut/app/DeviceManager.h>
#include <donut/core/log.h>
#include <donut/core/vfs/VFS.h>
#include <nvrhi/utils.h>

using namespace donut;

static const char* g_WindowTitle = "Donut Example: Basic Triangle";

class BasicTriangle : public app::IRenderPass
{
private:
    nvrhi::ShaderHandle m_VertexShader;
    nvrhi::ShaderHandle m_PixelShader;
    nvrhi::GraphicsPipelineHandle m_Pipeline;
    nvrhi::CommandListHandle m_CommandList;

public:
    using IRenderPass::IRenderPass;

    /*
     * 加载着色器文件，并创建着色器对象, 以及创建命令列表对象
     */
    bool Init()
    {
        std::filesystem::path appShaderPath = app::GetDirectoryWithExecutable() / "shaders/basic_triangle" /  app::GetShaderTypeName(GetDevice()->getGraphicsAPI());

        auto nativeFS = std::make_shared<vfs::NativeFileSystem>();
        engine::ShaderFactory shaderFactory(GetDevice(), nativeFS, appShaderPath);

        m_VertexShader = shaderFactory.CreateShader("shaders.hlsl", "main_vs", nullptr, nvrhi::ShaderType::Vertex);
        m_PixelShader = shaderFactory.CreateShader("shaders.hlsl", "main_ps", nullptr, nvrhi::ShaderType::Pixel);

        if (!m_VertexShader || !m_PixelShader)
        {
            return false;
        }

        m_CommandList = GetDevice()->createCommandList();

        return true;
    }
    /*
     * 在窗口或视图缓冲区大小改变时重置管线对象
     * */
    void BackBufferResizing() override
    {
        m_Pipeline = nullptr;
    }
    /*
     * 设置窗口标题
     * */
    void Animate(float fElapsedTimeSeconds) override
    {
        GetDeviceManager()->SetInformativeWindowTitle(g_WindowTitle);
    }
    /*
     * 渲染函数:创建或更新图形管线描述，设置渲染状态，清空颜色缓冲区，
     *         执行绘制命令
     * */
    void Render(nvrhi::IFramebuffer* framebuffer) override
    {
        // 如果渲染管线未初始化，则进行初始化并设置管线的各种状态
        if (!m_Pipeline)
        {
            nvrhi::GraphicsPipelineDesc psoDesc;
            psoDesc.VS = m_VertexShader;
            psoDesc.PS = m_PixelShader;
            psoDesc.primType = nvrhi::PrimitiveType::TriangleList;// 基本图元类型
            psoDesc.renderState.depthStencilState.depthTestEnable = false;

            m_Pipeline = GetDevice()->createGraphicsPipeline(psoDesc, framebuffer);
        }

        // 命令开始
        m_CommandList->open();

        // 清除帧缓冲区的颜色附件
        nvrhi::utils::ClearColorAttachment(m_CommandList, framebuffer, 0, nvrhi::Color(0.f));

        // 配置当前的管线和帧缓冲区，并添加视口和裁剪矩形
        nvrhi::GraphicsState state;
        state.pipeline = m_Pipeline;
        state.framebuffer = framebuffer;
        state.viewport.addViewportAndScissorRect(framebuffer->getFramebufferInfo().getViewport());
        // 设置渲染状态
        m_CommandList->setGraphicsState(state);

        // 设置执行绘制命令的参数
        nvrhi::DrawArguments args;
        args.vertexCount = 3;
        m_CommandList->draw(args);

        // 命令结束
        m_CommandList->close();

        // 执行命令列表
        GetDevice()->executeCommandList(m_CommandList);
    }

};

// 初始化图形渲染环境，并控制程序的整体流程
#ifdef WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
#else
int main(int __argc, const char** __argv)
#endif
{
    // dx12 as default
    nvrhi::GraphicsAPI api = app::GetGraphicsAPIFromCommandLine(__argc, __argv);
    app::DeviceManager* deviceManager = app::DeviceManager::Create(api);

    app::DeviceCreationParameters deviceParams;
#ifdef _DEBUG
    deviceParams.enableDebugRuntime = true;
    deviceParams.enableNvrhiValidationLayer = true;
#endif
    // 创建窗口设备和交换链
    if (!deviceManager->CreateWindowDeviceAndSwapChain(deviceParams, g_WindowTitle))
    {
        log::fatal("Cannot initialize a graphics device with the requested parameters");
        return 1;
    }

    {
        BasicTriangle example(deviceManager);
        if (example.Init())
        {   // 将example实例添加到渲染队列
            deviceManager->AddRenderPassToBack(&example);
            // 运行一个消息循环，直到窗口关闭, 涉及到处理窗口事件（如关闭、大小调整等）和定期调用渲染函数。
            deviceManager->RunMessageLoop();
            // 从渲染队列中移除example实例
            deviceManager->RemoveRenderPass(&example);
        }
    }
    // 关闭和清理设备管理器，释放所有相关资源
    deviceManager->Shutdown();

    delete deviceManager;

    return 0;
}
