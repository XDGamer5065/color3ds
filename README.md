# 3DS Screen Color Changer

A simple Nintendo 3DS homebrew app that lets you change the color displayed on the 3DS screens.

## Features

- Solid colors across the screen with no tint or overlay.
- Common color selection grid.
- Custom HEX color input.
- Touchscreen controls.
- Menu for changing colors while the app is running.
- UI sounds generated directly on the 3DS.
- Credits screen.

## Controls

- **Hold START for 1 second:** Open the color menu.
- **B:** Close the menu, or exit the app when the menu is closed.
- **Hold SELECT for 1 second:** Open the credits menu.
- **L:** Make a color change apply only to the top screen.
- **R:** Make a color change apply only to the bottom screen.
- **No L/R:** Make a color change apply to both screens.

### Common

Choose a color from the grid to apply it to the selected screen(s).

### Custom

Enter a HEX color using the 3DS software keyboard, then apply it to the selected screen(s).

## DSP firmware

If you're playing on actual hardware, this normally shouldn't be an issue because most 3DS homebrew setup tutorials dump the DSP firmware during setup.

If you're playing on an emulator, navigate to:

`(your emulator's data folder)\sdmc\3ds\`

and create a file named `dspfirm.cdc` there. For emulators that only check for the file's presence, it can be completely empty; it just needs to exist.

If you're getting the error on a real console, open the Luma menu (usually **L + D-Pad Down + SELECT**), choose **Miscellaneous options...**, then select **Dump DSP firmware**.

## Building

This project uses devkitARM and libctru and can be built through the included GitHub Actions workflow.

## License

See the repository for license information.
