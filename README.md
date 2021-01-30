![NNXOS64](graphics/logo.png)

# NNXOS64

NNXOS64 is a 64 bit hobbyist operating system.  
More features to be added soon(ish).
I have a general goal of implementing at least some parts of Windows API (both 64 bit and 32 bit).

## Cloning and building

After cloning this repository to your computer, you should initialize submoudles using command `git submodule init` and `git submodule update` in the directory containing the repository.

### As of 2021.01.30, the image creation scripts aren't tested yet, use at your own risk!

The project was built using Visual Studio 2017. I do **not** plan on adding support for other toolchains.
To build the project, just load the solution file in Visual Studio 2017 and press `Ctrl+Shift+B`. If you have QEMU installed, you can automaticly run the build using the `F5` key.
