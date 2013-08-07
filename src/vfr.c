#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "ogr_api.h"
#include "ogr_srs_api.h"
#include "cpl_error.h"

#include "cairo.h"

#if defined(__linux__)
#define VFRSYSNAME "Linux"
#else
#define VFRSYSNAME "Unknown"
#endif


typedef struct vfr_style_s {
    uint64_t bgcolor;
} vfr_style_t;

const char *g_progname;

static void usage(void);

static int runrender(int argc, char **argv);
static int runinform(int argc, char **argv);
static int runversion(int argc, char **argv);

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
    fprintf(stderr, "  %s render path [stylepath]\n", g_progname);
    fprintf(stderr, "  %s inform path [stylepath]\n", g_progname);
    fprintf(stderr, "  %s version\n", g_progname);
    fprintf(stderr, "\n");
    exit(1);
}

static int runrender(int argc, char **argv) {
    
    if(argc < 3) {
        usage();
        return(1);
    }

    int iw, ih;
    iw = 3600;
    ih = 1800;
    const char *outfilenm = "vfrout.png";
    vfr_style_t defaultstyle = {0};

    OGRDataSourceH src;
    OGRSFDriverH drvr;
    src = OGROpen(argv[2], FALSE, &drvr);
    if(src == NULL) {
        fprintf(stderr, "could not open %s: %s\n", argv[2], CPLGetLastErrorMsg());
        return 1;
    }
   
    int i, layercount = OGR_DS_GetLayerCount(src), lfcount;
    long j;
    OGREnvelope ext = {};
    OGREnvelope allext = {};
    OGRLayerH layer;
    OGRFeatureH ftr;
    for(i=0; i<layercount; i++) {
        layer = OGR_DS_GetLayer(src, i);
        OGR_L_GetExtent(layer, &ext, 0);
        if(i == 0 || ext.MinX < allext.MinX) {
            allext.MinX = ext.MinX;
        }
        if(i == 0 || ext.MinY < allext.MinY) {
            allext.MinY = ext.MinY;
        }
        if(i == 0 || ext.MaxX > allext.MaxX) {
            allext.MaxX = ext.MaxX;
        }
        if(i == 0 || ext.MaxY > allext.MaxY) {
            allext.MaxY = ext.MaxY;
        }
    }
    printf("\textent: %0.2f, %0.2f, %0.2f, %0.2f\n", 
        allext.MinX, allext.MinY, allext.MaxX, allext.MaxY);
    double pxw, pxh;
    pxw = (ext.MaxX - ext.MinX)/iw;
    pxh = (ext.MaxY - ext.MinY)/ih;
    printf("pxw,pxh: %f, %f\n", pxw, pxh);
    cairo_surface_t *surface;
    cairo_t *cr;
    surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, iw, ih);
    cr = cairo_create(surface);
    cairo_rectangle(cr, 0, 0, iw, ih);
    cairo_set_source_rgb(cr, 255, 255, 255);
    cairo_fill(cr);
    char *json;
    OGRGeometryH geom;
    double x, y, z, pxx, pxy;
    for(i=0; i<layercount; i++) {
        layer = OGR_DS_GetLayer(src, i);
        lfcount = OGR_L_GetFeatureCount(layer, 0);
        for(j=0; j<lfcount; j++) {
            ftr = OGR_L_GetFeature(layer, j);
            geom = OGR_F_GetGeometryRef(ftr);
            json = OGR_G_ExportToJson(geom);
            printf("json: %s\n", json);
            switch(OGR_G_GetGeometryType(geom)) {
                case wkbPoint:
                    OGR_G_GetPoint(geom, 0, &x, &y, &z);
                    pxx = (x - allext.MinX)/pxw;
                    pxy = ((allext.MaxY- y) - allext.MinY)/pxh;
                    printf("%f, %f -> %f, %f\n", x, y, pxx, pxy);
                    cairo_arc(cr, pxx, pxy, 4, 0, 2*M_PI);
                    cairo_set_source_rgb(cr, 255, 0, 0);
                    cairo_fill_preserve(cr);
                    cairo_set_source_rgb(cr, 0, 0, 0);
                    cairo_set_line_width(cr, 4);
                    cairo_stroke(cr);
                    break;
                default:
                    printf("unknown!\n");
            }
            OGRFree(json);
            OGR_F_Destroy(ftr);
        }
    }
    cairo_surface_write_to_png(surface, outfilenm);
    return 0;
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
