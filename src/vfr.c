#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "ogr_api.h"
#include "ogr_srs_api.h"
#include "cpl_error.h"

#include "cairo.h"

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#if defined(__linux__)
#define VFRSYSNAME "Linux"
#else
#define VFRSYSNAME "Unknown"
#endif


typedef struct vfr_style_s {
    uint64_t bgcolor;
    uint64_t fgcolor;
    int size;
} vfr_style_t;

const char *g_progname;

static void usage(void);

static int runrender(int argc, char **argv);
static int runinform(int argc, char **argv);
static int runversion(int argc, char **argv);

static int implrender(const char *datpath, int iw, int ih, 
        const char *outfilenm, vfr_style_t *style, const char *luafilenm);
static int vfr_ds_extent(OGRDataSourceH *ds, OGREnvelope *ext);
static int vfr_draw_point(cairo_t *cr, OGRGeometryH geom, OGREnvelope *ext,
    double pxw, double pxh, vfr_style_t *style);
static int vfr_draw_linestring(cairo_t *cr, OGRGeometryH geom, OGREnvelope *ext, 
    double pxw, double pxh, vfr_style_t *style);
static int vfr_draw_polygon(cairo_t *cr, OGRGeometryH geom, OGREnvelope *ext,
    double pxw, double pxh, vfr_style_t *style);
static int eval_feature_style(OGRFeatureH f, vfr_style_t *style);

int main(int argc, char **argv) {
    g_progname = argv[0];
    if(argc < 2) usage();
    int rv = 0;
    OGRRegisterAll();
    if(!strcmp(argv[1], "render")) {
        rv = runrender(argc, argv);
    } else if(!strcmp(argv[1], "inform")) {
        rv = runinform(argc, argv);
    } else if(!strcmp(argv[1], "version")) {
        rv = runversion(argc, argv);
    } else {
        usage();
    }
    return rv;
}

static void usage(void){
    fprintf(stderr, "%s: the command line vector feature renderer\n", g_progname);
    fprintf(stderr, "\n");
    fprintf(stderr, "usage:\n");
    fprintf(stderr, "  %s render [-out outfile] -ht INT | -wd INT path dsrc\n", g_progname);
    fprintf(stderr, "  %s inform dsrc\n", g_progname);
    fprintf(stderr, "  %s version\n", g_progname);
    fprintf(stderr, "\n");
    exit(1);
}

static int runrender(int argc, char **argv) {
    
    if(argc < 3) {
        usage();
        return(1);
    }

    char *path = NULL;
    char *outfilenm = NULL;
    char *luafilenm = NULL;
    vfr_style_t style = {0xffffff,0x000000,1};
    int iw, ih, i;
    iw = 0;
    ih = 0;
    char *str, *end;
    unsigned long long ullval;
    int ival;
    for(i=2; i<argc; i++) {
        if(!path && argv[i][0] == '-') {
            if(!strcmp(argv[i], "-ht")) {
                if(++i >= argc) {
                    usage();
                    return 1;
                }
                ih = atoi(argv[i]);
            } else if(!strcmp(argv[i], "-wd")) {
                if(++i >= argc) {
                    usage();
                    return 1;
                }
                iw = atoi(argv[i]);
            } else if(!strcmp(argv[i], "-out")) {
                if(++i >= argc) {
                    usage();
                    return 1;
                }
                outfilenm = argv[i];
            } else if(!strcmp(argv[i], "-fg")) {
                ullval = strtoull(argv[++i], &end, 16);
                if(ullval == 0 && end == argv[i]) {
                    usage();
                    return 1;
                } else if(ullval == ULLONG_MAX && errno) {
                    usage();
                    return 1;
                } else if(*end) {
                    usage();
                    return 1;
                } else {
                    style.fgcolor = ullval;
                }
            } else if(!strcmp(argv[i], "-bg")) {
                ullval = strtoull(argv[++i], &end, 16);
                if(ullval == 0 && end == argv[i]) {
                    usage();
                    return 1;
                } else if(ullval == ULLONG_MAX && errno) {
                    usage();
                    return 1;
                } else if(*end) {
                    usage();
                    return 1;
                } else {
                    style.bgcolor = ullval;
                }
            } else if(!strcmp(argv[i], "-sz")) {
                ival = atoi(argv[++i]);
                style.size = ival;
            } else if(!strcmp(argv[i], "-lua")) {
                if(++i >= argc) {
                    usage();
                    return 1;
                }
                luafilenm = argv[i];
            } else {
                usage();
                return 1;
            }
        } else if(!path) {
            path = argv[i];
        } else {
            usage();
            return 1;
        }
    }

    if(iw <= 0 && ih <= 0) {
        usage();
        return 1;
    } 

    int rv;

    if(outfilenm == NULL) {
        rv = implrender(path, iw, ih, "vfr_out.png", &style, luafilenm);
    } else {
        rv = implrender(path, iw, ih, outfilenm, &style, luafilenm);
    }

    return rv;
}

