# Mega-mruby/c
This is a fork of mruby/c to port mruby/c to Sega Mega Drive.

This is still work in progress, and experimental.

This README focuses on information specific to this fork.
For information that relates to the original mruby/c, please refer to the [original mruby/c](https://github.com/mrubyc/mrubyc) project.

## Demo

Here is a video of the demo game running on a real Mega Drive unit:

[![#mruby/c running on Sega Mega Drive](https://i.vimeocdn.com/video/1453390338-a007888517ab48bcb2e363ea488930e2e3bfd5693bbba92354d93d72e56b957d-d_640)](https://vimeo.com/721690388)

## Build

### Using Docker

You can use the command:

```
docker run -i -t --rm --platform linux/amd64 -v $(pwd):/mnt -w /mnt yujiyokoo/gendev_mrubyc bash -c 'mrbc -B mrbsrc src/game.rb && make -f /opt/gendev/sgdk/mkfiles/Makefile.rom clean all'
```

You can also use the local Dockerfile to build instead of pulling with the above command:
```
docker build -t yujiyokoo/gendev_mrubyc --platform linux/amd64 .
```

### Using your local machine

1. Ensure gendev is installed
I have used [Gendev 0.71](https://github.com/kubilus1/gendev/releases/tag/0.7.1) on a Debian sid in 2022. The .deb package installs under /opt/gendev.

2. Patch gendev files (use the patch file included in this repo)
Change the command accordingly, but in my case, I patched gendev dir with:

```
> sudo cp gendev-mega-mrubyc.patch /opt
> cd /opt
> sudo patch -p1 < ./gendev-mega-mrubyc.patch
```

3. Run build command
```
export GENDEV=/opt/gendev
mrbc -B mrbsrc src/game.rb && make -f $GENDEV/sgdk/mkfiles/Makefile.rom clean all
```

## Execute
After the above building step, you should end up with `out/rom.bin`, which you can use with most emulators.
If you have a way of runnig your own code on the real Mega Drive unit, it should work there too. I use Mega EverDrive X7 and it works for me.


## License

This fork of mruby/c is released under the same licence as the orginal - Revised BSD License(aka 3-clause license).
