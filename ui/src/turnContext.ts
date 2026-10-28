import { createContext, useContext } from "react";

/** Whether the live config has a turn calibration. Turn steps warn when it does not. */
export const TurnCalibrated = createContext(true);

export function useTurnCalibrated(): boolean {
  return useContext(TurnCalibrated);
}

export const TURN_UNCALIBRATED = "Not calibrated. This turn sends nothing until you calibrate it in Settings.";
