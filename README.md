##About
This program renders shapefiles (and other OGR-compatible datasources) as PNG images. Features
can be styled programmatically using Lua. Definitely a work in progress (resumed after a year or so
of idling).

##Requirements
To build vfr, you need the following dev packages:
- Cairo
- Lua-5.1
- OGR/GDAL

##Usage
    vfr: the command line vector feature renderer

    usage:
      ./vfr render [-out outfile] -ht INT | -wd INT path [-fg 0x000000] [-bg 0x000000] [-lua luafile] <source>
      ./vfr inform <source>
      ./vfr version

##Embedded Lua

- Style features using embedded Lua. See ./etc/style0.lua  and ./etc/style1.lua
- Default styles are drawn from a global variable named "vfr_feature_style"
- Feature styles may also be programmatically altered using a global function called "vfrFeatureStyle" which takes one argument, a table with fields from the feature (obtain these using ogrinfo or some such tool)
...

##Examples

![Mollweide](https://raw.github.com/runderwood/vfr/master/out/moll.png)
![Robinson](https://raw.github.com/runderwood/vfr/master/out/robinson.png)
![Spherical Mercator](https://raw.github.com/runderwood/vfr/master/out/sphmerc.png)
![Geographic](https://raw.github.com/runderwood/vfr/master/out/vfr_out.png)
![Hydrology](https://raw.github.com/runderwood/vfr/master/out/tx_res.png)

