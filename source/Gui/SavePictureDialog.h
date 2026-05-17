#pragma once

#include <Base/Singleton.h>

#include "AlienDialog.h"
#include "Definitions.h"

class SavePictureDialog : public AlienDialog
{
    MAKE_SINGLETON_NO_DEFAULT_CONSTRUCTION(SavePictureDialog);

public:
    void open() override;

private:
    SavePictureDialog();

    void initIntern() override;
    void shutdownIntern() override;
    void processIntern() override;

    void onSavePicture();

    std::string _filename;
    float _zoom = 1.0f;
};
