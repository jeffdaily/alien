#include "SimulationView.h"

#include <algorithm>
#include <stdexcept>
#include <vector>

#include <glad/glad.h>

#include <imgui.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <Base/GlobalSettings.h>
#include <Base/Resources.h>

#include <EngineInterface/SimulationFacade.h>
#include <EngineInterface/SpaceCalculator.h>

#include "AlienGui.h"
#include "RenderPipeline.h"
#include "RenderStep.h"
#include "Shader.h"
#include "SimulationScrollbars.h"
#include "StyleRepository.h"
#include "Viewport.h"

void SimulationView::setup()
{

    _cellDetailOverlayActive = GlobalSettings::get().getValue("settings.simulation view.overlay", _cellDetailOverlayActive);
    _brightness = GlobalSettings::get().getValue("windows.simulation view.brightness", _brightness);
    _contrast = GlobalSettings::get().getValue("windows.simulation view.contrast", _contrast);
    _motionBlur = GlobalSettings::get().getValue("windows.simulation view.motion blur factor", _motionBlur);

    setupRenderPipeline();

    _scrollbars = std::make_shared<_SimulationScrollbars>(true);

    resize(Viewport::get().getViewSize());
}

void SimulationView::shutdown()
{
    GlobalSettings::get().setValue("settings.simulation view.overlay", _cellDetailOverlayActive);
    GlobalSettings::get().setValue("windows.simulation view.brightness", _brightness);
    GlobalSettings::get().setValue("windows.simulation view.contrast", _contrast);
    GlobalSettings::get().setValue("windows.simulation view.motion blur factor", _motionBlur);
}

void SimulationView::resize(IntVector2D const& size)
{
    _renderPipeline->resize(size);

    Viewport::get().setViewSize(size);
}

