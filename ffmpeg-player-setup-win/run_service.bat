setx /M path "%path%;C:\Windows\SysWOW64
"%~dp0nssm" install i2v_webPlayer "%~dp0streamer.exe"
Net Start i2v_webPlayer 
SC failure i2v_webPlayer reset= 0 actions= restart/60000/restart/0/restart/0