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

county_list = {Briscoe=true,Brooks=true,Castro=true,Coleman=true,
    Collingsworth=true,Cottle=true,Dimmit=true,Edwards=true,Floyd=true,
    Hall=true,Hardeman=true,Haskell=true,Knox=true,Lamb=true,Motley=true,
    Refugio=true,Terry=true}
county_list["Red River"] = true

function vfrFeatureStyle(ftr)
    fstyle = {
        stroke = {
            r = 64,
            g = 64,
            b = 64
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

    if(county_list[ftr.name]) then
        fstyle.fill = {r = 127, g = 127, b = 127}
        fstyle.stroke = {r = 0, g = 0, b = 0}
        if(ftr.pop2010 < ftr.pop2000 and ftr.pop2000 < ftr.pop1990 and ftr.pop1990 < ftr.pop1980 and
            ftr.pop1980 < ftr.pop1970 and ftr.pop1970 < ftr.pop1960 and ftr.pop1960 < ftr.pop1950 and
                ftr.pop1950 < ftr.pop1940 and ftr.pop1940 < ftr.pop1930) then
            fstyle.fill = {r = 0, g = 0, b = 0}
            fstyle.stroke = {r = 0, g = 0, b = 0}
            fstyle.label_place = 1
        end
    end
    return fstyle
end
