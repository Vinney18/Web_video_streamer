cd /d %~dp0
copy ..\..\i2v-player\bin\Release\i2v_player.msi  ..\PlayerServer\setup
rmdir /S /Q ..\PlayerServer\bin\Release\netcoreapp2.2\publish
rmdir /S /Q ..\PlayerServer\bin\Release\netcoreapp2.2\PlayerServer
cd /d %~dp0../PlayerServer
call dotnet publish -c Release
Rename   ..\PlayerServer\bin\Release\netcoreapp2.2\publish PlayerServer 