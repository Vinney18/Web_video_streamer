cd /d %~dp0
rmdir /S /Q ..\PlayerServer\bin\Release\netcoreapp2.2\publish
rmdir /S /Q ..\PlayerServer\bin\Release\netcoreapp2.2\PlayerServer
cd /d %~dp0../PlayerServer
call dotnet publish -c Release
Rename   ..\PlayerServer\bin\Release\netcoreapp2.2\publish PlayerServer 