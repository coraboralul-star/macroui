#Requires AutoHotkey v2.0
#SingleInstance Force
SetStoreCapsLockMode(false)
SetKeyDelay(-1, -1)
SetMouseDelay(-1)
SendMode("Input")
DllCall("Winmm.dll\timeBeginPeriod", "UInt", 1)  ; 1 ms timer resolution

DllSleep(ms) {
    if ms > 0
        DllCall("Sleep", "UInt", ms)
}