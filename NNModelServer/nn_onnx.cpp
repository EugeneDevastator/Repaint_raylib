#ifdef __MINGW32__
# define _Return_type_success_(x)
#endif
#include "nn_onnx.h"
#include "onnxruntime_c_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#ifdef _WIN32
# include <windows.h>
#endif

#ifdef _MSC_VER
# define strdup _strdup
#endif

struct OnnxModel {
    const OrtApi*    api;
    OrtEnv*          env;
    OrtMemoryInfo*   mem;
    OrtSession*      sess;
    const char**     in_names;
    const char**     out_names;
    int              num_in;
    int              num_out;
};

OnnxModel* onnx_load(const char* path) {
    const OrtApi* api = OrtGetApiBase()->GetApi(ORT_API_VERSION);
    if (!api) { fprintf(stderr, "[onnx] GetApi failed\n"); return nullptr; }

    OnnxModel* m = (OnnxModel*)calloc(1, sizeof(OnnxModel));
    if (!m) return nullptr;
    m->api = api;

    OrtStatus* st = nullptr;
    OrtSessionOptions* opts = nullptr;
    OrtAllocator* alloc = nullptr;
#ifdef _WIN32
    std::wstring wpath;
#endif

    st = api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "nnserver", &m->env);
    if (st) { fprintf(stderr, "[onnx] env: %s\n", api->GetErrorMessage(st)); api->ReleaseStatus(st); goto fail; }

#ifdef _WIN32
    {   int wlen = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
        wpath.resize((size_t)wlen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, path, -1, &wpath[0], wlen); }
#endif

    st = api->CreateSessionOptions(&opts);
    if (st) { fprintf(stderr, "[onnx] opts: %s\n", api->GetErrorMessage(st)); api->ReleaseStatus(st); goto fail; }
    api->SetSessionGraphOptimizationLevel(opts, ORT_ENABLE_BASIC);

    st = api->CreateSession(m->env,
#ifdef _WIN32
                            wpath.c_str(),
#else
                            path,
#endif
                            opts, &m->sess);
    api->ReleaseSessionOptions(opts); opts = nullptr;
    if (st) { fprintf(stderr, "[onnx] sess: %s\n", api->GetErrorMessage(st)); api->ReleaseStatus(st); goto fail; }

    st = api->CreateMemoryInfo("Cpu", OrtDeviceAllocator, -1, OrtMemTypeDefault, &m->mem);
    if (st) { fprintf(stderr, "[onnx] mem: %s\n", api->GetErrorMessage(st)); api->ReleaseStatus(st); goto fail; }

    st = api->GetAllocatorWithDefaultOptions(&alloc);
    if (st) { fprintf(stderr, "[onnx] alloc: %s\n", api->GetErrorMessage(st)); api->ReleaseStatus(st); goto fail; }

    {   size_t ni = 0, no = 0;
        api->SessionGetInputCount(m->sess, &ni);
        api->SessionGetOutputCount(m->sess, &no);
        m->num_in = (int)ni; m->num_out = (int)no; }

    m->in_names = (const char**)calloc((size_t)m->num_in, sizeof(char*));
    m->out_names = (const char**)calloc((size_t)m->num_out, sizeof(char*));

    for (int i = 0; i < m->num_in; i++) {
        char* n; api->SessionGetInputName(m->sess, i, alloc, &n);
        m->in_names[i] = strdup(n);
        alloc->Free(alloc, n);
    }
    for (int i = 0; i < m->num_out; i++) {
        char* n; api->SessionGetOutputName(m->sess, i, alloc, &n);
        m->out_names[i] = strdup(n);
        alloc->Free(alloc, n);
    }

    return m;

fail:
    if (opts) api->ReleaseSessionOptions(opts);
    onnx_unload(m);
    return nullptr;
}

void onnx_unload(OnnxModel* m) {
    if (!m) return;
    if (m->in_names) {
        for (int i = 0; i < m->num_in; i++) free((void*)m->in_names[i]);
        free(m->in_names);
    }
    if (m->out_names) {
        for (int i = 0; i < m->num_out; i++) free((void*)m->out_names[i]);
        free(m->out_names);
    }
    if (m->sess) m->api->ReleaseSession(m->sess);
    if (m->mem)  m->api->ReleaseMemoryInfo(m->mem);
    if (m->env)  m->api->ReleaseEnv(m->env);
    free(m);
}