static int runinform(int argc, char **argv) {
    if(argc < 3) {
        usage();
        return 1;
    }
    OGRDataSourceH src;
    OGRSFDriverH drvr;
    src = OGROpen(argv[2], FALSE, &drvr);
    if(src == NULL) {
        fprintf(stderr, "could not open %s: %s\n", argv[2], CPLGetLastErrorMsg());
        return 1;
    }
    const char *srcnm = OGR_DS_GetName(src);
    int srclcount = OGR_DS_GetLayerCount(src);
    printf("%s: %d layer(s)\n", srcnm, srclcount);
    int i = 0;
    int lfcount;
    const char *lgeomtype = "UKNOWN";
    OGRLayerH layer;
    OGRFeatureH ftr;
    char *srswkt;
    OGRErr err;
    OGREnvelope ext;
    for(i =0; i<srclcount; i++) {
        layer = OGR_DS_GetLayer(src, i);
        lfcount = OGR_L_GetFeatureCount(layer, TRUE);
        OGR_L_ResetReading(layer);
        if((ftr = OGR_L_GetNextFeature(layer)) != NULL) {
            lgeomtype = OGR_G_GetGeometryName(OGR_F_GetGeometryRef(ftr));
        }
        printf("\tlayer %d: %s [%s] - %d feature(s)\n", i, OGR_L_GetName(layer), 
            lgeomtype, lfcount);
        OGR_L_GetExtent(layer, &ext, 0);
        printf("\textent: %0.2f, %0.2f, %0.2f, %0.2f\n", 
            ext.MinX, ext.MinY, ext.MaxX, ext.MaxY);
        OSRExportToWkt(OGR_L_GetSpatialRef(layer), &srswkt);
        printf("\t%s\n", srswkt);
    }
    OGR_DS_Destroy(src);
    return 0;
}

static int runversion(int argc, char **argv) {
    printf("VFR Vector Feature Renderer version %s for %s\n",
      "0.0.0", VFRSYSNAME);
    printf("Copyright (C) 2013 BdD Labs\n");
    return 0;
}

