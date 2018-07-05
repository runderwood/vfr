function mkgradientfunc(minval, maxval, stcol, encol)
    local gf = function (val)
        p = (val - minval)/(maxval - minval)
        r = stcol.r + p * (encol.r - stcol.r)
        g = stcol.g + p * (encol.g - stcol.g)
        b = stcol.b + p * (encol.b - stcol.b)
        return {r=r, g=g, b=b}
    end
    return gf
end

vfr_style = {
    stroke = {
        r = 127,
        g = 127,
        b = 127
    },
    fill = {
        r = 255,
        g = 255,
        b = 255
    },
    size = 1
}

function vfrFeatureStyle(ftr)
    fstyle = {
        stroke = {
            r = 0,
            g = 0,
            b = 0
        },
        fill = {
            r = 255,
            g = 255,
            b = 255 
        },
        size = 1,
        label_place = 0,
        label_field = "name",
        label_fill = { r=255, g=0, b=0 }
    }

    -- require 'pl.pretty'.dump(ftr)

    if(ftr._vfr_layer == "pubschfrl") then
        fstyle.fill = {r=0, g=255, b=0}
        fstyle.stroke = {r=0, g=255, b=0}
        fstyle.size = 1
        frl = 0.0
        frl = tonumber(ftr["frl1718._8"])
        if(frl == nil) then
            fstyle.fill = {r=160, g=160, b=160}
            fstyle.stroke = {r=160, g=160, b=160}
        else
            local gf = mkgradientfunc(0.0, 1.0, {r=58, g=168, b=21}, {r=173, g=38, b=31})
            fstyle.fill = gf(frl)
            fstyle.stroke = gf(frl)
            fstyle.size = fstyle.size + frl*9
        end
    else
        fstyle.stroke = {r=128, g=128, b=128}
        fstyle.fill = {r=255, g=255, b=255}
        fstyle.label_fill = {r=128, g=128, b=128}
        fstyle.label_place = 1
        fstyle.label_rotate = 45
        fstyle.label_field = "COUNTY_NAM"
        fstyle.label_fontdesc = "Roboto 12"
    end
    
    -- require 'pl.pretty'.dump(fstyle)
    return fstyle
end
