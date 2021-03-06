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
        label_place = 1,
        label_field = "GNIS_NAME",
        label_fontdesc = "EB Garamond 6",
        label_fill = { r=255, g=0, b=255 }
    }

    return fstyle
end