static int implrender(const char *datpath, int iw, int ih, 
        const char *outfilenm, vfr_style_t *style, const char *luafilenm) {
    
    // open shapefile
    OGRDataSourceH src;
    OGRSFDriverH drvr;
    src = OGROpen(datpath, FALSE, &drvr);
    if(src == NULL) {
        fprintf(stderr, "could not open %s: %s\n", datpath, CPLGetLastErrorMsg());
        return 1;
    }

    // init lua, load luafile if available
    lua_State *L;
    if(luafilenm != NULL) {
        fprintf(stderr, "opening lua file: %s\n", luafilenm);
        L = lua_open();
        luaL_openlibs(L);
        if(luaL_loadfile(L, luafilenm) || lua_pcall(L, 0, 0, 0)) {
            fprintf(stderr, "could not load luafile %s: %s\n", luafilenm, lua_tostring(L, -1));
            lua_close(L);
            return 1;
        }

        // load default style (if available)
        // this should call a fxn, something like synch_style_table(*style, lua_state)
        lua_getglobal(L, "vfr_style");
        if(!lua_istable(L, -1)) {
            fprintf(stderr, "vfr_style is not a valid lua table");
        } else {
            lua_pushstring(L, "fgcolor");
            lua_gettable(L, -2);
            if(lua_istable(L, -1)) {
                lua_pushstring(L, "r");
                lua_gettable(L, -2);
                if(lua_isnumber(L, -1)) {
                    // should check for valid color here. maybe later. meantime, expect weirdness for n > 255
                    style->fgcolor = ((int)lua_tonumber(L, -1) << 16) & 0xff0000;
                    fprintf(stderr, "set fgcolor (r) to %x (%d)\n", style->fgcolor, (int)lua_tonumber(L, -1));
                }
                lua_pop(L, 1);
                lua_pushstring(L, "g");
                lua_gettable(L, -2);
                if(lua_isnumber(L, -1)) {
                    // should check for valid color here. maybe later. meantime, expect weirdness for n > 255
                    style->fgcolor |= ((int)lua_tonumber(L, -1) << 8) & 0x00ff00;
                    fprintf(stderr, "set fgcolor (g) to %x (%d)\n", style->fgcolor, (int)lua_tonumber(L, -1));
                }
                lua_pop(L, 1);
                lua_pushstring(L, "b");
                lua_gettable(L, -2);
                if(lua_isnumber(L, -1)) {
                    // should check for valid color here. maybe later. meantime, expect weirdness for n > 255
                    style->fgcolor |= ((int)lua_tonumber(L, -1)) & 0x0000ff;
                    fprintf(stderr, "set fgcolor (b) to %x (%d)\n", style->fgcolor, (int)lua_tonumber(L, -1));
                }
                lua_pop(L, 2);
            }
            lua_pushstring(L, "bgcolor");
            lua_gettable(L, -2);
            if(lua_istable(L, -1)) {
                lua_pushstring(L, "r");
                lua_gettable(L, -2);
                if(lua_isnumber(L, -1)) {
                    // should check for valid color here. maybe later. meantime, expect weirdness for n > 255
                    style->bgcolor = ((int)lua_tonumber(L, -1) << 16) & 0xff0000;
                    fprintf(stderr, "set bgcolor (r) to %x (%d)\n", style->bgcolor, (int)lua_tonumber(L, -1));
                }
                lua_pop(L, 1);
                lua_pushstring(L, "g");
                lua_gettable(L, -2);
                if(lua_isnumber(L, -1)) {
                    // should check for valid color here. maybe later. meantime, expect weirdness for n > 255
                    style->bgcolor |= ((int)lua_tonumber(L, -1) << 8) & 0x00ff00;
                    fprintf(stderr, "set bgcolor (g) to %x (%d)\n", style->bgcolor, (int)lua_tonumber(L, -1));
                }
                lua_pop(L, 1);
                lua_pushstring(L, "b");
                lua_gettable(L, -2);
                if(lua_isnumber(L, -1)) {
                    // should check for valid color here. maybe later. meantime, expect weirdness for n > 255
                    style->bgcolor |= ((int)lua_tonumber(L, -1)) & 0x0000ff;
                    fprintf(stderr, "set bgcolor (b) to %x (%d)\n", style->bgcolor, (int)lua_tonumber(L, -1));
                }
                lua_pop(L, 2);
            }
        }
    }

    // get max extent for all layers
    int i, layercount, lfcount, gcount, g;
    long j;
    OGREnvelope ext;
    vfr_ds_extent(src, &ext);

    // get pixel-to-map unit ratio
    double pxw, pxh;
    if(iw == 0 && ih == 0) {
        usage();
        return 1;
    } else if(iw == 0) {
        iw = ih * ((ext.MaxX - ext.MinX)/(ext.MaxY - ext.MinY));
    } else if(ih == 0) {
        ih = iw * ((ext.MaxY - ext.MinY)/(ext.MaxX - ext.MinX));
    }
    pxw = (ext.MaxX - ext.MinX)/iw;
    pxh = (ext.MaxY - ext.MinY)/ih;

    // draw
    cairo_surface_t *surface;
    cairo_t *cr;
    surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, iw, ih);
    cr = cairo_create(surface);
    cairo_rectangle(cr, 0, 0, iw, ih);
    cairo_set_source_rgb(cr, 255, 255, 255);
    cairo_fill(cr);
    OGRGeometryH geom, geom2;
    OGRFeatureH ftr;
    OGRLayerH layer;
    layercount = OGR_DS_GetLayerCount(src);
    double x, y, z, pxx, pxy;

    for(i=0; i<layercount; i++) {
        layer = OGR_DS_GetLayer(src, i);
        lfcount = OGR_L_GetFeatureCount(layer, 0);
        for(j=0; j<lfcount; j++) {
            ftr = OGR_L_GetFeature(layer, j);
            geom = OGR_F_GetGeometryRef(ftr);
            if(geom == NULL) {
                fprintf(stderr, "skipping empty polygon w/ fid = %ld\n", 
                    OGR_F_GetFID(ftr));
                continue;
            }
            if(luafilenm != NULL) {
                eval_feature_style(ftr, style);
            }
            switch(OGR_G_GetGeometryType(geom)) {
                case wkbPoint:
                    vfr_draw_point(cr, geom, &ext, pxw, pxh, style);
                    break;
                case wkbPolygon:
                    vfr_draw_polygon(cr, geom, &ext, pxw, pxh, style);
                    break;
                case wkbLineString:
                    vfr_draw_linestring(cr, geom, &ext, pxw, pxh, style);
                    break;
                case wkbLinearRing:
                    printf("ring!\n");
                    break;
                case wkbMultiPolygon:
                    gcount = OGR_G_GetGeometryCount(geom);
                    for(g=0; g < gcount; g++) {
                        geom2 = OGR_G_GetGeometryRef(geom, g);
                        vfr_draw_polygon(cr, geom2, &ext, pxw, pxh, style);
                    }
                    break;
                case wkbMultiLineString:
                    gcount = OGR_G_GetGeometryCount(geom);
                    for(g=0; g < gcount; g++) {
                        geom2 = OGR_G_GetGeometryRef(geom, g);
                        vfr_draw_linestring(cr, geom2, &ext, pxw, pxh, style);
                    }
                    break;
                default:
                    printf("unknown!\n");
                    break;
            }
            OGR_F_Destroy(ftr);
        }
    }
    cairo_surface_write_to_png(surface, outfilenm);
    if(luafilenm != NULL) {
        lua_close(L);
    }
    return 0;
}

