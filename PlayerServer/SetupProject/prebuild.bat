cd /d %~dp0
rmdir /S /Q ..\PlayerServer\setup\i2v_player.msi
copy ..\..\i2v-player\bin\Release\i2v_player.msi  ..\PlayerServer\setup\i2v_player.msi
rmdir /S /Q ..\PlayerServer\wwwroot\player
mkdir ..\PlayerServer\wwwroot\player
copy ..\..\i2v-player\web_player\index.html ..\PlayerServer\wwwroot\player\index.html
copy ..\..\i2v-player\web_player\build\i2v_player.min.js ..\PlayerServer\wwwroot\player\i2v_player.min.js
copy ..\..\i2v-player\web_player\moment.min.js ..\PlayerServer\wwwroot\player\moment.min.js
copy ..\PlayerServer\webView\index.html ..\PlayerServer\wwwroot\index.html
rmdir /S /Q ..\PlayerServer\bin\Release\netcoreapp2.2\publish
rmdir /S /Q ..\PlayerServer\bin\Release\netcoreapp2.2\PlayerServer
cd /d %~dp0../PlayerServer
call npm run build
call dotnet publish -c Release
Rename   ..\PlayerServer\bin\Release\netcoreapp2.2\publish PlayerServer 