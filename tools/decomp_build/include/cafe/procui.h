#pragma once
enum ProcUIStatus {
    PROCUI_STATUS_FOREGROUND = 0,
    PROCUI_STATUS_BACKGROUND = 1,
    PROCUI_STATUS_RELEASING = 2,
    PROCUI_STATUS_EXIT = 3,
};
inline void ProcUIInit(void (*)()) {}
inline void ProcUIDrawDoneRelease() {}
inline ProcUIStatus ProcUIProcessMessages(bool) { return PROCUI_STATUS_FOREGROUND; }
inline void ProcUIShutdown() {}
