#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "ogr_api.h"
#include "ogr_srs_api.h"
#include "cpl_error.h"

#include "cairo.h"

#include <pango/pango.h>
#include <pango/pangocairo.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#if defined(__linux__)
#define VFRSYSNAME "Linux"
#else
#define VFRSYSNAME "Unknown"
#endif

#define VFRLABEL_CENTER 0x00000001 // center (varies)
#define VFRLABEL_OFFSET 0x00000002 // offset plcmt (line)
#define VFRLABEL_TANGNT 0x00000004 // tangential plcmt (line)
#define VFRLABEL_HORIZN 0x00000008 // horizontal plcmt (line)
#define VFRLABEL_CURVED 0x00000010 // curved plcmt (line)
#define VFRLABEL_PERPDI 0x00000020 // perpendicular plcmt (line)
#define VFRLABEL_PREFNW 0x00000040 // prefer northwest plcmt (point)
#define VFRLABEL_PREFNE 0x00000080 // " northeast
#define VFRLABEL_PREFSE 0x00000100 // " southeast
#define VFRLABEL_PREFSW 0x00000200 // " southwest
#define VFRLABEL_SPREAD 0x00000400 // spread to fill (poly)
#define VFRLABEL_UPCASE 0x00000800 // text transform to uppercase
#define VFRLABEL_OVRLAP 0x00001000 // overlap (for poly. bounds behavior, etc.)
#define VFRLABEL_NOCROS 0x00002000 // no cross (for shorelines, etc.)
#define VFRLABEL_FLDFIR 0x00004000 // use field first, if exists. text val sec.
#define VFRLABEL_WRPNON 0x00008000 // nowrap
#define VFRLABEL_WRPWRD 0x00010000 // wrap at word
#define VFRLABEL_WRPCHR 0x00020000 // wrap at character
#define VFRLABEL_WRPWCH 0x00040000 // wrap at word, then char (tolerance?)

#define VFRDEFAULT_FONTDESC "Courier New 12"

typedef enum {VFRPLACE_NONE, VFRPLACE_AUTO, VFRPLACE_CENTER, VFRPLAC_POINT,
    VFRPLACE_LINE} vfr_label_place_t;

// TODO: style dashes, hashes (patterns)
typedef struct vfr_style_s {
    uint64_t fill;
    uint64_t stroke;
    int size;
    vfr_label_place_t label_place;
    char *label_field;
    uint64_t label_fill;
    char *label_text;
    char *label_fontdesc;
    uint32_t label_flags;
    double label_xoff;
    double label_yoff;
    double label_halo_radius;
    uint64_t label_halo_fill;
    double label_rotate;
    double label_width; // width for wrapping (in ems? or points?)
} vfr_style_t;

typedef struct vfr_list_s {
    void *dat;
    struct vfr_list_s *next;
    struct vfr_list_s *prev;
} vfr_list_t;

const char *g_progname;

static void usage(void);

static int runrender(int argc, char **argv);
static int runinform(int argc, char **argv);
static int runversion(int argc, char **argv);

static int implrender(const char *datpath, int iw, int ih, 
        const char *outfilenm, vfr_style_t *style, const char *luafilenm);
static int vfr_ds_extent(OGRDataSourceH *ds, OGREnvelope *ext);
static int vfr_draw_geom(cairo_t *cr, OGRFeatureH ftr, OGRGeometryH geom, OGREnvelope *ext,
        double pxw, double pxh, vfr_style_t *style);
static int vfr_draw_label(cairo_t *cr, OGRFeatureH ftr, OGRGeometryH geom, OGREnvelope *ext,
        double pxw, double pxh, vfr_style_t *style);
static int vfr_draw_point(cairo_t *cr, OGRGeometryH geom, OGREnvelope *ext,
        double pxw, double pxh, vfr_style_t *style);
static int vfr_draw_linestring(cairo_t *cr, OGRGeometryH geom, OGREnvelope *ext, 
        double pxw, double pxh, vfr_style_t *style);
static int vfr_draw_polygon(cairo_t *cr, OGRGeometryH geom, OGREnvelope *ext,
        double pxw, double pxh, vfr_style_t *style);
static int eval_feature_style(lua_State *L, OGRFeatureH f, vfr_style_t *style);
static int synch_style_table(lua_State *L, vfr_style_t *style);

