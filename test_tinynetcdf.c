#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "tinynetcdf.h"

#define ASSERT(cond, msg) \
    do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } \
         else { printf("  OK: %s\n", msg); } } while(0)

/* -----------------------------------------------------------------------
 * Test 1: write a simple file
 * --------------------------------------------------------------------- */
static int test_write(void) {
    printf("\n=== test_write ===\n");

    ncf_file_t *f = ncf_open("test.nc", 'w');
    ASSERT(f != NULL, "open for write");

    /* global attribute */
    const char *hist = "Created by tinynetcdf test";
    ASSERT(ncf_put_att(f, NULL, "history", NC_CHAR,
                       strlen(hist), hist) == NCF_OK,
           "put global att history");

    /* dimensions */
    ASSERT(ncf_def_dim(f, "time", 5)  == NCF_OK, "def dim time");
    ASSERT(ncf_def_dim(f, "lat",  3)  == NCF_OK, "def dim lat");
    ASSERT(ncf_def_dim(f, "lon",  4)  == NCF_OK, "def dim lon");

    /* variable: time (1-D) */
    const char *time_dims[] = {"time"};
    ASSERT(ncf_def_var(f, "time", NC_FLOAT, 1, time_dims) == NCF_OK,
           "def var time");
    float time_data[5] = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f};
    ASSERT(ncf_put_var(f, "time", time_data) == NCF_OK,
           "put var time");
    const char *units = "days since 2024-01-01";
    ASSERT(ncf_put_att(f, "time", "units", NC_CHAR,
                       strlen(units), units) == NCF_OK,
           "put att time:units");

    /* variable: temp (3-D) */
    const char *temp_dims[] = {"time", "lat", "lon"};
    ASSERT(ncf_def_var(f, "temp", NC_FLOAT, 3, temp_dims) == NCF_OK,
           "def var temp");
    float temp_data[5*3*4];
    for (int i = 0; i < 5*3*4; i++) temp_data[i] = (float)i * 0.5f;
    ASSERT(ncf_put_var(f, "temp", temp_data) == NCF_OK,
           "put var temp");
    float fill = -9999.0f;
    ASSERT(ncf_put_att(f, "temp", "_FillValue", NC_FLOAT, 1, &fill) == NCF_OK,
           "put att temp:_FillValue");

    ASSERT(ncf_close(f) == NCF_OK, "close write");
    return 0;
}

/* -----------------------------------------------------------------------
 * Test 2: read back and verify
 * --------------------------------------------------------------------- */
static int test_read(void) {
    printf("\n=== test_read ===\n");

    ncf_file_t *f = ncf_open("test.nc", 'r');
    ASSERT(f != NULL, "open for read");

    /* check dims */
    int32_t len;
    ASSERT(ncf_inq_dim(f, "time", &len) == NCF_OK && len == 5, "dim time=5");
    ASSERT(ncf_inq_dim(f, "lat",  &len) == NCF_OK && len == 3, "dim lat=3");
    ASSERT(ncf_inq_dim(f, "lon",  &len) == NCF_OK && len == 4, "dim lon=4");

    /* check global att */
    nc_type_t type; size_t alen; void *aval;
    ASSERT(ncf_get_att(f, NULL, "history", &type, &alen, &aval) == NCF_OK,
           "get global att history");
    ASSERT(type == NC_CHAR, "history type=NC_CHAR");
    printf("  history = \"%.*s\"\n", (int)alen, (char *)aval);

    /* check time variable */
    void *data; size_t nbytes;
    ASSERT(ncf_get_var(f, "time", &data, &nbytes) == NCF_OK, "get var time");
    float *tdata = (float *)data;
    ASSERT(nbytes == 5 * sizeof(float), "time nbytes");
    for (int i = 0; i < 5; i++) {
        ASSERT(fabsf(tdata[i] - (float)i) < 1e-5f, "time value");
    }

    /* check temp variable */
    ASSERT(ncf_get_var(f, "temp", &data, &nbytes) == NCF_OK, "get var temp");
    float *tmp = (float *)data;
    ASSERT(nbytes == 5*3*4 * sizeof(float), "temp nbytes");
    for (int i = 0; i < 5*3*4; i++) {
        ASSERT(fabsf(tmp[i] - (float)i * 0.5f) < 1e-5f, "temp value");
    }

    /* check fill value att */
    ASSERT(ncf_get_att(f, "temp", "_FillValue", &type, &alen, &aval) == NCF_OK,
           "get att temp:_FillValue");
    ASSERT(fabsf(*(float *)aval - (-9999.0f)) < 1e-3f, "_FillValue=-9999");

    ASSERT(ncf_close(f) == NCF_OK, "close read");
    return 0;
}