void onnx_print_info(OnnxModel* m) {
    if (!m) return;
    OrtAllocator* alloc = nullptr;
    OrtStatus* st = m->api->GetAllocatorWithDefaultOptions(&alloc);
    if (st) { m->api->ReleaseStatus(st); return; }

    for (int i = 0; i < m->num_in; i++) {
        char* n; m->api->SessionGetInputName(m->sess, i, alloc, &n);
        OrtTypeInfo* ti = nullptr;
        m->api->SessionGetInputTypeInfo(m->sess, i, &ti);
        const OrtTensorTypeAndShapeInfo* tsi = nullptr;
        m->api->CastTypeInfoToTensorInfo(ti, &tsi);
        size_t nd = 0;
        m->api->GetDimensionsCount(tsi, &nd);
        int64_t dims[4] = {0};
        m->api->GetDimensions(tsi, dims, 4);
        m->api->ReleaseTypeInfo(ti);
        fprintf(stderr, "  input[%d] \"%s\" shape=[%ld,%ld,%ld,%ld] ndims=%zu\n",
                i, n, (long)dims[0], (long)dims[1], (long)dims[2], (long)dims[3], nd);
        alloc->Free(alloc, n);
    }
    for (int i = 0; i < m->num_out; i++) {
        char* n; m->api->SessionGetOutputName(m->sess, i, alloc, &n);
        OrtTypeInfo* ti = nullptr;
        m->api->SessionGetOutputTypeInfo(m->sess, i, &ti);
        const OrtTensorTypeAndShapeInfo* tsi = nullptr;
        m->api->CastTypeInfoToTensorInfo(ti, &tsi);
        size_t nd = 0;
        m->api->GetDimensionsCount(tsi, &nd);
        int64_t dims[4] = {0};
        m->api->GetDimensions(tsi, dims, 4);
        m->api->ReleaseTypeInfo(ti);
        fprintf(stderr, "  output[%d] \"%s\" shape=[%ld,%ld,%ld,%ld] ndims=%zu\n",
                i, n, (long)dims[0], (long)dims[1], (long)dims[2], (long)dims[3], nd);
        alloc->Free(alloc, n);
    }
}

bool onnx_run(OnnxModel* m,
              const char* const* in_names,
              const float* const* in_data,
              const int64_t* in_shapes,
              int num_in,
              const char* const* out_names,
              float** out_data,
              int64_t* out_shapes,
              int num_out) {
    std::vector<OrtValue*> invals(num_in, nullptr);
    std::vector<OrtValue*> outvals(num_out, nullptr);
    bool ok = false;

    for (int i = 0; i < num_in; i++) {
        int64_t n_elem = 1;
        for (int d = 0; d < 4; d++) n_elem *= in_shapes[i * 4 + d];
        OrtStatus* st = m->api->CreateTensorWithDataAsOrtValue(
            m->mem, (void*)in_data[i], (size_t)n_elem * sizeof(float),
            &in_shapes[i * 4], 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &invals[i]);
        if (st) {
            fprintf(stderr, "[onnx] input tensor %d: %s\n", i, m->api->GetErrorMessage(st));
            m->api->ReleaseStatus(st);
            goto cleanup;
        }
    }

    {
        const OrtValue* const* cin = (const OrtValue* const*)invals.data();
        OrtStatus* st = m->api->Run(m->sess, nullptr,
            in_names, cin, num_in,
            out_names, num_out, outvals.data());
        if (st) {
            fprintf(stderr, "[onnx] Run: %s\n", m->api->GetErrorMessage(st));
            m->api->ReleaseStatus(st);
            goto cleanup;
        }
    }

    for (int i = 0; i < num_out; i++) {
        float* data = nullptr;
        OrtStatus* st = m->api->GetTensorMutableData(outvals[i], (void**)&data);
        if (st) { fprintf(stderr, "[onnx] get data: %s\n", m->api->GetErrorMessage(st)); m->api->ReleaseStatus(st); goto cleanup; }

        OrtTensorTypeAndShapeInfo* info = nullptr;
        st = m->api->GetTensorTypeAndShape(outvals[i], &info);
        if (st) { fprintf(stderr, "[onnx] get shape: %s\n", m->api->GetErrorMessage(st)); m->api->ReleaseStatus(st); goto cleanup; }

        m->api->GetDimensions(info, &out_shapes[i * 4], 4);
        m->api->ReleaseTensorTypeAndShapeInfo(info);

        int64_t n_elem = 1;
        for (int d = 0; d < 4; d++) n_elem *= out_shapes[i * 4 + d];
        out_data[i] = (float*)malloc((size_t)n_elem * sizeof(float));
        memcpy(out_data[i], data, (size_t)n_elem * sizeof(float));
    }

    ok = true;

cleanup:
    for (auto v : invals) if (v) m->api->ReleaseValue(v);
    for (auto v : outvals) if (v) m->api->ReleaseValue(v);
    return ok;
}