static double vfr_color_compextr(uint64_t color, char c);
// static vfr_list_t* vfr_list_new(void *dat);

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
    fprintf(stderr, "  %s render [-out outfile] -ht INT | -wd INT [-lua luafile] datasrc\n", g_progname);
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
    vfr_style_t style = {
        0xffffff,0x000000,1, //stroke, fill, size
        VFRPLACE_NONE,NULL,0xffffff,NULL,NULL // label placement, field, color, text
    };
    int iw, ih, i;
    iw = 0;
    ih = 0;
    char *end;
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
                    style.stroke = ullval;
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
                    style.fill = ullval;
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
        synch_style_table(L, style);
    }

    // get max extent for all layers
    int i, layercount, lfcount;
    long j;
    OGREnvelope ext;
    vfr_ds_extent(src, &ext);
    fprintf(stderr, "got extents: max = (%f, %f), min = (%f, %f)\n", ext.MaxX, ext.MaxY, ext.MinX, ext.MinY);

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
    OGRGeometryH geom;
    OGRFeatureH ftr;
    OGRLayerH layer;
    layercount = OGR_DS_GetLayerCount(src);

    fprintf(stderr, "loading layers (%d)\n", layercount);
    for(i=0; i<layercount; i++) {
        layer = OGR_DS_GetLayer(src, i);
        lfcount = OGR_L_GetFeatureCount(layer, 0);
        fprintf(stderr, "- %d feature(s)\n", lfcount);
        for(j=0; j<lfcount; j++) {
            ftr = OGR_L_GetFeature(layer, j);
            geom = OGR_F_GetGeometryRef(ftr);
            if(geom == NULL) {
                fprintf(stderr, "skipping null geometry w/ fid = %ld\n", 
                    OGR_F_GetFID(ftr));
                continue;
            }
            if(luafilenm != NULL) {
                eval_feature_style(L, ftr, style);
            }
            vfr_draw_geom(cr, ftr, geom, &ext, pxw, pxh, style);
            vfr_draw_label(cr, ftr, geom, &ext, pxw, pxh, style);
            OGR_F_Destroy(ftr);
        }
    }
    fprintf(stderr, "writing to %s\n", outfilenm);
    cairo_surface_write_to_png(surface, outfilenm);
    if(luafilenm != NULL) {
        lua_close(L);
    }
    return 0;
}

static int eval_feature_style(lua_State *L, OGRFeatureH ftr, vfr_style_t *style) {
    lua_getglobal(L, "vfrFeatureStyle");
    if(!lua_isfunction(L, -1)) {
        fprintf(stderr, "vfrFeatureStyle is not a lua function");
        lua_pop(L, 1);
        return 1;
    }
    lua_newtable(L);
    int fldcount = OGR_F_GetFieldCount(ftr);
    int i;
    OGRFieldDefnH fdef;
    for(i=0; i<fldcount; i++) {
        fdef = OGR_F_GetFieldDefnRef(ftr, i);
        lua_pushstring(L, OGR_Fld_GetNameRef(fdef));
        switch(OGR_Fld_GetType(fdef)) {
            case OFTString:
            case OFTDate:
            case OFTTime:
                lua_pushstring(L, OGR_F_GetFieldAsString(ftr, i));
                break;
            case OFTInteger:
            case OFTReal:
                lua_pushnumber(L, OGR_F_GetFieldAsDouble(ftr, i));
                break;
            default:
                fprintf(stderr, "skipping unimplemented field type (pushing nil) for field named '%s'\n",
                    OGR_Fld_GetNameRef(fdef));
                lua_pushnil(L);
                break;
        }
        lua_settable(L, -3);
    }
    if(lua_pcall(L, 1, 1, 0) != 0) {
        fprintf(stderr, "error calling vfrFeatureStyle: %s\n", lua_tostring(L, -1));
    }
    if(!lua_istable(L, -1)) {
        fprintf(stderr, "vfrFeatureStyle did not return a table\n");
        return 1;
    }
    synch_style_table(L, style);
    return 0;
}

