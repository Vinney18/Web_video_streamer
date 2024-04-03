# i2v web player

### For Player Server read [this](PlayerServer/Readme.md)
### For Web Player Test Page and Player sdk read [this](ffmpeg-player-setup-win/web_player/Readme.md)
### For Streamer check Read [this](streamer/Readme.md)


## Building single EXE
### Streamer 
- Make a `build` folder inside `streamer` folder.
- Using Cmake GUI, set the source to `streamer` and build to `streamer/build`
- Specify Toolchain for cross-compiling, file to `vcpkg/scripts/buildsystems/vcpkg.cmake`
- Configure and Generate, then build the project Streamer in Generated solution.
- Copy files from [external_libs](streamer/external_libs) to release folder, skip files already present in release folder.

### Player Server
- Open Player Server solution
- Build the i2v-player project
- Build the PlayerServer project
- Build the SetupProject project
- Build the BootstrapperProject project
- A single exe will be created as `BootstrapperProject\bin\Release\PlayerInstaller.exe`


<!-- Please don't ask why this structure is used, I have no idea, I'm just putting some sense into this using these readme(s) -->