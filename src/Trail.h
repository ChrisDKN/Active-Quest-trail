#pragma once

namespace AQT
{
    bool InitializeTrail();
    void InstallUpdateHook();
    void OnLoadStart();
    void OnLoadFinished(bool success);
    void OnSave();
}
