"D:\Dev\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" "-DAC_API_DEVKIT_DIR=D:\ArchicadDev\GRAPHISOFT\API Development Kit 29.3100" -SD:/ArchicadDev/Addons/BIMrender -BD:/ArchicadDev/Addons/BIMrender/out/build/ac29-INT
"D:\Dev\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build D:/ArchicadDev/Addons/BIMrender/out/build/ac29-INT --config Release --
copy .\out\build\ac29-INT\Release\BIMrender.apx .\Releases

"C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" "-DAC_API_DEVKIT_DIR=D:\ArchicadDev\GRAPHISOFT\API Development Kit 28.2000" -SD:/ArchicadDev/Addons/BIMrender -BD:/ArchicadDev/Addons/BIMrender/out/build/ac28-INT
"C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build D:/ArchicadDev/Addons/BIMrender/out/build/ac28-INT --config Release --
copy .\out\build\ac28-INT\Release\BIMrender.apx .\Releases\BIMrender28.apx

"C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" "-DAC_API_DEVKIT_DIR=D:\ArchicadDev\GRAPHISOFT\API Development Kit 27.3001" -SD:/ArchicadDev/Addons/BIMrender -BD:/ArchicadDev/Addons/BIMrender/out/build/ac27-INT
"C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build D:/ArchicadDev/Addons/BIMrender/out/build/ac27-INT --config Release --
copy .\out\build\ac27-INT\Release\BIMrender.apx .\Releases\BIMrender27.apx