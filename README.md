##Usage
    vfr: the command line vector feature renderer

    usage:
      ./vfr render [-out outfile] -ht INT | -wd INT path [-fg 0x000000] [-bg 0x000000] [-lua luafile] <source>
      ./vfr inform <source>
      ./vfr version

##Embedded Lua

- Style features using embedded Lua. See ./etc/style0.lua 
...

##Examples

![Mollweide](https://raw.github.com/runderwood/vfr/master/out/moll.png)
![Robinson](https://raw.github.com/runderwood/vfr/master/out/robinson.png)
![Spherical Mercator](https://raw.github.com/runderwood/vfr/master/out/sphmerc.png)
![Geographic](https://raw.github.com/runderwood/vfr/master/out/vfr_out.png)
![Hydrology](https://raw.github.com/runderwood/vfr/master/out/tx_res.png)