// assumes a valid style table is pushed on the lua stack
static int synch_style_table(lua_State *L, vfr_style_t *style) {

    int objlen;

    if(!lua_istable(L, -1)) {
        fprintf(stderr, "style is not a valid lua table");
    } else {
        lua_pushstring(L, "stroke");
        lua_gettable(L, -2);
        if(lua_istable(L, -1)) {
            lua_pushstring(L, "r");
            lua_gettable(L, -2);
            if(lua_isnumber(L, -1)) {
                // should check for valid color here. maybe later. meantime, expect weirdness for n > 255
                style->stroke = ((int)lua_tonumber(L, -1) << 16) & 0xff0000;
            }
            lua_pop(L, 1);
            lua_pushstring(L, "g");
            lua_gettable(L, -2);
            if(lua_isnumber(L, -1)) {
                // should check for valid color here. maybe later. meantime, expect weirdness for n > 255
                style->stroke |= ((int)lua_tonumber(L, -1) << 8) & 0x00ff00;
            }
            lua_pop(L, 1);
            lua_pushstring(L, "b");
            lua_gettable(L, -2);
            if(lua_isnumber(L, -1)) {
                // should check for valid color here. maybe later. meantime, expect weirdness for n > 255
                style->stroke |= ((int)lua_tonumber(L, -1)) & 0x0000ff;
            }
            lua_pop(L, 2);
        }
        lua_pushstring(L, "fill");
        lua_gettable(L, -2);
        if(lua_istable(L, -1)) {
            lua_pushstring(L, "r");
            lua_gettable(L, -2);
            if(lua_isnumber(L, -1)) {
                // should check for valid color here. maybe later. meantime, expect weirdness for n > 255
                style->fill = ((int)lua_tonumber(L, -1) << 16) & 0xff0000;
            }
            lua_pop(L, 1);
            lua_pushstring(L, "g");
            lua_gettable(L, -2);
            if(lua_isnumber(L, -1)) {
                // should check for valid color here. maybe later. meantime, expect weirdness for n > 255
                style->fill |= ((int)lua_tonumber(L, -1) << 8) & 0x00ff00;
            }
            lua_pop(L, 1);
            lua_pushstring(L, "b");
            lua_gettable(L, -2);
            if(lua_isnumber(L, -1)) {
                // should check for valid color here. maybe later. meantime, expect weirdness for n > 255
                style->fill |= ((int)lua_tonumber(L, -1)) & 0x0000ff;
            }
            lua_pop(L, 2);
        }
        lua_pushstring(L, "size");
        lua_gettable(L, -2);
        if(lua_isnumber(L, -1)) {
            style->size = (int)lua_tonumber(L, -1);
        }
        lua_pop(L, 1);
        lua_pushstring(L, "label_place");
        lua_gettable(L, -2);
        if(lua_isnumber(L, -1)) {
            style->label_place = (int)lua_tonumber(L, -1);
        }
        lua_pop(L, 1);
        lua_pushstring(L, "label_fontdesc");
        lua_gettable(L, -2);
        if(lua_isstring(L, -1)) {
            objlen = lua_objlen(L, -1);
            if(style->label_fontdesc != NULL) {
                free(style->label_fontdesc);
            }
            style->label_fontdesc = malloc(objlen+1);
            if(style->label_fontdesc == NULL) {
                fprintf(stderr, "out of memory\n");
                exit(1);
            }
            memcpy(style->label_fontdesc, lua_tostring(L, -1), objlen);
            style->label_fontdesc[objlen] = '\0';
        }
        lua_pop(L, 1);
        lua_pushstring(L, "label_field");
        lua_gettable(L, -2);
        if(lua_isstring(L, -1)) {
            // we need to copy this string, not juse use the pointer...
            objlen = lua_objlen(L, -1);
            // free old string
            if(style->label_field != NULL) {
                free(style->label_field);
            }
            style->label_field = malloc(objlen+1);
            if(style->label_field == NULL) {
                fprintf(stderr, "out of memory\n");
                exit(1);
            }
            memcpy(style->label_field, lua_tostring(L, -1), objlen);
            style->label_field[objlen] = '\0';
        }
        lua_pop(L, 1);
        lua_pushstring(L, "label_text");
        lua_gettable(L, -2);
        if(lua_isstring(L, -1)) {
            // we need to copy this string, not juse use the pointer...
            objlen = lua_objlen(L, -1);
            // free old string
            if(style->label_text != NULL) {
                free(style->label_text);
            }
            style->label_text = malloc(objlen+1);
            if(style->label_text == NULL) {
                fprintf(stderr, "out of memory\n");
                exit(1);
            }
            memcpy(style->label_text, lua_tostring(L, -1), objlen);
            style->label_text[objlen] = '\0';
        }
        lua_pop(L, 1);
        lua_pushstring(L, "label_fill");
        lua_gettable(L, -2);
        if(lua_istable(L, -1)) {
            lua_pushstring(L, "r");
            lua_gettable(L, -2);
            if(lua_isnumber(L, -1)) {
                // should check for valid color here. maybe later. meantime, expect weirdness for n > 255
                style->label_fill = ((int)lua_tonumber(L, -1) << 16) & 0xff0000;
            }
            lua_pop(L, 1);
            lua_pushstring(L, "g");
            lua_gettable(L, -2);
            if(lua_isnumber(L, -1)) {
                // should check for valid color here. maybe later. meantime, expect weirdness for n > 255
                style->label_fill |= ((int)lua_tonumber(L, -1) << 8) & 0x00ff00;
            }
            lua_pop(L, 1);
            lua_pushstring(L, "b");
            lua_gettable(L, -2);
            if(lua_isnumber(L, -1)) {
                // should check for valid color here. maybe later. meantime, expect weirdness for n > 255
                style->label_fill |= ((int)lua_tonumber(L, -1)) & 0x0000ff;
            }
            lua_pop(L, 2);
        }
    }
    return 0;
}

