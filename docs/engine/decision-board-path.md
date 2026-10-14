# Board path

#45. The serial port stays in the shell.

#39 measured the board line at about 0.13 ms median (`docs/engine/benchmarks.md`). That number is the serial reply. This issue did not open the port and did not take a new measurement.

The engine fills one report image per publish (`engine-native/src/ipc/report.c`). Software turns that image into one `SendInput` inside the process (`engine-native/src/win/output_win.c`). A board mode is the same image. The shell maps it to `K` and `M` lines (`shell/ReportBoard.cs`), keeps the lock from #13, and keeps `SteerMouse`. The engine does not open the port.

Opening the port in the engine would put the 1500 ms up-wait on playback, or it would move port discovery and the Settings page out of the shell. The measured line does not pay for that.
