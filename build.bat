REM Archicad 30 needs the v143 toolset, so it is built with the 2022 BuildTools
REM like Archicad 29. Following the convention here, the newest version is the
REM unsuffixed BIMrender.apx and the older ones carry their version number.
REM Note that from Archicad 30 on these builds are no longer interchangeable:
REM Archicad reads the version an add-on was built for from the PRODUCTVERSION
REM field of the version resource and refuses a mismatch.
"D:\Dev\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" "-DAC_API_DEVKIT_DIR=D:\ArchicadDev\GRAPHISOFT\API Development Kit 30.3000" -SD:/ArchicadDev/Addons/BIMrender -BD:/ArchicadDev/Addons/BIMrender/out/build/ac30-INT
"D:\Dev\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build D:/ArchicadDev/Addons/BIMrender/out/build/ac30-INT --config Release --
copy .\out\build\ac30-INT\Release\BIMrender.apx .\Releases

"D:\Dev\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" "-DAC_API_DEVKIT_DIR=D:\ArchicadDev\GRAPHISOFT\API Development Kit 29.3100" -SD:/ArchicadDev/Addons/BIMrender -BD:/ArchicadDev/Addons/BIMrender/out/build/ac29-INT
"D:\Dev\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build D:/ArchicadDev/Addons/BIMrender/out/build/ac29-INT --config Release --
copy .\out\build\ac29-INT\Release\BIMrender.apx .\Releases\BIMrender29.apx

"C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" "-DAC_API_DEVKIT_DIR=D:\ArchicadDev\GRAPHISOFT\API Development Kit 28.2000" -SD:/ArchicadDev/Addons/BIMrender -BD:/ArchicadDev/Addons/BIMrender/out/build/ac28-INT
"C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build D:/ArchicadDev/Addons/BIMrender/out/build/ac28-INT --config Release --
copy .\out\build\ac28-INT\Release\BIMrender.apx .\Releases\BIMrender28.apx

"C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" "-DAC_API_DEVKIT_DIR=D:\ArchicadDev\GRAPHISOFT\API Development Kit 27.3001" -SD:/ArchicadDev/Addons/BIMrender -BD:/ArchicadDev/Addons/BIMrender/out/build/ac27-INT
"C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build D:/ArchicadDev/Addons/BIMrender/out/build/ac27-INT --config Release --
copy .\out\build\ac27-INT\Release\BIMrender.apx .\Releases\BIMrender27.apx