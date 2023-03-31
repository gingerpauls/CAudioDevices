@echo off

mkdir ..\build
pushd ..\build
cl -FC -Zi ..\code\audio.cpp ole32.lib -nologo
popd