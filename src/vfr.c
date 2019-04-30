#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include "ogr_api.h"
#include "ogr_srs_api.h"
#include "cpl_error.h"

#include "cairo.h"
#include "cairo-svg.h"

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

typedef enum {VFRPLACE_NONE, VFRPLACE_AUTO, VFRPLACE_CENTER, VFRPLACE_POINT,
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

/*typedef struct vfr_list_s {
    void *dat;
    struct vfr_list_s *next;
    struct vfr_list_s *prev;
} vfr_list_t;*/

typedef double param_t;

typedef struct {
    cairo_path_t *path;
    param_t *params;
    double length;
} paramd_path_t;

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


static double euclid_dist(cairo_path_data_t *pt1, cairo_path_data_t *pt2);
static param_t* parametrize_path(cairo_path_t *path, double *plen);
static cairo_path_t* get_linear_label_path(cairo_t *cr, paramd_path_t *parpath,
        double txtwidth, double placeat);
static void transform_label_points(cairo_path_t *lyopath, paramd_path_t *paramd_lblpath);
static void transform_label_point(paramd_path_t *paramd_lblpath, double *xptr, double *yptr);

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
        rv = implrender(path, iw, ih, "vfr_out.svg", &style, luafilenm);
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
    // surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, iw, ih);
    surface = cairo_svg_surface_create(outfilenm, iw, ih);
    cairo_svg_surface_restrict_to_version(surface, CAIRO_SVG_VERSION_1_2);
    cr = cairo_create(surface);
    // cairo_rectangle(cr, 0, 0, iw, ih);
    // cairo_set_source_rgb(cr, 255, 255, 255);
    cairo_fill(cr);
    // make label surface/context
    cairo_surface_t *lsurface;
    cairo_t *lcr;
    cairo_rectangle_t surfext = {0.0, 0.0, iw, ih};
    lsurface = cairo_recording_surface_create(CAIRO_CONTENT_COLOR_ALPHA, &surfext);
    lcr = cairo_create(lsurface);
    // geom/feature/layer variables
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
            if(j && !(j % 200)) {
                fprintf(stderr, ".");
                if(!(j % 10000)) {
                    fprintf(stderr, " (%ld/%d)\n", j, lfcount);
                }
            } else if(j == (lfcount-1)) {
                fprintf(stderr, "done.\n");
            }
            vfr_draw_geom(cr, ftr, geom, &ext, pxw, pxh, style);
            vfr_draw_label(lcr, ftr, geom, &ext, pxw, pxh, style);
            OGR_F_Destroy(ftr);
        }
    }
    fprintf(stderr, "painting labels over shapes...\n");
    cairo_set_source_surface(cr, lsurface, 0.0, 0.0);
    cairo_paint(cr);
    fprintf(stderr, "written to %s\n", outfilenm);
    // fprintf(stderr, "writing to %s\n", outfilenm);
    // cairo_surface_write_to_png(surface, outfilenm);
    // cairo_surface_write_to_png(lsurface, "test.png");
    cairo_destroy(lcr);
    cairo_surface_destroy(lsurface);
    cairo_destroy(cr);
    cairo_surface_flush(surface);
    cairo_surface_destroy(surface);
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
    OGRFeatureDefnH ftrdef;
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

    // add metadata for layer name, geom type, etc.
    ftrdef = OGR_F_GetDefnRef(ftr);
    lua_pushstring(L, "_vfr_layer");
    lua_pushstring(L, OGR_FD_GetName(ftrdef));
    lua_settable(L, -3);
    lua_pushstring(L, "_vfr_geomtype");
    lua_pushstring(L, OGR_G_GetGeometryName(OGR_F_GetGeometryRef(ftr)));
    lua_settable(L, -3);

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
        fprintf(stderr, "style is not a valid lua table\n");
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
        } else {
            style->fill = 0x01000000;
            lua_pop(L, 1);
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
    //fprintf(stderr, "rendering %s\n", OGR_G_GetGeometryName(geom));
    
    switch(wkbFlatten(OGR_G_GetGeometryType(geom))) {
        case wkbPoint:
            vfr_draw_point(cr, geom, ext, pxw, pxh, style);
            break;
        case wkbPolygon:
            vfr_draw_polygon(cr, geom, ext, pxw, pxh, style);
            break;
        case wkbLineString:
            vfr_draw_linestring(cr, geom, ext, pxw, pxh, style);
            break;
        case wkbLinearRing:
            printf("ring!\n");
            break;
        case wkbMultiPolygon:
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
            gcount = OGR_G_GetGeometryCount(geom);
            for(g=0; g < gcount; g++) {
                geom2 = OGR_G_GetGeometryRef(geom, g);
                vfr_draw_geom(cr, ftr, geom2, ext, pxw, pxh, style);
            }
            break;
        default:
            printf("unknown geometry type (%s)- ignoring!\n", OGR_G_GetGeometryName(geom));
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
    if(style->fill <= 0xffffff) {
        cairo_set_source_rgb(cr, 
                vfr_color_compextr(style->fill, 'r'), 
                vfr_color_compextr(style->fill, 'g'), 
                vfr_color_compextr(style->fill, 'b')); 
        cairo_fill_preserve(cr);
    }
    cairo_set_source_rgb(cr, 
            vfr_color_compextr(style->stroke, 'r'),
            vfr_color_compextr(style->stroke, 'g'),
            vfr_color_compextr(style->stroke, 'b'));
    cairo_set_line_width(cr, 1);
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
    if(style->fill <= 0xffffff) {
        cairo_set_source_rgb(cr, 
                vfr_color_compextr(style->fill, 'r'), 
                vfr_color_compextr(style->fill, 'g'), 
                vfr_color_compextr(style->fill, 'b')); 
        cairo_fill_preserve(cr);
    }
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
    
    if(!style->label_place) return 0;
    
    fprintf(stderr, "label feature #%ld\n", OGR_F_GetFID(ftr));

    PangoFontDescription *fdesc;
    PangoLayout *plyo;
    int i, pcount, fieldidx;
    OGRGeometryH centroid;
    OGREnvelope envelope;
    cairo_path_t *ftrpath, *lblpath, *lyopath;
    PangoLayoutLine *line;
    double x, y, z, pxx, pxy, wrap_width;
    int lyow, lyoh;
    paramd_path_t paramd_ftrpath, paramd_lblpath;
    param_t* params;
    double pathlen, lblwidth;

    cairo_set_source_rgb(cr,
            vfr_color_compextr(style->label_fill, 'r'),
            vfr_color_compextr(style->label_fill, 'g'),
            vfr_color_compextr(style->label_fill, 'b'));

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

    // position
    switch(wkbFlatten(OGR_G_GetGeometryType(geom))) {
        case wkbPoint:
            OGR_G_GetPoint(geom, 0, &x, &y, &z);
            pxx = (x - ext->MinX)/pxw;
            pxx += style->size/2.0;
            pxy = (ext->MaxY - y)/pxh;
            cairo_move_to(cr, pxx, pxy);
            pango_cairo_show_layout(cr, plyo);
            break;
        case wkbMultiLineString:
            fprintf(stderr, "multilinestring labelling not implemented\n");
            break;
        case wkbLineString:
            // trace line path
            cairo_new_path(cr);
            pcount = OGR_G_GetPointCount(geom);
            if(!pcount) return 0;
            for(i = 0; i < pcount; i++) {
                OGR_G_GetPoint(geom, i, &x, &y, &z);
                pxx = (x - ext->MinX)/pxw;
                pxy = (ext->MaxY - y)/pxh;
                if(!i) {
                    cairo_move_to(cr, pxx, pxy);
                    continue;
                } else {
                    cairo_line_to(cr, pxx, pxy);
                }
            }
            // save and copy line path
            cairo_save(cr);
            ftrpath = cairo_copy_path_flat(cr);
            // parametrize path
            params = parametrize_path(ftrpath, &pathlen);
            paramd_ftrpath.params = params;
            paramd_ftrpath.path = ftrpath;
            paramd_ftrpath.length = pathlen;
            // get middle segments as new path (.5 = midpoint)
            pango_layout_get_size(plyo, &lyow, &lyoh);
            lblwidth = (double)lyow/PANGO_SCALE;
            lblpath = get_linear_label_path(cr, &paramd_ftrpath, lblwidth, 0.5);
            cairo_path_destroy(ftrpath);
            ftrpath = NULL;
            paramd_ftrpath.path = NULL;
            free(params);
            paramd_ftrpath.params = NULL;
            if(lblpath) {
                params = parametrize_path(lblpath, &pathlen);
                paramd_lblpath.params = params;
                paramd_lblpath.path = lblpath;
                paramd_lblpath.length = pathlen;
                // get layout path
                line = pango_layout_get_line_readonly(plyo, 0);
                pxx = (&lblpath->data[0])[1].point.x;
                pxy = (&lblpath->data[0])[1].point.y;
                cairo_move_to(cr, pxx, pxy);
                cairo_new_path(cr);
                pango_cairo_layout_line_path(cr, line);
                lyopath = cairo_copy_path_flat(cr);
                cairo_new_path(cr);
                // put layout points on path
                transform_label_points(lyopath, &paramd_lblpath);
                cairo_append_path(cr, lyopath);
                cairo_path_destroy(lyopath);
                lyopath = NULL;
                cairo_set_source_rgb(cr, 1.0, 0.0, 0.0);
                cairo_fill(cr);
            } else {
                centroid = OGR_G_CreateGeometry(wkbPoint);
                if(OGR_G_Centroid(geom, centroid) == OGRERR_FAILURE) {
                    fprintf(stderr, "could not get centroid\n");
                }

                if(style->label_width) {
                    wrap_width = style->label_width/pxw*PANGO_SCALE;
                } else {
                    OGR_G_GetEnvelope(geom, &envelope); 
                    wrap_width = (envelope.MaxX-envelope.MinX)/pxw*PANGO_SCALE;
                }

                OGR_G_GetPoint(centroid, 0, &x, &y, &z);
                pxx = (x - ext->MinX)/pxw;
                pxx -= (wrap_width/PANGO_SCALE)/2.0;
                pxy = (ext->MaxY - y)/pxh;

                pango_layout_set_alignment(plyo, PANGO_ALIGN_CENTER);
                pango_layout_set_width(plyo, -1);
                pango_layout_get_size(plyo, &lyow, &lyoh);
                pxy -= lyoh/PANGO_SCALE/2.0;
                cairo_move_to(cr, pxx, pxy);
                pango_cairo_show_layout(cr, plyo);
            }
            break;
        case wkbPolygon:
        default:
            centroid = OGR_G_CreateGeometry(wkbPoint);
            if(OGR_G_Centroid(geom, centroid) == OGRERR_FAILURE) {
                fprintf(stderr, "could not get centroid\n");
            }

            if(style->label_width) {
                wrap_width = style->label_width/pxw*PANGO_SCALE;
            } else {
                OGR_G_GetEnvelope(geom, &envelope); 
                wrap_width = (envelope.MaxX-envelope.MinX)/pxw*PANGO_SCALE;
            }

            OGR_G_GetPoint(centroid, 0, &x, &y, &z);
            pxx = (x - ext->MinX)/pxw;
            pxx -= (wrap_width/PANGO_SCALE)/2.0;
            pxy = (ext->MaxY - y)/pxh;

            pango_layout_set_alignment(plyo, PANGO_ALIGN_CENTER);
            pango_layout_set_width(plyo, wrap_width);
            pango_layout_set_wrap(plyo, PANGO_WRAP_WORD_CHAR);
            pango_layout_get_size(plyo, &lyow, &lyoh);
            pxy -= lyoh/PANGO_SCALE/2.0;
            cairo_move_to(cr, pxx, pxy);
            pango_cairo_show_layout(cr, plyo);
            break;
    }
   
    pango_font_description_free(fdesc);
    
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

static double euclid_dist(cairo_path_data_t *pt1, cairo_path_data_t *pt2) {
    double dx, dy;
    dx = pt2->point.x - pt1->point.x;
    dy = pt2->point.y - pt1->point.y;
    return sqrt(dx*dx + dy*dy);    
}

static param_t* parametrize_path(cairo_path_t *path, double *plen) {
    
    int i;
    cairo_path_data_t *pdat, lastmove, curpt;
    param_t *params;

    params = malloc(path->num_data*sizeof(param_t));
    if(!params) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }

    *plen = 0.0;
    
    for(i=0; i < path->num_data; i += path->data[i].header.length) {
        pdat = &path->data[i];
        params[i] = 0.0;
        switch(pdat->header.type) {
            case CAIRO_PATH_MOVE_TO:
                lastmove = pdat[1];
                curpt = pdat[1];
                break;
            case CAIRO_PATH_CLOSE_PATH:
                // close path by simulating line_to to
                // most recent move_to
                pdat = (&lastmove-1);
            case CAIRO_PATH_LINE_TO:
                params[i] = euclid_dist(&curpt, &pdat[1]);
                *plen += params[i];
                curpt = pdat[1];
                break;
            case CAIRO_PATH_CURVE_TO:
            default:
                fprintf(stderr, "labelling on curves not supported\n");
                exit(1);
                break;
        }
    }

    return params;
}

static cairo_path_t* get_linear_label_path(cairo_t *cr, paramd_path_t *parpath, double txtwidth, double placeat) {

    int i,j;
    cairo_path_t *src, *path;
    cairo_path_data_t curpt, nxtpt;
    param_t *params;
    double plen, dx, dy, rat, stx, sty, enx, eny;

    if(txtwidth > parpath->length) {
        return NULL;
    }

    src = parpath->path;
    params = parpath->params;
    plen = parpath->length*placeat;

    cairo_save(cr);
    cairo_new_path(cr);

    // walk to beginning of label region
    for(i=0; i<src->num_data && ((plen-params[i])>(txtwidth/2.0)); 
            i += src->data[i].header.length) {
        plen -= params[i];
        curpt = (&src->data[i])[1];
    }
    nxtpt = (&src->data[i])[1];
    dx = nxtpt.point.x - curpt.point.x;
    dy = nxtpt.point.y - curpt.point.y;
    rat = (plen-txtwidth/2.0)/params[i];
    stx = curpt.point.x+rat*dx;
    sty = curpt.point.y+rat*dy;
    cairo_move_to(cr, stx, sty);
    plen += txtwidth/2.0;

    // walk to end of label region
    for(j=i; j<src->num_data && ((plen-params[j])>0.0);
            j += src->data[j].header.length) {
        plen -= params[j];
        curpt = (&src->data[j])[1];
        cairo_line_to(cr, curpt.point.x, curpt.point.y);
    }
    nxtpt = (&src->data[j])[1];
    rat = plen/params[j];
    dx = nxtpt.point.x - curpt.point.x;
    dy = nxtpt.point.y - curpt.point.y;

    enx = curpt.point.x+rat*dx;
    eny = curpt.point.y+rat*dy;
    cairo_line_to(cr, enx, eny);

    path = cairo_copy_path_flat(cr);

    cairo_restore(cr);

    return path;
}

static void transform_label_points(cairo_path_t *lyopath, paramd_path_t *paramd_lblpath) {

    cairo_path_data_t *pdat;
    int i;
    double *xptr, *yptr;

    for(i=0; i<lyopath->num_data;
            i+=lyopath->data[i].header.length) {
        // walk segments...
        pdat = &lyopath->data[i];
        switch(pdat->header.type) {
            case CAIRO_PATH_CURVE_TO:
                // TODO: approximate w/ segments
                fprintf(stderr, "curves not supported\n");
                exit(1);
                break;
            case CAIRO_PATH_MOVE_TO:
            case CAIRO_PATH_LINE_TO:
                // transform point
                xptr = &pdat[1].point.x;
                yptr = &pdat[1].point.y;
                transform_label_point(paramd_lblpath, xptr, yptr);
                break;
            case CAIRO_PATH_CLOSE_PATH:
                break;
            default:
                fprintf(stderr, "invalid path data type\n");
                exit(1);
                break;
        }
    }

    return;
}

static void transform_label_point(paramd_path_t *paramd_lblpath, double *xptr, double *yptr) {

    int i;
    double rat, x=*xptr, y=*yptr, dx, dy;
    cairo_path_data_t *pdat, lastmoveto, curpt;
    cairo_path_t *lblpath;
    param_t *params;

    lblpath = paramd_lblpath->path;
    params = paramd_lblpath->params;

    for (i=0; i + lblpath->data[i].header.length < lblpath->num_data &&
            (x > params[i] ||
             lblpath->data[i].header.type == CAIRO_PATH_MOVE_TO);
            i += lblpath->data[i].header.length) {
        x -= params[i];
        pdat = &lblpath->data[i];
        switch (pdat->header.type) {
            case CAIRO_PATH_MOVE_TO:
                curpt = pdat[1];
                lastmoveto = pdat[1];
                break;
            case CAIRO_PATH_LINE_TO:
                curpt = pdat[1];
                break;
            case CAIRO_PATH_CURVE_TO:
                curpt = pdat[3];
                break;
            case CAIRO_PATH_CLOSE_PATH:
                break;
            default:
                fprintf(stderr, "invalid path data type\n");
                exit(1);
                break;
        }
    }

    pdat = &lblpath->data[i];

    switch (pdat->header.type) {
        case CAIRO_PATH_MOVE_TO:
            break;
        case CAIRO_PATH_CLOSE_PATH:
            pdat = (&lastmoveto) - 1;
        case CAIRO_PATH_LINE_TO:

            // modelled on Behdad Esfahbod's technique in cairo examples:
            // https://github.com/phuang/pango/blob/master/examples/cairotwisted.c
            // http://mces.blogspot.com/2008/11/text-on-path-with-cairo.html)
            
            rat = x/params[i];

            /* Line polynomial */
            *xptr = curpt.point.x*(1-rat)+pdat[1].point.x*rat;
            *yptr = curpt.point.y*(1-rat)+pdat[1].point.y*rat;

            /* Line gradient */
            dx = -(curpt.point.x-pdat[1].point.x);
            dy = -(curpt.point.y-pdat[1].point.y);

            /*optimization for: ratio = the_y / sqrt (dx * dx + dy * dy);*/
            rat = y/params[i];
            *xptr += -dy*rat;
            *yptr +=  dx*rat;
            break;
        case CAIRO_PATH_CURVE_TO:
            fprintf(stderr, "labelling curves not supported\n");
            exit(1);
            break;
        default:
            fprintf(stderr, "invalid path data type\n");
            exit(1);
            break;
    }

    return;
}
