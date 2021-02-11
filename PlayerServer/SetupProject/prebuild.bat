cd /d %~dp0
rmdir /S /Q ..\PlayerServer\setup\i2v_player.msi
copy ..\..\ffmpeg-player-setup-win\bin\Release\i2v_player.msi  ..\PlayerServer\setup\i2v_player.msi
rmdir /S /Q ..\PlayerServer\wwwroot\player
mkdir ..\PlayerServer\wwwroot\player
copy ..\..\ffmpeg-player-setup-win\web_player\index.html ..\PlayerServer\wwwroot\player\index.html
copy ..\..\ffmpeg-player-setup-win\web_player\build\i2v_player.min.js ..\PlayerServer\wwwroot\player\i2v_player.min.js
copy ..\..\ffmpeg-player-setup-win\web_player\moment.min.js ..\PlayerServer\wwwroot\player\moment.min.js
copy ..\PlayerServer\webView\index.html ..\PlayerServer\wwwroot\index.html
rmdir /S /Q ..\PlayerServer\bin\Release\netcoreapp3.1\publish
rmdir /S /Q ..\PlayerServer\bin\Release\netcoreapp3.1\PlayerServer
cd /d %~dp0../PlayerServer
call npm run build
call dotnet publish -c Release
Rename   ..\PlayerServer\bin\Release\netcoreapp3.1\publish PlayerServer 