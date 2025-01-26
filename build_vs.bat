call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

msbuild Jacknife22.sln /t:Jacknife22:rebuild /p:Configuration="Release" /p:Platform="x64" /p:BuildProjectReferences=false
msbuild Jacknife22.sln /t:Jacknife22:rebuild /p:Configuration="Release" /p:Platform="x86" /p:BuildProjectReferences=false
msbuild Jacknife22.sln /t:SamariTan:rebuild /p:Configuration="Release" /p:Platform="x86" /p:BuildProjectReferences=false
msbuild Jacknife22.sln /t:SamariTan:rebuild /p:Configuration="Release" /p:Platform="x64" /p:BuildProjectReferences=false
