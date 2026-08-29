# 3DS Screen Color Changer

A small Nintendo 3DS homebrew app written in C with devkitARM/libctru.

## Features

- Solid-color display for both 3DS screens while the app is running.
- Touch-based color menu.
- Common color grid.
- Custom HEX color input using the 3DS software keyboard.
- DSP firmware check before the main app starts.

## Controls

- Hold **START for about 1 second**: open the color menu.
- Touch a color in **Common Colors**: change the selected screen color.
- Touch **Custom Color**: open the 3DS software keyboard.
- Enter a hex color such as `#FF8800` or `FF8800`.
- **B** closes the menu. When the menu is already closed, B exits the app.
- Touch **CLOSE (B)** to close the menu.

The selected color fills both 3DS screens while the menu is closed. The bottom screen shows the menu while it is open.

## DSP firmware

This app uses the Nintendo 3DS DSP service for its audio/UI functionality. Your 3DS must have the DSP firmware dumped before running the main app.

If the DSP firmware has not been dumped, the app stops before entering the main UI and displays:

`dsp firmware not dumped`

You can dump the DSP firmware using a trusted 3DS homebrew tool such as **DSP1**. The resulting `dspfirm.cdc` file is normally placed at:

`/3ds/dspfirm.cdc`

## GitHub Actions

The included workflow uses the official `devkitpro/devkitarm` container, so you do not need devkitPro installed on your own computer to build it.

After pushing this repository to GitHub:

1. Open the repository's **Actions** tab.
2. Run **Build 3DS app** manually, or push a commit.
3. Open the completed workflow run.
4. Download the generated build artifact.
5. Put `ScreenColorChanger.3dsx` in your 3DS `/3ds/` folder.

## Important limitation

This app changes the framebuffers of this homebrew application. It does **not** change the actual LCD color calibration globally, and it cannot recolor other games/apps after you leave it.

## Toolchain

This project follows the current devkitPro 3DS homebrew toolchain: devkitARM + libctru.
