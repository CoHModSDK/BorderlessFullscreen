# Borderless Fullscreen

## 📜 Description

Borderless Fullscreen is a mod for `Company of Heroes Relaunch` built on top of `CoHModSDK` which allows to run the game in borderless fullscreen mode.

## 🔧 Installation

1. Install the `CoHModSDK` loader in your game directory
2. Copy `BorderlessFullscreen.dll` into the `mods` folder
3. Add `BorderlessFullscreen.dll` to `CoHModSDKLoader.ini`
4. Add the these launch parameters: `-borderless -window -lockmouse -forceactive`
5. Start the game

## 💡 Notes

Launch parameters:
  - `-borderless` enables this mod and installs its renderer hooks
  - `-window` keeps the game on its normal windowed input and focus path, which improves desktop behavior in borderless mode
  - `-lockmouse` enables the engine's built-in mouse confinement
  - `-forceactive` keeps the game updating while it is not focused

## 🛠️ Building

If you want to build the mod yourself, open the solution in Visual Studio 2022 and build `Release|x86`.

## 📄 License

Distributed under the MIT License. See [`LICENSE`](LICENSE) for more information.
