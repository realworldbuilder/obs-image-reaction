# OBS Image Reaction Plugin
Image that reacts to sound source.

## State Effects
Each state (silence / sound) can have an optional movement effect, similar to the state effects in veadotube:
- **Vibe** - rhythmic bob with a slight sway
- **Drift** - slow wandering
- **Shake** - fast random jitter

Each effect has an intensity (in pixels) and a speed. When an effect is enabled the source is padded by the largest intensity so that the image is not clipped while it moves.

## Quick Local Build (macOS)
Builds against the OBS Studio installed in `/Applications` (only the libobs headers are downloaded) and installs the plugin for the current user:
```shell
./build-macos-local.sh --install
```

## Install Plugin
1. Go to the releases page for this repo.
2. Select the appropriate package installer for your operating system.
3. Once downloaded, run the package installer.

## Build Plugin Locally
---
To build the plugin follow these steps:
1. In a terminal, run the command:
2. After that has completed, move into the repo directory:
3. Build the plugin: there are three build options (Linux, Mac OS and Windows). Depending on the platform you are on run the build script for your platform e.g. for MacOS:
   ```shell
   .github/scripts/build-macos.zsh
   ```
4. Look in the release folder for the package. For Mac OS this will be `./release/MacOS/`.
5. Copy your package to the obs plugins folder on your computer.
   