/* -----------------------------------------------------------------------
 * Test 3: double precision
 * --------------------------------------------------------------------- */
static int test_double(void) {
    printf("\n=== test_double ===\n");

    ncf_file_t *f = ncf_open("test_dbl.nc", 'w');
    ASSERT(f != NULL, "open for write");
    ASSERT(ncf_def_dim(f, "x", 4) == NCF_OK, "def dim x");
    const char *dims[] = {"x"};
    ASSERT(ncf_def_var(f, "data", NC_DOUBLE, 1, dims) == NCF_OK, "def var data");
    double ddata[4] = {1.1, 2.2, 3.3, 4.4};
    ASSERT(ncf_put_var(f, "data", ddata) == NCF_OK, "put var data");
    ASSERT(ncf_close(f) == NCF_OK, "close write");

    f = ncf_open("test_dbl.nc", 'r');
    ASSERT(f != NULL, "open for read");
    void *data; size_t nbytes;
    ASSERT(ncf_get_var(f, "data", &data, &nbytes) == NCF_OK, "get var data");
    double *rd = (double *)data;
    for (int i = 0; i < 4; i++)
        ASSERT(fabs(rd[i] - ddata[i]) < 1e-12, "double roundtrip");
    ASSERT(ncf_close(f) == NCF_OK, "close read");
    return 0;
}

/* -----------------------------------------------------------------------
 * Test 4: unlimited (record) dimension
 * --------------------------------------------------------------------- */
static int test_record_dim(void) {
    printf("\n=== test_record_dim ===\n");

    /* Write: time is unlimited, lat/lon are fixed */
    ncf_file_t *f = ncf_open("test_rec.nc", 'w');
    ASSERT(f != NULL, "open for write");

    ASSERT(ncf_def_dim(f, "time", 0) == NCF_OK, "def unlimited dim time");
    ASSERT(ncf_def_dim(f, "lat",  3) == NCF_OK, "def dim lat");
    ASSERT(ncf_def_dim(f, "lon",  4) == NCF_OK, "def dim lon");

    const char *tdims[] = {"time"};
    const char *vdims[] = {"time", "lat", "lon"};
    ASSERT(ncf_def_var(f, "time", NC_DOUBLE, 1, tdims) == NCF_OK, "def var time");
    ASSERT(ncf_def_var(f, "temp", NC_FLOAT,  3, vdims) == NCF_OK, "def var temp");

    /* Append 6 records one at a time */
    for (int r = 0; r < 6; r++) {
        double t = (double)r * 3600.0;
        ASSERT(ncf_put_rec(f, "time", r, &t) == NCF_OK,
               "put_rec time");

        float slab[3*4];
        for (int i = 0; i < 3*4; i++) slab[i] = (float)(r * 100 + i);
        ASSERT(ncf_put_rec(f, "temp", r, slab) == NCF_OK,
               "put_rec temp");
    }

    int32_t nr;
    ASSERT(ncf_inq_nrecs(f, &nr) == NCF_OK && nr == 6, "nrecs=6 before close");
    ASSERT(ncf_close(f) == NCF_OK, "close write");

    /* Read back */
    f = ncf_open("test_rec.nc", 'r');
    ASSERT(f != NULL, "open for read");

    ASSERT(ncf_inq_nrecs(f, &nr) == NCF_OK && nr == 6, "nrecs=6 after read");

    int32_t tlen;
    ASSERT(ncf_inq_dim(f, "time", &tlen) == NCF_OK && tlen == 0,
           "time dim is unlimited (0)");

    for (int r = 0; r < 6; r++) {
        void *data; size_t nb;

        ASSERT(ncf_get_rec(f, "time", r, &data, &nb) == NCF_OK, "get_rec time");
        ASSERT(nb == sizeof(double), "time rec size");
        double expected_t = (double)r * 3600.0;
        ASSERT(fabs(*(double *)data - expected_t) < 1e-9, "time value");

        ASSERT(ncf_get_rec(f, "temp", r, &data, &nb) == NCF_OK, "get_rec temp");
        ASSERT(nb == 3*4*sizeof(float), "temp rec size");
        float *slab = (float *)data;
        for (int i = 0; i < 3*4; i++) {
            ASSERT(fabsf(slab[i] - (float)(r * 100 + i)) < 1e-4f, "temp value");
        }
    }

    ASSERT(ncf_close(f) == NCF_OK, "close read");
    return 0;
}