static int vfr_ds_extent(OGRDataSourceH *ds, OGREnvelope *ext) {
    int i;
    int layercount = OGR_DS_GetLayerCount(ds);
    OGRLayerH layer;
    OGREnvelope lext = {};
    for(i=0; i<layercount; i++) {
        layer = OGR_DS_GetLayer(ds, i);
        OGR_L_GetExtent(layer, &lext, 1); // arg 3 is bForce (exp. but nec.)
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

static int vfr_draw_geom(cairo_t *cr, OGRFeatureH ftr, OGRGeometryH geom, OGREnvelope *ext,
        double pxw, double pxh, vfr_style_t *style) {

    OGRGeometryH geom2;

    int g, gcount;
    
    switch(OGR_G_GetGeometryType(geom)) {
        case wkbPoint:
            fprintf(stderr, "rendering point\n");
            vfr_draw_point(cr, geom, ext, pxw, pxh, style);
            break;
        case wkbPolygon:
            fprintf(stderr, "rendering polygon\n");
            vfr_draw_polygon(cr, geom, ext, pxw, pxh, style);
            break;
        case wkbLineString:
            fprintf(stderr, "rendering linestring\n");
            vfr_draw_linestring(cr, geom, ext, pxw, pxh, style);
            break;
        case wkbLinearRing:
            printf("ring!\n");
            break;
        case wkbMultiPolygon:
            fprintf(stderr, "rendering multipolygon\n");
            gcount = OGR_G_GetGeometryCount(geom);
            for(g=0; g < gcount; g++) {
                geom2 = OGR_G_GetGeometryRef(geom, g);
                vfr_draw_polygon(cr, geom2, ext, pxw, pxh, style);
            }
            break;
        case wkbMultiLineString:
            gcount = OGR_G_GetGeometryCount(geom);
            for(g=0; g < gcount; g++) {
                geom2 = OGR_G_GetGeometryRef(geom, g);
                vfr_draw_linestring(cr, geom2, ext, pxw, pxh, style);
            }
            break;
        case wkbGeometryCollection:
            fprintf(stderr, "geomcollection\n");
            gcount = OGR_G_GetGeometryCount(geom);
            for(g=0; g < gcount; g++) {
                geom2 = OGR_G_GetGeometryRef(geom, g);
                vfr_draw_geom(cr, ftr, geom2, ext, pxw, pxh, style);
            }
            break;
        default:
            printf("unknown!\n");
            break;
    }

    return 0;
}

static int vfr_draw_point(cairo_t *cr, OGRGeometryH geom, OGREnvelope *ext,
        double pxw, double pxh, vfr_style_t *style) {
    double x, y, z;
    OGR_G_GetPoint(geom, 0, &x, &y, &z);
    double pxx = (x - ext->MinX)/pxw;
    double pxy = (ext->MaxY - y)/pxh;
    cairo_arc(cr, pxx, pxy, style->size, 0, 2*M_PI);
    cairo_set_source_rgb(cr, 
            vfr_color_compextr(style->fill, 'r'), 
            vfr_color_compextr(style->fill, 'g'), 
            vfr_color_compextr(style->fill, 'b')); 
    cairo_fill_preserve(cr);
    cairo_set_source_rgb(cr, 
            vfr_color_compextr(style->stroke, 'r'),
            vfr_color_compextr(style->stroke, 'g'),
            vfr_color_compextr(style->stroke, 'b'));
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
    cairo_set_source_rgb(cr,
            vfr_color_compextr(style->stroke, 'r'),
            vfr_color_compextr(style->stroke, 'g'),
            vfr_color_compextr(style->stroke, 'b'));
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
    cairo_set_source_rgb(cr, 
            vfr_color_compextr(style->fill, 'r'), 
            vfr_color_compextr(style->fill, 'g'), 
            vfr_color_compextr(style->fill, 'b')); 
    cairo_fill_preserve(cr);
    cairo_set_source_rgb(cr, 
            vfr_color_compextr(style->stroke, 'r'), 
            vfr_color_compextr(style->stroke, 'g'), 
            vfr_color_compextr(style->stroke, 'b')); 
    cairo_set_line_width(cr, style->size);
    cairo_stroke(cr);
    return 0;
}


static int vfr_draw_label(cairo_t *cr, OGRFeatureH ftr, OGRGeometryH geom,
        OGREnvelope *ext, double pxw, double pxh, vfr_style_t *style) {

    fprintf(stderr, "label feature #%ld\n", OGR_F_GetFID(ftr));

    PangoFontDescription *fdesc;
    PangoRectangle ink_rect, logic_rect;
    PangoLayout *plyo;
    int fieldidx;
    OGRGeometryH centroid;
    double x, y, z, pxx, pxy;

    cairo_set_source_rgb(cr,
            vfr_color_compextr(style->label_fill, 'r'),
            vfr_color_compextr(style->label_fill, 'g'),
            vfr_color_compextr(style->label_fill, 'b'));
    fprintf(stderr, "moving to %f,%f\n", (ext->MaxX-ext->MinX)/pxw/2.0, (ext->MaxY-ext->MinY)/pxh/2.0);

    // layout
    plyo = pango_cairo_create_layout(cr);
    if(style->label_text != NULL) {
        pango_layout_set_text(plyo, style->label_text, -1);
    } else if(style->label_field) {
        fieldidx = OGR_F_GetFieldIndex(ftr, style->label_field);
        if(fieldidx < 0) {
            fprintf(stderr, "invalid label field name '%s'\n", style->label_field);
            return -1;
        } else {
            pango_layout_set_text(plyo, OGR_F_GetFieldAsString(ftr, fieldidx), -1);
        }
    }
    if(style->label_fontdesc == NULL) {
        fdesc = pango_font_description_from_string(VFRDEFAULT_FONTDESC);
    } else {
        fdesc = pango_font_description_from_string(style->label_fontdesc);
    }
    pango_layout_set_font_description(plyo, fdesc);
    fprintf(stderr, "font desc set to '%s'\n", pango_font_description_to_string(pango_layout_get_font_description(plyo)));
    fprintf(stderr, "label text set to '%s'\n", pango_layout_get_text(plyo));

    pango_layout_get_pixel_extents(plyo, &ink_rect, &logic_rect);

    // position
    switch(OGR_G_GetGeometryType(geom)) {
        case wkbPoint:
            OGR_G_GetPoint(geom, 0, &x, &y, &z);
            pxx = (x - ext->MinX)/pxw;
            pxx += style->size/2.0;
            pxy = (ext->MaxY - y)/pxh;
            cairo_move_to(cr, pxx, pxy);
            break;
        default:
            centroid = OGR_G_CreateGeometry(wkbPoint);
            if(OGR_G_Centroid(geom, centroid) == OGRERR_FAILURE) {
                fprintf(stderr, "could not get centroid\n");
            }
            OGR_G_GetPoint(centroid, 0, &x, &y, &z);
            pxx = (x - ext->MinX)/pxw;
            pxx -= logic_rect.width/2.0;
            pxy = (ext->MaxY - y)/pxh;
            pxy -= logic_rect.height/2.0;
            cairo_move_to(cr, pxx, pxy);
            break;
    }
    fprintf(stderr, "labelling at %f,%f\n", pxx, pxy);

   
    pango_font_description_free(fdesc);
    
    pango_cairo_show_layout(cr, plyo);
    
    g_object_unref(plyo);
    
    return 0;
}

static double vfr_color_compextr(uint64_t color, char c) {

    double comp = -1.0;
    
    switch(c) {
        case 'r':
            comp = ((color & 0xff0000) >> 16)/255.0;
            break;
        case 'g':
            comp = ((color & 0x00ff00) >> 8)/255.0;
            break;
        case 'b':
            comp = (color & 0x0000ff)/255.0;
            break;
        default:
            break;
    }

    return comp;
}

/*static vfr_list_t* vfr_list_new(void *dat) {
    vfr_list_t *ptr = malloc(sizeof(vfr_list_t));
    if(ptr == NULL) fprintf(stderr, "out of memory\n");
    ptr->dat = dat;
    ptr->next = NULL;
    ptr->prev = NULL;
    return ptr;
}*/
