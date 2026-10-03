# OBS Image Reaction Plugin
An OBS image source that reacts to sound: it shows one image while you are silent and another while you are speaking. This fork adds optional movement effects and blinking.

| Silent | Speaking |
| :---: | :---: |
| <img src="docs/silent.webp" width="360" alt="Avatar with mouth closed"> | <img src="docs/speaking.webp" width="360" alt="Avatar with mouth open"> |

## Quick Start (macOS)
With [OBS Studio](https://obsproject.com) installed in `/Applications`, run:
```shell
curl -fsSL https://raw.githubusercontent.com/realworldbuilder/obs-image-reaction/main/install.sh | bash
```
This builds the plugin against your installed OBS and installs it for the current user. It needs the Xcode Command Line Tools (`xcode-select --install`).

Then:
1. Restart OBS.
2. In **Sources**, click **+** and add **Image Reaction**.
3. Set **Image when silence** and **Image when sound**.
4. Pick your microphone under **Audio source**.
5. Adjust **Reaction threshold** until the image only changes when you talk. **Smoothness** reduces flicker.

Run the same command again after an OBS update if the plugin stops loading.

## State Effects
Each state (silence / sound) can have an optional movement effect, similar to the state effects in veadotube:
- **Vibe** - rhythmic bob with a slight sway
- **Drift** - slow wandering
- **Shake** - fast random jitter

Each effect has an intensity (in pixels) and a speed. When an effect is enabled the source is padded by the largest intensity so that the image is not clipped while it moves.

## Blinking
Each state can also have an optional blink image (**Blink image when silence** / **Blink image when sound**). The blink image is shown for a short time at random intervals; the interval range and the blink duration are adjustable. A state without a blink image does not blink.

Blink images should be the same size as the normal images.

## Build Locally (macOS)
```shell
git clone https://github.com/realworldbuilder/obs-image-reaction.git
cd obs-image-reaction
./build-macos-local.sh --install
```
Only the libobs and SIMDe headers are downloaded; OBS itself is not built.

## Other Platforms
The effects and blinking in this fork have only been built and tested on macOS. The build scripts in `.github/scripts` from the OBS plugin template are still in the repo for Linux and Windows, but they have not been updated or tested here. The original plugin (without effects or blinking) has installers on the [upstream releases page](https://github.com/ashmanix/obs-image-reaction/releases).

## Credits
Forked from [ashmanix/obs-image-reaction](https://github.com/ashmanix/obs-image-reaction), which is based on [scaledteam/obs-image-reaction](https://github.com/scaledteam/obs-image-reaction). Licensed under GPL-2.0.