static int eval_feature_style(OGRFeatureH ftr, vfr_style_t *style) {
    return 0;
}

static int vfr_ds_extent(OGRDataSourceH *ds, OGREnvelope *ext) {
    int i;
    int layercount = OGR_DS_GetLayerCount(ds);
    OGRLayerH layer;
    OGREnvelope lext = {};
    for(i=0; i<layercount; i++) {
        layer = OGR_DS_GetLayer(ds, i);
        OGR_L_GetExtent(layer, &lext, 0);
        if(i == 0 || lext.MinX < ext->MinX) {
            ext->MinX = lext.MinX;
        }
        if(i == 0 || lext.MinY < ext->MinY) {
            ext->MinY = lext.MinY;
        }
        if(i == 0 || lext.MaxX > ext->MaxX) {
            ext->MaxX = lext.MaxX;
        }
        if(i == 0 || lext.MaxY > ext->MaxY) {
            ext->MaxY = lext.MaxY;
        }
    }
    return 0;
}

static int vfr_draw_point(cairo_t *cr, OGRGeometryH geom, OGREnvelope *ext,
        double pxw, double pxh, vfr_style_t *style) {
    double x, y, z;
    OGR_G_GetPoint(geom, 0, &x, &y, &z);
    double pxx = (x - ext->MinX)/pxw;
    double pxy = (ext->MaxY - y)/pxh;
    cairo_arc(cr, pxx, pxy, 4, 0, 2*M_PI);
    cairo_set_source_rgb(cr, 255, 0, 0);
    cairo_fill_preserve(cr);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_line_width(cr, 2);
    cairo_stroke(cr);
    return 0;
}

static int vfr_draw_linestring(cairo_t *cr, OGRGeometryH geom, OGREnvelope *ext, 
        double pxw, double pxh, vfr_style_t *style) {
    int pcount = OGR_G_GetPointCount(geom);
    if(!pcount) return 0;
    double x, y, z, pxx, pxy;
    int i;
    cairo_set_line_width(cr, style->size);
    cairo_set_source_rgb(cr, (style->fgcolor & 0xff0000)/0xff0000, 
        (style->fgcolor & 0x00ff00)/0x00ff00, (style->fgcolor & 0x0000ff)/0x0000ff);
    for(i = 0; i < pcount; i++) {
        OGR_G_GetPoint(geom, i, &x, &y, &z);
        pxx = (x - ext->MinX)/pxw;
        pxy = (ext->MaxY - y)/pxh;
        if(!i) {
            cairo_move_to(cr, pxx, pxy);
            continue;
        }
        cairo_line_to(cr, pxx, pxy);
    }
    cairo_stroke(cr);
    return 0;
}

static int vfr_draw_polygon(cairo_t *cr, OGRGeometryH geom, OGREnvelope *ext, 
        double pxw, double pxh, vfr_style_t *style) {
    OGRGeometryH geom2 = OGR_G_GetGeometryRef(geom, 0);
    int p, ptcount = OGR_G_GetPointCount(geom2);
    double x, y, z, pxx, pxy;
    for(p=0; p<ptcount; p++) {
        OGR_G_GetPoint(geom2, p, &x, &y, &z);
        pxx = (x - ext->MinX)/pxw;
        pxy = (ext->MaxY - y)/pxh;
        if(!p) {
            cairo_move_to(cr, pxx, pxy);
        } else {
            cairo_line_to(cr, pxx, pxy);
        }
    }
    OGR_G_GetPoint(geom2, 0, &x, &y, &z);
    pxx = (x - ext->MinX)/pxw;
    pxy = (ext->MaxY - y)/pxh;
    cairo_line_to(cr, pxx, pxy);
    cairo_set_source_rgb(cr, ((style->bgcolor & 0xff0000) >> 16)/256.0, 
        ((style->bgcolor & 0x00ff00) >> 8)/256.0, 
        (style->bgcolor & 0x0000ff)/256.0);
    cairo_fill_preserve(cr);
    cairo_set_source_rgb(cr, ((style->fgcolor & 0xff0000) >> 16)/256.0, 
        ((style->fgcolor & 0x00ff00) >> 8)/256.0, 
        (style->fgcolor & 0x0000ff)/256.0);
    cairo_set_line_width(cr, style->size);
    cairo_stroke(cr);
    return 0;
}