void SimulationView::draw()
{
    if (_renderSimulation) {
        _renderPipeline->execute();

        if (_SimulationFacade::get()->getSimulationParameters().markReferenceDomain.value) {
            markReferenceDomain();
        }

    } else {
        glClearColor(0, 0, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        auto textWidth = scale(300.0f);
        auto textHeight = scale(80.0f);
        ImDrawList* drawList = ImGui::GetBackgroundDrawList();
        auto& styleRep = StyleRepository::get();
        auto right = ImGui::GetMainViewport()->Pos.x + ImGui::GetMainViewport()->Size.x;
        auto bottom = ImGui::GetMainViewport()->Pos.y + ImGui::GetMainViewport()->Size.y;
        auto maxLength = std::max(right, bottom);

        AlienGui::RotateStart(drawList);
        auto font = styleRep.getReefLargeFont();
        auto text = "Rendering disabled";
        ImVec4 clipRect(-100000.0f, -100000.0f, 100000.0f, 100000.0f);
        for (int i = 0; toFloat(i) * textWidth < maxLength * 2; ++i) {
            for (int j = 0; toFloat(j) * textHeight < maxLength * 2; ++j) {
                font->RenderText(
                    drawList,
                    scale(34.0f),
                    {toFloat(i) * textWidth - maxLength / 2, toFloat(j) * textHeight - maxLength / 2},
                    Const::RenderingDisabledTextColor,
                    clipRect,
                    text,
                    text + strlen(text),
                    0.0f,
                    false);
            }
        }
        AlienGui::RotateEnd(45.0f, drawList);
    }
}

void SimulationView::processSimulationScrollbars()
{
    if (_renderSimulation) {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        auto mainMenubarHeight = scale(22);

        auto worldCenter = Viewport::get().getCenterInWorldPos();
        auto worldRect = RealRect{{0, 0}, toRealVector2D(_SimulationFacade::get()->getWorldSize())};
        auto visibleWorldRect = Viewport::get().getVisibleWorldRect();
        auto viewRect =
            RealRect{{viewport->Pos.x, viewport->Pos.y + mainMenubarHeight}, {viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y}};
        _scrollbars->process(worldCenter, worldRect, visibleWorldRect, viewRect);
        Viewport::get().setCenterInWorldPos({worldCenter.x, worldCenter.y});
    }
}

bool SimulationView::isScrollbarDragging() const
{
    return _scrollbars->isHoveredOrDragged();
}

bool SimulationView::isRenderSimulation() const
{
    return _renderSimulation;
}

void SimulationView::setRenderSimulation(bool value)
{
    _renderSimulation = value;
}

bool SimulationView::isOverlayActive() const
{
    return _cellDetailOverlayActive;
}

void SimulationView::setOverlayActive(bool active)
{
    _cellDetailOverlayActive = active;
}

float SimulationView::getBrightness() const
{
    return _brightness;
}

void SimulationView::setBrightness(float value)
{
    _brightness = value;
}

float SimulationView::getContrast() const
{
    return _contrast;
}

void SimulationView::setContrast(float value)
{
    _contrast = value;
}

float SimulationView::getMotionBlur() const
{
    return _motionBlur;
}

void SimulationView::setMotionBlur(float value)
{
    _motionBlur = value;
}

void SimulationView::updateMotionBlur() {}

void SimulationView::savePicture(std::filesystem::path const& filename, float pixelPerWorldUnit)
{
    auto& viewport = Viewport::get();

    // Determine the visible world rect (unchanged during this operation) and the resulting picture size.
    auto worldRect = viewport.getVisibleWorldRect();
    auto rectWidth = worldRect.bottomRight.x - worldRect.topLeft.x;
    auto rectHeight = worldRect.bottomRight.y - worldRect.topLeft.y;

    IntVector2D pictureSize{
        std::max(1, toInt(rectWidth * pixelPerWorldUnit)),
        std::max(1, toInt(rectHeight * pixelPerWorldUnit)),
    };

    // Save current viewport state so it can be restored later. The visible world rect is preserved by
    // using a temporary zoom of `pixelPerWorldUnit` together with a view size of `pictureSize` (because
    // the visible world rect width/height equals viewSize / zoomFactor).
    auto origViewSize = viewport.getViewSize();
    auto origZoomFactor = viewport.getZoomFactor();

    viewport.setViewSize(pictureSize);
    viewport.setZoomFactor(pixelPerWorldUnit);

    // Create offscreen framebuffer that captures the final output of the render pipeline (which writes
    // to the framebuffer that is bound when execute() is called).
    GLint origFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &origFbo);

    GLuint captureTexture = 0;
    GLuint captureFbo = 0;
    GLuint captureDepth = 0;
    glGenTextures(1, &captureTexture);
    glBindTexture(GL_TEXTURE_2D, captureTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, pictureSize.x, pictureSize.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenRenderbuffers(1, &captureDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, captureDepth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, pictureSize.x, pictureSize.y);

    glGenFramebuffers(1, &captureFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, captureFbo);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, captureTexture, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, captureDepth);

    bool fboComplete = (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

    std::vector<unsigned char> pixels;
    bool success = false;
    if (fboComplete) {
        // Resize render pipeline so its intermediate texture targets match the new picture size.
        _renderPipeline->resize(pictureSize);

        // Render through the standard pipeline. The bound `captureFbo` will be used as the "screen" target.
        _renderPipeline->execute();

        // Read back the pixels from the capture framebuffer.
        glBindFramebuffer(GL_FRAMEBUFFER, captureFbo);
        pixels.resize(static_cast<size_t>(pictureSize.x) * pictureSize.y * 4);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, pictureSize.x, pictureSize.y, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        success = true;
    }

    // Restore the original framebuffer binding and clean up the capture resources.
    glBindFramebuffer(GL_FRAMEBUFFER, origFbo);
    glDeleteFramebuffers(1, &captureFbo);
    glDeleteRenderbuffers(1, &captureDepth);
    glDeleteTextures(1, &captureTexture);

    // Restore the viewport and render pipeline to their previous size.
    viewport.setViewSize(origViewSize);
    viewport.setZoomFactor(origZoomFactor);
    _renderPipeline->resize(origViewSize);

    if (!success) {
        throw std::runtime_error("Could not create offscreen framebuffer for picture capture.");
    }

    // OpenGL returns pixels bottom-up; flip the rows so the PNG appears in the expected orientation.
    stbi_flip_vertically_on_write(1);
    if (!stbi_write_png(filename.string().c_str(), pictureSize.x, pictureSize.y, 4, pixels.data(), pictureSize.x * 4)) {
        throw std::runtime_error("Could not write PNG file: " + filename.string());
    }
}

void SimulationView::setupRenderPipeline()
{
    // Define lambdas for render pipeline
    auto backgroundUniformFunc = [this](SimulationParameters const& parameters) {
        return UniformValueMap{
            {"background", parameters.backgroundColor.baseValue},
            {"borderlessRendering", parameters.borderlessRendering.value},
        };
    };
    auto gridLinesUniformFunc = [](SimulationParameters const& parameters) {
        return UniformValueMap{
            {"gridLines", parameters.gridLines.value},
        };
    };
    auto moduloUniformFunc = [](SimulationParameters const& parameters) {
        return UniformValueMap{{"borderlessRendering", parameters.borderlessRendering.value}};
    };

    // Number of blur repetitions and blur strengths is based on zoom level to balance performance and quality
    auto blurStrengthFunc = [](SimulationParameters const& parameters) {
        auto zoom = Viewport::get().getZoomFactor();
        float strength = 0.035f;
        if (zoom < 100.0f) {
            strength *= 3.5f;
        }
        if (zoom < 50.0f) {
            strength *= 2.5f;
        }
        if (zoom < 25.0f) {
            strength *= 2.5f;
        }
        if (zoom < 12.0f) {
            strength *= 2.5f;
        }
        if (zoom < 6.0f) {
            strength *= 2.5f;
        }
        return UniformValueMap{{"strength", strength}};
    };
    auto blurRepetitionsFunc = [] {
        auto zoom = Viewport::get().getZoomFactor();
        auto result = 7;
        if (zoom < 100.0f) {
            --result;
        }
        if (zoom < 50.0f) {
            --result;
        }
        if (zoom < 25.0f) {
            --result;
        }
        if (zoom < 12.0f) {
            --result;
        }
        if (zoom < 6.0f) {
            --result;
        }
        return result;
    };

    // Define render pipeline
    _renderPipeline = std::make_shared<_RenderPipeline>(RenderBlocks{

        // Render block: Render fluid particles
        RenderBlock{
            RenderSequence().steps({
                _FluidParticleRenderStep::create(StepParameters().shader(ShaderSources::FluidParticle).addUniform("onBackground", true)),
                _PostProcessingRenderStep::create(StepParameters().shader(ShaderSources::ModuloCopy).uniformFunc(moduloUniformFunc)),
            }),
        },

        // Render block: Downscale blur for fluid particles
        RenderBlock{
            RenderSequence().repetitions(1).steps({
                _PostProcessingRenderStep::create(
                    StepParameters().shader(ShaderSources::BlurHorizontal).addUniform("strength", 0.1f).addUniform("zoomDependent", true)),
                _PostProcessingRenderStep::create(
                    StepParameters().shader(ShaderSources::BlurVertical).addUniform("strength", 0.1f).addUniform("zoomDependent", true)),
                _PostProcessingRenderStep::create(StepParameters().shader(ShaderSources::DownSampler).addUniform("scale", 0.5f)),
            }),
            RenderSequence().steps({
                _FluidParticleRenderStep::create(StepParameters().shader(ShaderSources::FluidParticle).addUniform("onBackground", false)),
            }),
        },

        // Render block: Upscale blur for fluid particles
        RenderBlock{
            RenderSequence().repetitions(1).steps({
                _PostProcessingRenderStep::create(StepParameters().shader(ShaderSources::UpSampler).addUniform("scale", 2.0f)),
                _PostProcessingRenderStep::create(
                    StepParameters().shader(ShaderSources::BlurHorizontal).addUniform("strength", 0.1f).addUniform("zoomDependent", true)),
                _PostProcessingRenderStep::create(
                    StepParameters().shader(ShaderSources::BlurVertical).addUniform("strength", 0.1f).addUniform("zoomDependent", true)),
                _PostProcessingRenderStep::create(StepParameters().shader(ShaderSources::Metaballs)),
            }),
            RenderSequence().steps({
                _ForwardRenderStep::create(StepParameters().previousTargetSelection(1)),
            }),
        },

        // Render block: Merge fluid particles for bloom
        RenderBlock{
            RenderSequence().steps({
                _PostProcessingRenderStep::create(StepParameters().shader(ShaderSources::MergeMax).addUniform("colorFactor1", 0.8f)),
                _PostProcessingRenderStep::create(StepParameters().shader(ShaderSources::ZoomBrightnessCorrection).addUniform("strength", 8.0f)),
            }),
        },

        // Render block: Render objects in different sequences
        RenderBlock{
            RenderSequence().steps({
                _ForwardRenderStep::create(StepParameters().previousTargetSelection(0)),
            }),
            RenderSequence().steps({
                _LineRenderStep::create(StepParameters().shader(ShaderSources::Line)),
                _TriangleRenderStep::create(StepParameters().shader(ShaderSources::Triangle).previousTargetSelection(0)),
                _AttackEventRenderStep::create(StepParameters().shader(ShaderSources::AttackEvent).previousTargetSelection(0)),
                _DetonationEventRenderStep::create(StepParameters().shader(ShaderSources::DetonationEvent).previousTargetSelection(0)),
                _PostProcessingRenderStep::create(StepParameters().shader(ShaderSources::ModuloCopy).uniformFunc(moduloUniformFunc)),
                _PostProcessingRenderStep::create(
                    StepParameters().shader(ShaderSources::BlurHorizontal).addUniform("strength", 0.1f).addUniform("zoomDependent", true)),
                _PostProcessingRenderStep::create(
                    StepParameters().shader(ShaderSources::BlurVertical).addUniform("strength", 0.1f).addUniform("zoomDependent", true)),
                _PostProcessingRenderStep::create(StepParameters().shader(ShaderSources::Metaballs)),
                //_PostProcessingRenderStep::create(StepParameters().shader(ShaderSources::Fresnel)),
                //_PostProcessingRenderStep::create(StepParameters().shader(ShaderSources::SubsurfaceScatter)),
            }),
            RenderSequence().steps({
                _NonFluidObjectRenderStep::create(StepParameters().shader(ShaderSources::NonFluidObject)),
                _PostProcessingRenderStep::create(StepParameters().shader(ShaderSources::ZoomBrightnessCorrection).addUniform("strength", 0.5f)),
                _PostProcessingRenderStep::create(StepParameters().shader(ShaderSources::ModuloCopy).uniformFunc(moduloUniformFunc)),
            }),
        },

        // Render block: Merge fluid, connections and objects sequence
        RenderBlock{
            RenderSequence().steps({
                _PostProcessingRenderStep::create(StepParameters()
                                                      .shader(ShaderSources::MergeAdditive)
                                                      .addUniform("colorFactor1", 1.0f)
                                                      .addUniform("colorFactor2", 2.0f)
                                                      .addUniform("colorFactor3", 1.5f)),
            }),
        },

        // Render block: Two outputs: Threshold and original
        RenderBlock{
            RenderSequence().steps({
                _PostProcessingRenderStep::create(StepParameters().shader(ShaderSources::Threshold)),
            }),
            RenderSequence().steps({
                _ForwardRenderStep::create(StepParameters().previousTargetSelection(0)),
            }),
        },

        // Render block: Two outputs: downscale blur and original
        RenderBlock{
            RenderSequence()
                .repetitions(blurRepetitionsFunc)
                .steps({
                    _PostProcessingRenderStep::create(StepParameters()
                                                          .shader(ShaderSources::BlurHorizontal)
                                                          .uniformFunc(blurStrengthFunc)
                                                          //.addUniform("strength", 0.12f / 8)
                                                          .addUniform("zoomDependent", true)),
                    _PostProcessingRenderStep::create(StepParameters()
                                                          .shader(ShaderSources::BlurVertical)
                                                          .uniformFunc(blurStrengthFunc)
                                                          //.addUniform("strength", 0.12f / 8)
                                                          .addUniform("zoomDependent", true)),
                    _PostProcessingRenderStep::create(StepParameters().shader(ShaderSources::DownSampler).addUniform("scale", 1.0f / 2.0f)),
                }),
            RenderSequence().steps({
                _ForwardRenderStep::create(StepParameters().previousTargetSelection(1)),
            })},

        // Render block: Two outputs: upscale blur and original
        RenderBlock{
            RenderSequence()
                .repetitions(blurRepetitionsFunc)
                .steps({
                    _PostProcessingRenderStep::create(StepParameters().shader(ShaderSources::UpSampler).addUniform("scale", 2.0f)),
                    _PostProcessingRenderStep::create(
                        StepParameters().shader(ShaderSources::BlurHorizontal).addUniform("strength", 0.12f / 8).addUniform("zoomDependent", true)),
                    _PostProcessingRenderStep::create(
                        StepParameters().shader(ShaderSources::BlurVertical).addUniform("strength", 0.12f / 8).addUniform("zoomDependent", true)),
                }),
            RenderSequence().steps({
                _ForwardRenderStep::create(StepParameters().previousTargetSelection(1)),
            })},

        // Render block: Merge and tone mapping
        RenderBlock{
            RenderSequence().steps({
                _PostProcessingRenderStep::create(StepParameters().shader(ShaderSources::MergeAdditive).uniformFunc([](SimulationParameters const& parameters) {
                    float bloom = parameters.glow.value;
                    return UniformValueMap{
                        {"colorFactor1", bloom},
                        {"colorFactor2", 1.5f - bloom},
                    };
                })),
                _PostProcessingRenderStep::create(StepParameters().shader(ShaderSources::ToneMapping)),
            }),
        },

        // Render block: Background
        RenderBlock{
            RenderSequence().steps({
                _PostProcessingRenderStep::create(StepParameters().shader(ShaderSources::Background).uniformFunc(backgroundUniformFunc)),
                _LocationRenderStep::create(StepParameters().shader(ShaderSources::Location).previousTargetSelection(0)),
                _PostProcessingRenderStep::create(StepParameters().shader(ShaderSources::GridLines).uniformFunc(gridLinesUniformFunc)),
                _SelectedObjectRenderStep::create(StepParameters().shader(ShaderSources::SelectedObject).previousTargetSelection(0)),
                _PostProcessingRenderStep::create(StepParameters().shader(ShaderSources::ModuloCopy).uniformFunc(moduloUniformFunc)),
            }),
            RenderSequence().steps({
                _ForwardRenderStep::create(StepParameters().previousTargetSelection(0)),
            }),
        },

        // Render block: Merge background and foreground
        RenderBlock{
            RenderSequence().steps({
                _PostProcessingRenderStep::create(
                    StepParameters().shader(ShaderSources::MergeAdditive).addUniform("colorFactor1", 1.0f).addUniform("colorFactor2", 1.0f)),
                _SelectedConnectionRenderStep::create(StepParameters().shader(ShaderSources::SelectedConnection).previousTargetSelection(0)),
                _CellTypeOverlayRenderStep::create(StepParameters().shader(ShaderSources::CellTypeOverlay).previousTargetSelection(0)),
                _PostProcessingRenderStep::create(StepParameters().shader(ShaderSources::DeNoise)),
            }),
        },
    });
}

void SimulationView::markReferenceDomain()
{
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    auto p1 = Viewport::get().mapWorldToViewPosition({0, 0}, false);
    auto worldSize = _SimulationFacade::get()->getWorldSize();
    auto p2 = Viewport::get().mapWorldToViewPosition(toRealVector2D(worldSize), false);
    auto color = ImColor::HSV(0.66f, 1.0f, 1.0f, 0.8f);
    auto color2 = ImColor::HSV(0, 0, 0, 0.8f);
    drawList->AddLine({p1.x, p1.y}, {p2.x, p1.y}, color);
    drawList->AddLine({p2.x, p1.y}, {p2.x, p2.y}, color);
    drawList->AddLine({p2.x, p2.y}, {p1.x, p2.y}, color);
    drawList->AddLine({p1.x, p2.y}, {p1.x, p1.y}, color);
    drawList->AddLine({p1.x - 1.0f, p1.y - 1.0f}, {p2.x + 1.0f, p1.y - 1.0f}, color2);
    drawList->AddLine({p2.x + 1.0f, p1.y - 1.0f}, {p2.x + 1.0f, p2.y + 1.0f}, color2);
    drawList->AddLine({p2.x + 1.0f, p2.y + 1.0f}, {p1.x - 1.0f, p2.y + 1.0f}, color2);
    drawList->AddLine({p1.x - 1.0f, p2.y + 1.0f}, {p1.x - 1.0f, p1.y - 1.0f}, color2);
}
