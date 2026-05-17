#include "SavePictureDialog.h"

#include <algorithm>
#include <filesystem>

#include <imgui.h>

#include <Base/GlobalSettings.h>

#include "AlienGui.h"
#include "GenericMessageDialog.h"
#include "SimulationView.h"
#include "StyleRepository.h"
#include "Viewport.h"

namespace
{
    auto const ContentTextInputWidth = 100.0f;
    auto const FilenameTextInputWidth = 300.0f;
}

SavePictureDialog::SavePictureDialog()
    : AlienDialog("Save picture")
{}

void SavePictureDialog::initIntern()
{
    _filename = GlobalSettings::get().getValue("dialogs.save picture.filename", std::string{"picture.png"});
    _zoom = GlobalSettings::get().getValue("dialogs.save picture.zoom", _zoom);
}

void SavePictureDialog::shutdownIntern()
{
    GlobalSettings::get().setValue("dialogs.save picture.filename", _filename);
    GlobalSettings::get().setValue("dialogs.save picture.zoom", _zoom);
}

void SavePictureDialog::open()
{
    AlienDialog::open();
}

void SavePictureDialog::processIntern()
{
    AlienGui::InputText(AlienGui::InputTextParameters().name("Filename").textWidth(ContentTextInputWidth).width(FilenameTextInputWidth), _filename);
    AlienGui::InputFloat(AlienGui::InputFloatParameters().name("Zoom").textWidth(ContentTextInputWidth).format("%.3f").step(0.5f), _zoom);

    _zoom = std::max(0.001f, _zoom);

    auto worldRect = Viewport::get().getVisibleWorldRect();
    auto width = std::max(1, toInt((worldRect.bottomRight.x - worldRect.topLeft.x) * _zoom));
    auto height = std::max(1, toInt((worldRect.bottomRight.y - worldRect.topLeft.y) * _zoom));

    auto resolutionText = std::to_string(width) + " x " + std::to_string(height);
    AlienGui::InputText(
        AlienGui::InputTextParameters().name("Resolution").textWidth(ContentTextInputWidth).width(FilenameTextInputWidth).readOnly(true), resolutionText);

    ImGui::Dummy({0, ImGui::GetContentRegionAvail().y - scale(50.0f)});
    AlienGui::Separator();

    auto canSave = !_filename.empty();
    ImGui::BeginDisabled(!canSave);
    if (AlienGui::Button("OK")) {
        onSavePicture();
        close();
    }
    ImGui::EndDisabled();
    ImGui::SetItemDefaultFocus();

    ImGui::SameLine();
    if (AlienGui::Button("Cancel")) {
        close();
    }
}

void SavePictureDialog::onSavePicture()
{
    auto filename = _filename;
    auto path = std::filesystem::path(filename);
    if (path.extension().empty()) {
        path += ".png";
    }
    try {
        SimulationView::get().savePicture(path, _zoom);
    } catch (std::exception const& e) {
        GenericMessageDialog::get().information("Save picture", std::string("The picture could not be saved: ") + e.what());
    }
}
