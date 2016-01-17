vfr_style = {
    fgcolor = {
        r = 127,
        g = 127,
        b = 127
    },
    bgcolor = {
        r = 255,
        g = 255,
        b = 255
    },
    size = 1
}

function vfrFeatureStyle(ftr)
    fstyle = {
        fgcolor = {
            r = 64,
            g = 64,
            b = 64
        },
        bgcolor = {
            r = 255,
            g = 255,
            b = 255 
        },
        size = 1
    }

    if(ftr.pop2010 < ftr.pop2000 and ftr.pop2000 < ftr.pop1990 and ftr.pop1990 < ftr.pop1980 and
        ftr.pop1980 < ftr.pop1970 and ftr.pop1970 < ftr.pop1960 and ftr.pop1960 < ftr.pop1950 and
            ftr.pop1950 < ftr.pop1940 and ftr.pop1940 < ftr.pop1930) then
        fstyle.bgcolor = {r = 0, g = 0, b = 0}
        fstyle.fgcolor = {r = 0, g = 0, b = 0}
    end
    return fstyle
end
