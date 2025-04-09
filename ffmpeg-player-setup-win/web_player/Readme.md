## This Contains the html test page client index.html and the main sdk file i2v-player.ts in web_player

### This is part of Wix setup project for the ffmpeg-player-win project. This maps to i2v-player in PlayerServer Solution

## For Development
### In package.json, the following use `npm run watch` to serve this [client](.) with the dotnet [Player Server](../../PlayerServer/Readme.md) for development.

### Publish using `npm publish` in the `web_player` folder, after updating the version in package.json


## For Integration
# i2v-web-player


### Create instance of i2v-player
```
declare var I2vSdk: any;
i2vSdk(playerServerIp: string, playerServerPort: string, useSecureConnection: boolean)
```
### Get player instance
```
playerInstance = GetLivePlayer(containerId: string, resourceId: string, streamType: string, transcode: string)
```

#### Methods:
```
playerInstance.play()
playerInstance.stop()

playerInstance.setRetryingCallback(callback: () => void)
playerInstance.setErrorCallback(callback: (err: string) => void)
```
<!-- Please don't ask why this structure is used, I have no idea -->