/* -----------------------------------------------------------------------
 * Test 5: append file
 * --------------------------------------------------------------------- */
static int test_append(void) {
    printf("\n=== test_append ===\n");

    /* First write 3 records */
    ncf_file_t *f = ncf_open("test_append.nc", 'w');
    ASSERT(f != NULL, "open for write");
    ASSERT(ncf_def_dim(f, "time", 0) == NCF_OK, "def unlimited dim");
    ASSERT(ncf_def_dim(f, "x", 4)    == NCF_OK, "def dim x");
    const char *vdims[] = {"time", "x"};
    ASSERT(ncf_def_var(f, "data", NC_FLOAT, 2, vdims) == NCF_OK, "def var");
    for (int r = 0; r < 3; r++) {
        float slab[4]; for (int i = 0; i < 4; i++) slab[i] = (float)(r*10+i);
        ASSERT(ncf_put_rec(f, "data", r, slab) == NCF_OK, "put_rec");
    }
    ASSERT(ncf_close(f) == NCF_OK, "close write");

    /* Append 3 more records */
    f = ncf_open("test_append.nc", 'a');
    ASSERT(f != NULL, "open for append");
    int32_t nr;
    ASSERT(ncf_inq_nrecs(f, &nr) == NCF_OK && nr == 3, "nrecs=3 at open");
    for (int r = 3; r < 6; r++) {
        float slab[4]; for (int i = 0; i < 4; i++) slab[i] = (float)(r*10+i);
        ASSERT(ncf_put_rec(f, "data", r, slab) == NCF_OK, "append put_rec");
    }
    ASSERT(ncf_close(f) == NCF_OK, "close append");

    /* Read back all 6 records */
    f = ncf_open("test_append.nc", 'r');
    ASSERT(f != NULL, "open for read");
    ASSERT(ncf_inq_nrecs(f, &nr) == NCF_OK && nr == 6, "nrecs=6 after append");
    for (int r = 0; r < 6; r++) {
        void *data; size_t nb;
        ASSERT(ncf_get_rec(f, "data", r, &data, &nb) == NCF_OK, "get_rec");
        float *slab = (float *)data;
        for (int i = 0; i < 4; i++)
            ASSERT(fabsf(slab[i] - (float)(r*10+i)) < 1e-4f, "append value");
    }
    ASSERT(ncf_close(f) == NCF_OK, "close read");
    return 0;
}

/* -----------------------------------------------------------------------
 * main
 * --------------------------------------------------------------------- */
int main(void) {
    int fail = 0;
    fail += test_write();
    fail += test_read();
    fail += test_double();
    fail += test_record_dim();
    fail += test_append();
    printf("\n%s\n", fail == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    return fail;
}
