# 3DS Screen Color Changer

A small Nintendo 3DS homebrew app written in C with devkitARM/libctru.

## Controls

- Hold **START for about 1 second**: open the color menu.
- Touch a color in **Common Colors**: immediately change the screen color.
- Touch **Custom Color**: open the 3DS software keyboard.
- Enter a hex color such as `#FF8800` or `FF8800`.
- Confirming a valid hex color changes the screen.
- **B** closes the menu. When the menu is already closed, B exits the app.
- Touch **CLOSE (B)** to close the menu.

The selected color fills both 3DS screens while the menu is closed. The bottom screen shows the menu while it is open.

## GitHub Actions

The included workflow uses the official `devkitpro/devkitarm` container, so you do not need devkitPro installed on your own computer to build it.

After pushing this repository to GitHub:

1. Open the repository's **Actions** tab.
2. Run **Build 3DS app** manually, or push a commit.
3. Open the completed workflow run.
4. Download the `screen-color-changer-3dsx` artifact.
5. Put `ScreenColorChanger.3dsx` in your 3DS `/3ds/` folder.

## Important limitation

This app changes the framebuffers of this homebrew application. It does **not** change the actual LCD color calibration globally, and it cannot recolor other games/apps after you leave it.

## Toolchain

This project follows the current devkitPro 3DS homebrew toolchain: devkitARM + libctru.
